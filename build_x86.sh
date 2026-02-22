#!/bin/bash
# Android/x86, x86 (i686 SSE3), Android 4.0+ (ICS)
NDK=/mnt/d/Mobile/sdk/linux/ndk/19.2.5345600
NDKABI=19
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
NDKP=$TOOLCHAIN/bin/i686-linux-android-
CC=$TOOLCHAIN/bin/i686-linux-android$NDKABI-clang

cd luajit-2.1/src
make clean
make HOST_CC="gcc -m32" CROSS=$NDKP STATIC_CC=$CC DYNAMIC_CC="$CC -fPIC" TARGET_LD=$CC TARGET_SYS=Linux
cp ./libluajit.a ../../android/jni/libluajit.a
make clean

cd ../../android
$NDK/ndk-build clean APP_ABI="x86"
$NDK/ndk-build APP_ABI="x86"
mkdir -p ../Plugins/Android/libs/x86
cp libs/x86/libtolua.so ../Plugins/Android/libs/x86
$NDK/ndk-build clean APP_ABI="x86"