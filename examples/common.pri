DESTDIR = $$OUT_PWD/../../bin
CONFIG += c++17 strict_c++ utf8_source warn_on
DEFINES += \
    QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII \
    QT_NO_KEYWORDS \
    QT_DEPRECATED_WARNINGS \
    QT_DISABLE_DEPRECATED_BEFORE=0x060100
RESOURCES += $$PWD/images.qrc
win32 {
    CONFIG += windeployqt
    CONFIG -= embed_manifest_exe
    LIBS += -luser32 -lshell32 -ldwmapi
    RC_FILE = $$PWD/example.rc
    OTHER_FILES += $$PWD/example.manifest
}
win32 {
    CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../../debug -lFramelessHelperd
    else: CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../../release -lFramelessHelper
} else: unix {
    LIBS += -L$$OUT_PWD/../../bin -lFramelessHelper
}
