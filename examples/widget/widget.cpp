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

#include "widget.h"
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qlabel.h>
#include <QtCore/qdatetime.h>
#include <QtWidgets/qpushbutton.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qpainter.h>
#include "../../utilities.h"
#include "../../framelesswindowsmanager.h"

FRAMELESSHELPER_USE_NAMESPACE

Widget::Widget(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    createWinId();
    setupUi();
    startTimer(500);
}

Widget::~Widget() = default;

void Widget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    static bool inited = false;
    if (!inited) {
        QWindow *win = windowHandle();
        FramelessWindowsManager::addWindow(win);
        FramelessWindowsManager::setHitTestVisibleInChrome(win, m_minimizeButton, true);
        FramelessWindowsManager::setHitTestVisibleInChrome(win, m_maximizeButton, true);
        FramelessWindowsManager::setHitTestVisibleInChrome(win, m_closeButton, true);
        setContentsMargins(1, 1, 1, 1);
        inited = true;
    }
}

void Widget::timerEvent(QTimerEvent *event)
{
    QWidget::timerEvent(event);
    m_label->setText(QTime::currentTime().toString(QStringLiteral("hh:mm:ss")));
}

void Widget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    bool shouldUpdate = false;
    if (event->type() == QEvent::WindowStateChange) {
        if (isMaximized() || isFullScreen()) {
            setContentsMargins(0, 0, 0, 0);
            m_maximizeButton->setIcon(QIcon{QStringLiteral(":/images/button_restore_black.svg")});
        } else if (!isMinimized()) {
            setContentsMargins(1, 1, 1, 1);
            m_maximizeButton->setIcon(QIcon{QStringLiteral(":/images/button_maximize_black.svg")});
        }
        shouldUpdate = true;
    } else if (event->type() == QEvent::ActivationChange) {
        shouldUpdate = true;
    }
    if (shouldUpdate) {
        update();
    }
}

void Widget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (windowState() == Qt::WindowNoState) {
        QPainter painter(this);
        const int w = width();
        const int h = height();
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        using BorderLines = QList<QLine>;
#else
        using BorderLines = QVector<QLine>;
#endif
        const BorderLines lines = {
            {0, 0, w, 0},
            {w - 1, 0, w - 1, h},
            {w, h - 1, 0, h - 1},
            {0, h, 0, 0}
        };
        painter.save();
        painter.setPen({isActiveWindow() ? Qt::black : Qt::darkGray, 1});
        painter.drawLines(lines);
        painter.restore();
    }
}

void Widget::setupUi()
{
    const QWindow *win = windowHandle();
    const int resizeBorderHeight = FramelessWindowsManager::getResizeBorderHeight(win);
    const int titleBarHeight = FramelessWindowsManager::getTitleBarHeight(win);
    const int systemButtonHeight = titleBarHeight + resizeBorderHeight;
    const QSize systemButtonSize = {qRound(static_cast<qreal>(systemButtonHeight) * 1.5), systemButtonHeight};
    m_minimizeButton = new QPushButton(this);
    m_minimizeButton->setObjectName(QStringLiteral("MinimizeButton"));
    m_minimizeButton->setFixedSize(systemButtonSize);
    m_minimizeButton->setIcon(QIcon{QStringLiteral(":/images/button_minimize_black.svg")});
    m_minimizeButton->setIconSize(systemButtonSize);
    connect(m_minimizeButton, &QPushButton::clicked, this, &Widget::showMinimized);
    m_maximizeButton = new QPushButton(this);
    m_maximizeButton->setObjectName(QStringLiteral("MaximizeButton"));
    m_maximizeButton->setFixedSize(systemButtonSize);
    m_maximizeButton->setIcon(QIcon{QStringLiteral(":/images/button_maximize_black.svg")});
    m_maximizeButton->setIconSize(systemButtonSize);
    connect(m_maximizeButton, &QPushButton::clicked, this, [this](){
        if (isMaximized() || isFullScreen()) {
            showNormal();
            m_maximizeButton->setIcon(QIcon{QStringLiteral(":/images/button_maximize_black.svg")});
        } else {
            showMaximized();
            m_maximizeButton->setIcon(QIcon{QStringLiteral(":/images/button_restore_black.svg")});
        }
    });
    m_closeButton = new QPushButton(this);
    m_closeButton->setObjectName(QStringLiteral("CloseButton"));
    m_closeButton->setFixedSize(systemButtonSize);
    m_closeButton->setIcon(QIcon{QStringLiteral(":/images/button_close_black.svg")});
    m_closeButton->setIconSize(systemButtonSize);
    connect(m_closeButton, &QPushButton::clicked, this, &Widget::close);
    const auto systemButtonLayout = new QHBoxLayout;
    systemButtonLayout->setContentsMargins(0, 0, 0, 0);
    systemButtonLayout->setSpacing(0);
    systemButtonLayout->addStretch();
    systemButtonLayout->addWidget(m_minimizeButton);
    systemButtonLayout->addWidget(m_maximizeButton);
    systemButtonLayout->addWidget(m_closeButton);
    m_label = new QLabel(this);
    QFont font = QGuiApplication::font();
    font.setBold(true);
    font.setPointSize(70);
    m_label->setFont(font);
    m_label->setFrameShape(QFrame::NoFrame);
    const auto contentLayout = new QHBoxLayout;
    contentLayout->addStretch();
    contentLayout->addWidget(m_label);
    contentLayout->addStretch();
    const auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addLayout(systemButtonLayout);
    mainLayout->addStretch();
    mainLayout->addLayout(contentLayout);
    mainLayout->addStretch();
    setLayout(mainLayout);
    setStyleSheet(QStringLiteral(R"(
#MinimizeButton, #MaximizeButton, #CloseButton {
  border-style: none;
  background-color: transparent;
}

#MinimizeButton:hover, #MaximizeButton:hover {
  background-color: #80c7c7c7;
}

#MinimizeButton:pressed, #MaximizeButton:pressed {
  background-color: #80808080;
}

#CloseButton:hover {
  background-color: #e81123;
}

#CloseButton:pressed {
  background-color: #8c0a15;
}
)"));
}
