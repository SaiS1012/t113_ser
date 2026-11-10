QT       += core gui
QT       += network
QT       += serialport
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    HailinFingerprint.cpp \
    audioreceiverthread.cpp \
    audiosenderthread.cpp \
    camera/camera_udp_server.cpp \
    camera/v4l2camera.cpp \
    camera_push_thread.cpp \
    form_call.cpp \
    form_finger.cpp \
    form_passwd.cpp \
    main.cpp \
    managers/audio_manager.cpp \
    managers/face_manager.cpp \
    managers/fingerprint_manager.cpp \
    managers/password_manager.cpp \
    widget.cpp

HEADERS += \
    HailinFingerprint.h \
    audioreceiverthread.h \
    audiosenderthread.h \
    camera/camera_udp_server.h \
    camera/v4l2camera.h \
    camera_push_thread.h \
    camera_sender.h \
    form_call.h \
    form_finger.h \
    form_passwd.h \
    managers/audio_manager.h \
    managers/face_manager.h \
    managers/fingerprint_manager.h \
    managers/password_manager.h \
    widget.h

FORMS += \
    form_call.ui \
    form_finger.ui \
    form_passwd.ui \
    widget.ui

INCLUDEPATH += -I /home/meetyoo/t113/Tina-Linux/out/t113-bingpi_m2/staging_dir/target/usr/include/
LIBS +=  -L /home/meetyoo/t113/Tina-Linux/out/t113-bingpi_m2/staging_dir/target/usr/lib/ -lasound

INCLUDEPATH += /home/meetyoo/opencv_arm_build/_install/include/opencv4

LIBS += -L/home/meetyoo/opencv_arm_build/_install/lib  -lopencv_videoio -lopencv_video -lopencv_core -lopencv_calib3d -lopencv_flann  \
                -lopencv_features2d -lopencv_highgui -lopencv_face -lopencv_imgproc \
                -lopencv_dnn -lopencv_imgcodecs   -lopencv_objdetect -lopencv_photo

INCLUDEPATH += /home/meetyoo/gpio/libgpiod/_install/include/
LIBS += /home/meetyoo/gpio/libgpiod/_install/lib/libgpiod.a

LIBS += -L/home/meetyoo/t113/Tina-Linux/out/t113-bingpi_m2/staging_dir/target/rootfs/usr/lib/arm-qt/lib
LIBS += -Wl,-rpath-link,/home/meetyoo/t113/Tina-Linux/out/t113-bingpi_m2/staging_dir/target/usr/lib
LIBS += -Wl,-rpath-link,/home/meetyoo/t113/Tina-Linux/out/t113-bingpi_m2/staging_dir/target/rootfs/usr/lib/arm-qt/lib
LIBS += -lz -ljpeg -lpng12 -lQt5Test

TARGET = smartpro

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

LIBS += -lasound -lpthread

RESOURCES += \
    res.qrc

QT += serialport
