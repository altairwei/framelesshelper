/*
 * MIT License
 *
 * Copyright (C) 2021 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "utilities.h"
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qscreen.h>
#include <QtGui/qpa/qplatformwindow.h>
#include <QtGui/qpainter.h>
#include <QtGui/private/qmemrotate_p.h>
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#include <QtGui/qpa/qplatformnativeinterface.h>
#else
#include <QtGui/qpa/qplatformwindow_p.h>
#endif
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
#include <QtCore/qoperatingsystemversion.h>
#else
#include <QtCore/qsysinfo.h>
#endif

Q_DECLARE_METATYPE(QMargins)

/*
 * Copied from https://code.qt.io/cgit/qt/qtbase.git/tree/src/widgets/effects/qpixmapfilter.cpp
 * With minor modifications, most of them are format changes.
 */

#ifndef AVG
#define AVG(a,b)  ( ((((a)^(b)) & 0xfefefefeUL) >> 1) + ((a)&(b)) )
#endif

#ifndef AVG16
#define AVG16(a,b)  ( ((((a)^(b)) & 0xf7deUL) >> 1) + ((a)&(b)) )
#endif

template<const int shift>
static inline int qt_static_shift(const int value)
{
    if (shift == 0) {
        return value;
    } else if (shift > 0) {
        return value << (uint(shift) & 0x1f);
    } else {
        return value >> (uint(-shift) & 0x1f);
    }
}

template<const int aprec, const int zprec>
static inline void qt_blurinner(uchar *bptr, int &zR, int &zG, int &zB, int &zA, const int alpha)
{
    QRgb *pixel = reinterpret_cast<QRgb *>(bptr);
#define Z_MASK (0xff << zprec)
    const int A_zprec = qt_static_shift<zprec - 24>(*pixel) & Z_MASK;
    const int R_zprec = qt_static_shift<zprec - 16>(*pixel) & Z_MASK;
    const int G_zprec = qt_static_shift<zprec - 8>(*pixel)  & Z_MASK;
    const int B_zprec = qt_static_shift<zprec>(*pixel)      & Z_MASK;
#undef Z_MASK
    const int zR_zprec = zR >> aprec;
    const int zG_zprec = zG >> aprec;
    const int zB_zprec = zB >> aprec;
    const int zA_zprec = zA >> aprec;
    zR += alpha * (R_zprec - zR_zprec);
    zG += alpha * (G_zprec - zG_zprec);
    zB += alpha * (B_zprec - zB_zprec);
    zA += alpha * (A_zprec - zA_zprec);
#define ZA_MASK (0xff << (zprec + aprec))
    *pixel =
        qt_static_shift<24 - zprec - aprec>(zA & ZA_MASK)
        | qt_static_shift<16 - zprec - aprec>(zR & ZA_MASK)
        | qt_static_shift<8 - zprec - aprec>(zG & ZA_MASK)
        | qt_static_shift<-zprec - aprec>(zB & ZA_MASK);
#undef ZA_MASK
}

static const int alphaIndex = ((QSysInfo::ByteOrder == QSysInfo::BigEndian) ? 0 : 3);

template<const int aprec, const int zprec>
static inline void qt_blurinner_alphaOnly(uchar *bptr, int &z, const int alpha)
{
    const int A_zprec = int(*(bptr)) << zprec;
    const int z_zprec = z >> aprec;
    z += alpha * (A_zprec - z_zprec);
    *(bptr) = z >> (zprec + aprec);
}

template<const int aprec, const int zprec, const bool alphaOnly>
static inline void qt_blurrow(QImage &im, const int line, const int alpha)
{
    uchar *bptr = im.scanLine(line);
    int zR = 0, zG = 0, zB = 0, zA = 0;
    if (alphaOnly && (im.format() != QImage::Format_Indexed8)) {
        bptr += alphaIndex;
    }
    const int stride = im.depth() >> 3;
    const int im_width = im.width();
    for (int index = 0; index < im_width; ++index) {
        if (alphaOnly) {
            qt_blurinner_alphaOnly<aprec, zprec>(bptr, zA, alpha);
        } else {
            qt_blurinner<aprec, zprec>(bptr, zR, zG, zB, zA, alpha);
        }
        bptr += stride;
    }
    bptr -= stride;
    for (int index = im_width - 2; index >= 0; --index) {
        bptr -= stride;
        if (alphaOnly) {
            qt_blurinner_alphaOnly<aprec, zprec>(bptr, zA, alpha);
        } else {
            qt_blurinner<aprec, zprec>(bptr, zR, zG, zB, zA, alpha);
        }
    }
}

/*
 *  expblur(QImage &img, const qreal radius)
 *
 *  Based on exponential blur algorithm by Jani Huhtanen
 *
 *  In-place blur of image 'img' with kernel
 *  of approximate radius 'radius'.
 *
 *  Blurs with two sided exponential impulse
 *  response.
 *
 *  aprec = precision of alpha parameter
 *  in fixed-point format 0.aprec
 *
 *  zprec = precision of state parameters
 *  zR,zG,zB and zA in fp format 8.zprec
 */
template<const int aprec, const int zprec, const bool alphaOnly>
static inline void expblur(QImage &img, const qreal radius, const bool improvedQuality = false, const int transposed = 0)
{
    qreal _radius = radius;
    // halve the radius if we're using two passes
    if (improvedQuality) {
        _radius *= 0.5;
    }
    Q_ASSERT((img.format() == QImage::Format_ARGB32_Premultiplied)
             || (img.format() == QImage::Format_RGB32)
             || (img.format() == QImage::Format_Indexed8)
             || (img.format() == QImage::Format_Grayscale8));
    // choose the alpha such that pixels at radius distance from a fully
    // saturated pixel will have an alpha component of no greater than
    // the cutOffIntensity
    const qreal cutOffIntensity = 2;
    const int alpha = _radius <= qreal(1e-5)
                    ? ((1 << aprec)-1)
                    : qRound((1<<aprec)*(1 - qPow(cutOffIntensity * (1 / qreal(255)), 1 / _radius)));
    int img_height = img.height();
    for (int row = 0; row < img_height; ++row) {
        for (int i = 0; i <= int(improvedQuality); ++i) {
            qt_blurrow<aprec, zprec, alphaOnly>(img, row, alpha);
        }
    }
    QImage temp(img.height(), img.width(), img.format());
    temp.setDevicePixelRatio(img.devicePixelRatio());
    if (transposed >= 0) {
        if (img.depth() == 8) {
            qt_memrotate270(reinterpret_cast<const quint8*>(img.bits()),
                            img.width(), img.height(), img.bytesPerLine(),
                            reinterpret_cast<quint8*>(temp.bits()),
                            temp.bytesPerLine());
        } else {
            qt_memrotate270(reinterpret_cast<const quint32*>(img.bits()),
                            img.width(), img.height(), img.bytesPerLine(),
                            reinterpret_cast<quint32*>(temp.bits()),
                            temp.bytesPerLine());
        }
    } else {
        if (img.depth() == 8) {
            qt_memrotate90(reinterpret_cast<const quint8*>(img.bits()),
                           img.width(), img.height(), img.bytesPerLine(),
                           reinterpret_cast<quint8*>(temp.bits()),
                           temp.bytesPerLine());
        } else {
            qt_memrotate90(reinterpret_cast<const quint32*>(img.bits()),
                           img.width(), img.height(), img.bytesPerLine(),
                           reinterpret_cast<quint32*>(temp.bits()),
                           temp.bytesPerLine());
        }
    }
    img_height = temp.height();
    for (int row = 0; row < img_height; ++row) {
        for (int i = 0; i <= int(improvedQuality); ++i) {
            qt_blurrow<aprec, zprec, alphaOnly>(temp, row, alpha);
        }
    }
    if (transposed == 0) {
        if (img.depth() == 8) {
            qt_memrotate90(reinterpret_cast<const quint8*>(temp.bits()),
                           temp.width(), temp.height(), temp.bytesPerLine(),
                           reinterpret_cast<quint8*>(img.bits()),
                           img.bytesPerLine());
        } else {
            qt_memrotate90(reinterpret_cast<const quint32*>(temp.bits()),
                           temp.width(), temp.height(), temp.bytesPerLine(),
                           reinterpret_cast<quint32*>(img.bits()),
                           img.bytesPerLine());
        }
    } else {
        img = temp;
    }
}

static inline QImage qt_halfScaled(const QImage &source)
{
    if (source.width() < 2 || source.height() < 2) {
        return {};
    }
    QImage srcImage = source;
    if (source.format() == QImage::Format_Indexed8 || source.format() == QImage::Format_Grayscale8) {
        // assumes grayscale
        QImage dest(source.width() / 2, source.height() / 2, srcImage.format());
        dest.setDevicePixelRatio(source.devicePixelRatio());
        const uchar *src = reinterpret_cast<const uchar*>(const_cast<const QImage &>(srcImage).bits());
        qsizetype sx = srcImage.bytesPerLine();
        qsizetype sx2 = sx << 1;
        uchar *dst = reinterpret_cast<uchar*>(dest.bits());
        qsizetype dx = dest.bytesPerLine();
        int ww = dest.width();
        int hh = dest.height();
        for (int y = hh; y; --y, dst += dx, src += sx2) {
            const uchar *p1 = src;
            const uchar *p2 = src + sx;
            uchar *q = dst;
            for (int x = ww; x; --x, ++q, p1 += 2, p2 += 2) {
                *q = ((int(p1[0]) + int(p1[1]) + int(p2[0]) + int(p2[1])) + 2) >> 2;
            }
        }
        return dest;
    } else if (source.format() == QImage::Format_ARGB8565_Premultiplied) {
        QImage dest(source.width() / 2, source.height() / 2, srcImage.format());
        dest.setDevicePixelRatio(source.devicePixelRatio());
        const uchar *src = reinterpret_cast<const uchar*>(const_cast<const QImage &>(srcImage).bits());
        qsizetype sx = srcImage.bytesPerLine();
        qsizetype sx2 = sx << 1;
        uchar *dst = reinterpret_cast<uchar*>(dest.bits());
        qsizetype dx = dest.bytesPerLine();
        int ww = dest.width();
        int hh = dest.height();
        for (int y = hh; y; --y, dst += dx, src += sx2) {
            const uchar *p1 = src;
            const uchar *p2 = src + sx;
            uchar *q = dst;
            for (int x = ww; x; --x, q += 3, p1 += 6, p2 += 6) {
                // alpha
                q[0] = AVG(AVG(p1[0], p1[3]), AVG(p2[0], p2[3]));
                // rgb
                const quint16 p16_1 = (p1[2] << 8) | p1[1];
                const quint16 p16_2 = (p1[5] << 8) | p1[4];
                const quint16 p16_3 = (p2[2] << 8) | p2[1];
                const quint16 p16_4 = (p2[5] << 8) | p2[4];
                const quint16 result = AVG16(AVG16(p16_1, p16_2), AVG16(p16_3, p16_4));
                q[1] = result & 0xff;
                q[2] = result >> 8;
            }
        }
        return dest;
    } else if ((source.format() != QImage::Format_ARGB32_Premultiplied) && (source.format() != QImage::Format_RGB32)) {
        srcImage = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QImage dest(source.width() / 2, source.height() / 2, srcImage.format());
    dest.setDevicePixelRatio(source.devicePixelRatio());
    const quint32 *src = reinterpret_cast<const quint32*>(const_cast<const QImage &>(srcImage).bits());
    qsizetype sx = srcImage.bytesPerLine() >> 2;
    qsizetype sx2 = sx << 1;
    quint32 *dst = reinterpret_cast<quint32*>(dest.bits());
    qsizetype dx = dest.bytesPerLine() >> 2;
    int ww = dest.width();
    int hh = dest.height();
    for (int y = hh; y; --y, dst += dx, src += sx2) {
        const quint32 *p1 = src;
        const quint32 *p2 = src + sx;
        quint32 *q = dst;
        for (int x = ww; x; --x, q++, p1 += 2, p2 += 2) {
            *q = AVG(AVG(p1[0], p1[1]), AVG(p2[0], p2[1]));
        }
    }
    return dest;
}

void Utilities::blurImage(QPainter *painter, QImage &blurImage, const qreal radius, const bool quality, const bool alphaOnly, const int transposed)
{
    if ((blurImage.format() != QImage::Format_ARGB32_Premultiplied) && (blurImage.format() != QImage::Format_RGB32)) {
        blurImage = blurImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    qreal _radius = radius;
    qreal scale = 1;
    if ((_radius >= 4) && (blurImage.width() >= 2) && (blurImage.height() >= 2)) {
        blurImage = qt_halfScaled(blurImage);
        scale = 2;
        _radius *= 0.5;
    }
    if (alphaOnly) {
        expblur<12, 10, true>(blurImage, _radius, quality, transposed);
    } else {
        expblur<12, 10, false>(blurImage, _radius, quality, transposed);
    }
    if (painter) {
        painter->scale(scale, scale);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(QRect{QPoint{0, 0}, blurImage.size() / blurImage.devicePixelRatio()}, blurImage);
    }
}

void Utilities::blurImage(QImage &blurImage, const qreal radius, const bool quality, const int transposed)
{
    if ((blurImage.format() == QImage::Format_Indexed8) || (blurImage.format() == QImage::Format_Grayscale8)) {
        expblur<12, 10, true>(blurImage, radius, quality, transposed);
    } else {
        expblur<12, 10, false>(blurImage, radius, quality, transposed);
    }
}

///////////////////////////////////////////////////

/*
 * Copied from https://code.qt.io/cgit/qt/qtbase.git/tree/src/widgets/styles/qstyle.cpp
 * With minor modifications, most of them are format changes.
 */

static inline Qt::Alignment visualAlignment(const Qt::LayoutDirection direction, const Qt::Alignment alignment)
{
    return QGuiApplicationPrivate::visualAlignment(direction, alignment);
}

QRect Utilities::alignedRect(const Qt::LayoutDirection direction, const Qt::Alignment alignment, const QSize &size, const QRect &rectangle)
{
    const Qt::Alignment align = visualAlignment(direction, alignment);
    int x = rectangle.x();
    int y = rectangle.y();
    const int w = size.width();
    const int h = size.height();
    if ((align & Qt::AlignVCenter) == Qt::AlignVCenter) {
        y += rectangle.size().height() / 2 - h / 2;
    } else if ((align & Qt::AlignBottom) == Qt::AlignBottom) {
        y += rectangle.size().height() - h;
    }
    if ((align & Qt::AlignRight) == Qt::AlignRight) {
        x += rectangle.size().width() - w;
    } else if ((align & Qt::AlignHCenter) == Qt::AlignHCenter) {
        x += rectangle.size().width() / 2 - w / 2;
    }
    return {x, y, w, h};
}

///////////////////////////////////////////////////

bool Utilities::isWin7OrGreater()
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows7;
#else
    return QSysInfo::WindowsVersion >= QSysInfo::WV_WINDOWS7;
#endif
}

bool Utilities::isWin8OrGreater()
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows8;
#else
    return QSysInfo::WindowsVersion >= QSysInfo::WV_WINDOWS8;
#endif
}

bool Utilities::isWin8Point1OrGreater()
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows8_1;
#else
    return QSysInfo::WindowsVersion >= QSysInfo::WV_WINDOWS8_1;
#endif
}

bool Utilities::isWin10OrGreater()
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows10;
#else
    return QSysInfo::WindowsVersion >= QSysInfo::WV_WINDOWS10;
#endif
}

bool Utilities::isWin10OrGreater(const int subVer)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, subVer);
#else
    Q_UNUSED(ver);
    return QSysInfo::WindowsVersion >= QSysInfo::WV_WINDOWS10;
#endif
}

bool Utilities::isMSWin10AcrylicEffectAvailable()
{
    if (!isWin10OrGreater()) {
        return false;
    }
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    const QOperatingSystemVersion currentVersion = QOperatingSystemVersion::current();
    if (currentVersion > QOperatingSystemVersion::Windows10) {
        return true;
    }
    return ((currentVersion.microVersion() >= 16190) && (currentVersion.microVersion() < 18362));
#else
    // TODO
    return false;
#endif
}

QWindow *Utilities::findWindow(const WId winId)
{
    Q_ASSERT(winId);
    if (!winId) {
        return nullptr;
    }
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (auto &&window : qAsConst(windows)) {
        if (window && window->handle()) {
            if (window->winId() == winId) {
                return window;
            }
        }
    }
    return nullptr;
}

bool Utilities::isAcrylicEffectSupported()
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
    return isWin10OrGreater() || (QOperatingSystemVersion::current() >= QOperatingSystemVersion::OSXYosemite);
#else
    // TODO
    return false;
#endif
}

void Utilities::updateQtFrameMargins(QWindow *window, const bool enable)
{
    Q_ASSERT(window);
    if (!window) {
        return;
    }
    const int tbh = enable ? Utilities::getSystemMetric(window, Utilities::SystemMetric::TitleBarHeight, true, true) : 0;
    const QMargins margins = {0, -tbh, 0, 0};
    const QVariant marginsVar = QVariant::fromValue(margins);
    window->setProperty("_q_windowsCustomMargins", marginsVar);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPlatformWindow *platformWindow = window->handle();
    if (platformWindow) {
        QGuiApplication::platformNativeInterface()->setWindowProperty(platformWindow, QStringLiteral("WindowsCustomMargins"), marginsVar);
    }
#else
    auto *platformWindow = dynamic_cast<QNativeInterface::Private::QWindowsWindow *>(
        window->handle());
    if (platformWindow) {
        platformWindow->setCustomMargins(margins);
    }
#endif
}

QRect Utilities::getScreenAvailableGeometry()
{
    return QGuiApplication::primaryScreen()->availableGeometry();
}

QColor Utilities::getNativeWindowFrameColor(const bool isActive)
{
    if (!isActive) {
        return Qt::darkGray;
    }
    // TODO: what about Linux and macOS?
    return isWin10OrGreater() ? getColorizationColor() : Qt::black;
}