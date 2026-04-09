#!/bin/bash
set -e
# Android/arm64-v8a, Android 5.0+ (Lollipop)
NDK=${NDK:-/mnt/d/Mobile/sdk/linux/ndk/android-ndk-r10e}

if [ -d "$NDK/toolchains/llvm/prebuilt/linux-x86_64" ]; then
	NDKABI=21
	TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
	NDKP=$TOOLCHAIN/bin/aarch64-linux-android-
	CC=$TOOLCHAIN/bin/aarch64-linux-android$NDKABI-clang
	TARGET_FLAGS=
else
	TOOLCHAIN=$NDK/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64
	NDKP=$TOOLCHAIN/bin/aarch64-linux-android-
	CC=$NDKP"gcc"
	TARGET_FLAGS=--sysroot=$NDK/platforms/android-21/arch-arm64
fi

cd luajit-2.1/src
make clean
make -j$(nproc --ignore 3) HOST_CC="gcc -m64" CROSS=$NDKP STATIC_CC=$CC DYNAMIC_CC="$CC -fPIC" TARGET_LD=$CC TARGET_SYS=Linux TARGET_FLAGS="$TARGET_FLAGS" XCFLAGS="-DLUAJIT_ENABLE_GC64"
cp ./libluajit.a ../../android/jni/libluajit.a
make clean

cd ../../android
ARM64_LDFLAGS="-Wl,-z,max-page-size=16384"
$NDK/ndk-build clean APP_ABI="arm64-v8a" APP_LDFLAGS="$ARM64_LDFLAGS"
$NDK/ndk-build APP_ABI="arm64-v8a" APP_LDFLAGS="$ARM64_LDFLAGS"
mkdir -p ../Plugins/Android/libs/arm64-v8a
cp libs/arm64-v8a/libtolua.so ../Plugins/Android/libs/arm64-v8a
$NDK/ndk-build clean APP_ABI="arm64-v8a" APP_LDFLAGS="$ARM64_LDFLAGS"
