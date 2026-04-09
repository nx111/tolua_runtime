@echo off
set ndkPath=D:/android-ndk-r15c
set arm64LdfLags=-Wl,-z,max-page-size=16384
cd ./android
call %ndkPath%/ndk-build clean APP_ABI="armeabi-v7a,x86,arm64-v8a" APP_PLATFORM=android-21 APP_LDFLAGS="%arm64LdfLags%"
call %ndkPath%/ndk-build APP_ABI="arm64-v8a" APP_PLATFORM=android-21 APP_LDFLAGS="%arm64LdfLags%"
copy libs\arm64-v8a\libtolua.so ..\Plugins\Android\libs\arm64-v8a
call %ndkPath%/ndk-build clean APP_ABI="armeabi-v7a,x86,arm64-v8a" APP_PLATFORM=android-21 APP_LDFLAGS="%arm64LdfLags%"
echo Successfully linked
exit
