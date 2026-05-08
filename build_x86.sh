#!/bin/bash
# Android/x86, x86 (i686 SSE3), Android 4.0+ (ICS)
NDK=${NDK:-/mnt/d/Mobile/sdk/linux/ndk/android-ndk-r10e}

if [ -d "$NDK/toolchains/llvm/prebuilt/linux-x86_64" ]; then
	NDKABI=19
	TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
	NDKP=$TOOLCHAIN/bin/i686-linux-android-
	CC=$TOOLCHAIN/bin/i686-linux-android$NDKABI-clang
	TARGET_FLAGS=
else
	TOOLCHAIN=$NDK/toolchains/x86-4.8/prebuilt/linux-x86_64
	NDKP=$TOOLCHAIN/bin/i686-linux-android-
	CC=$NDKP"gcc"
	TARGET_FLAGS=--sysroot=$NDK/platforms/android-16/arch-x86
fi

cd luajit-2.1/src
make clean
make HOST_CC="gcc -m32" CROSS=$NDKP STATIC_CC=$CC DYNAMIC_CC="$CC -fPIC" TARGET_LD=$CC TARGET_SYS=Linux TARGET_FLAGS="$TARGET_FLAGS"
cp ./libluajit.a ../../android/jni/libluajit.a
make clean

cd ../../android
$NDK/ndk-build clean APP_ABI="x86"
$NDK/ndk-build APP_ABI="x86"
mkdir -p ../Plugins/Android/libs/x86
cp libs/x86/libtolua.so ../Plugins/Android/libs/x86
$NDK/ndk-build clean APP_ABI="x86"