#-------------------------------------------------
#
# Project created by QtCreator 2014-05-29T21:59:54
#
#-------------------------------------------------

QT       += core

QT       += gui

TARGET = motogame
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app


SOURCES += \
    sha512.cpp \
    render.cpp \
    graphics.cpp \
    game.cpp \
    ../moto-engine.cpp \
    ../moto-protocol.cpp

HEADERS += \
    vec2.hpp \
    render.hpp \
    graphics.hpp \
    ../moto-engine.h \
    ../moto-protocol.h \
    sha512.h \
    debug.h

OTHER_FILES += \
    Makefile \
    game.pro.user \
    game.includes \
    game.files \
    game.creator.user \
    game.creator \
    game.config \
    build-linux.sh
QMAKE_CXXFLAGS+=-std=c++11
QMAKE_CXXFLAGS_DEBUG -= -O2


LIBS+=-lGL -lGLEW -lglfw3 -lX11 -lXxf86vm -lXrandr -lXi -lpthread

INCLUDEPATH += $$BOOST_INCLUDE_PATH $$BDB_INCLUDE_PATH $$OPENSSL_INCLUDE_PATH $$QRENCODE_INCLUDE_PATH
LIBS += $$join(BOOST_LIB_PATH,,-L,) $$join(BDB_LIB_PATH,,-L,) $$join(OPENSSL_LIB_PATH,,-L,) $$join(QRENCODE_LIB_PATH,,-L,)
LIBS += -lssl -lcrypto -ldb_cxx$$BDB_LIB_SUFFIX
