#!/bin/bash
# Android/ARM, armeabi-v7a (ARMv7 VFP), Android 4.0+ (ICS)
NDK=/mnt/d/Mobile/sdk/linux/ndk/19.2.5345600
NDKABI=19
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
NDKP=$TOOLCHAIN/bin/arm-linux-androideabi-
CC=$TOOLCHAIN/bin/armv7a-linux-androideabi$NDKABI-clang

cd luajit-2.1/src
make clean
make HOST_CC="gcc -m32" CROSS=$NDKP STATIC_CC=$CC DYNAMIC_CC="$CC -fPIC" TARGET_LD=$CC TARGET_SYS=Linux
cp ./libluajit.a ../../android/jni/libluajit.a
make clean

cd ../../android
$NDK/ndk-build clean APP_ABI="armeabi-v7a"
$NDK/ndk-build APP_ABI="armeabi-v7a"
mkdir -p ../Plugins/Android/libs/armeabi-v7a
cp libs/armeabi-v7a/libtolua.so ../Plugins/Android/libs/armeabi-v7a
$NDK/ndk-build clean APP_ABI="armeabi-v7a"