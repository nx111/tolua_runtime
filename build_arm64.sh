#!/bin/bash
# Android/arm64-v8a, Android 5.0+ (Lollipop)
NDK=/mnt/d/Mobile/sdk/linux/ndk/19.2.5345600
NDKABI=21
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
NDKP=$TOOLCHAIN/bin/aarch64-linux-android-
CC=$TOOLCHAIN/bin/aarch64-linux-android$NDKABI-clang

cd luajit-2.1/src
make clean
make HOST_CC="gcc -m64" CROSS=$NDKP STATIC_CC=$CC DYNAMIC_CC="$CC -fPIC" TARGET_LD=$CC TARGET_SYS=Linux XCFLAGS="-DLUAJIT_ENABLE_GC64"
cp ./libluajit.a ../../android/jni/libluajit.a
make clean

cd ../../android
$NDK/ndk-build clean APP_ABI="arm64-v8a"
$NDK/ndk-build APP_ABI="arm64-v8a"
mkdir -p ../Plugins/Android/libs/arm64-v8a
cp libs/arm64-v8a/libtolua.so ../Plugins/Android/libs/arm64-v8a
$NDK/ndk-build clean APP_ABI="arm64-v8a"