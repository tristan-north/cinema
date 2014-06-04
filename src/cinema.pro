TEMPLATE = app
CONFIG += console  # Comment this out to not generate console window on startup.
CONFIG -= qt

TARGET = ../cinema

INCLUDEPATH += ../../SDL2-2.0.3/include
INCLUDEPATH += ../../glew-1.10.0/include
INCLUDEPATH += ../../ovr_sdk_win_0.3.2/OculusSDK/LibOVR/Include
INCLUDEPATH += ../../ovr_sdk_win_0.3.2/OculusSDK/LibOVR/Src
INCLUDEPATH += ../../ffmpeg-20140528-git-bbc10a1-win32-dev/include
LIBS += ../../glew-1.10.0/lib/Release/Win32/glew32.lib
LIBS += ../../SDL2-2.0.3/lib/x86/SDL2.lib
LIBS += ../../SDL2-2.0.3/lib/x86/SDL2main.lib
LIBS += ../../ovr_sdk_win_0.3.2/oculusSDK/LibOVR/Lib/Win32/VS2013/libovr.lib
LIBS += -lwinspool -lgdi32 -luser32 -lkernel32 -lwinmm -lcomdlg32 -ladvapi32 -lshell32 -lole32 -loleaut32 -luuid -lOpenGL32

# FFmpeg libs
LIBS += -L../../ffmpeg-20140528-git-bbc10a1-win32-dev/lib/
LIBS += -lavformat -lavcodec -lavutil -lswscale -lswresample

#QMAKE_CXXFLAGS += /FS # Prevents a compiler error.
#QMAKE_LFLAGS += /ENTRY:"mainCRTStartup"  # Entry point is main not WinMain

SOURCES += main.cpp \
	objloader.cpp \
    utilities.cpp \
    loadtexture.cpp \
    video.cpp

HEADERS += \
	objloader.h \
    utilities.h \
    loadtexture.h \
    todo.h \
    video.h

