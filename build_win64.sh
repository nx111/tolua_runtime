#!/bin/bash
# 64 Bit Version

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: build_win64.sh [-fr2]

Options:
  -fr2    Enable LuaJIT GC64 build flags.
EOF
}

ENABLE_FR2=0
for arg in "$@"; do
  case "$arg" in
    -fr2)
      ENABLE_FR2=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $arg" >&2
      usage >&2
      exit 1
      ;;
  esac
done

WIN64_LUAJIT_XCFLAGS="${WIN64_LUAJIT_XCFLAGS:-}"
if [ "$ENABLE_FR2" -eq 1 ]; then
  WIN64_LUAJIT_XCFLAGS="${WIN64_LUAJIT_XCFLAGS:+$WIN64_LUAJIT_XCFLAGS }-DLUAJIT_ENABLE_GC64"
fi

mkdir -p window/x86_64
mkdir -p Plugins/x86_64

if [ "$ENABLE_FR2" -eq 1 ]; then
  echo "[INFO] LuaJIT GC64: enabled (-fr2)"
fi

cd luajit-2.1
make clean
make BUILDMODE=static HOST_CC="gcc -m64 -O2" CROSS=x86_64-w64-mingw32- TARGET_SYS=Windows XCFLAGS="$WIN64_LUAJIT_XCFLAGS"
cp src/libluajit.a ../window/x86_64/libluajit.a

cd ..

x86_64-w64-mingw32-gcc -m64 -O2 -std=gnu99 $WIN64_LUAJIT_XCFLAGS -shared \
 tolua.c \
 tolua_fr1_to_fr2.c \
 int64.c \
 uint64.c \
 pb.c \
 lpeg.c \
 struct.c \
 cjson/strbuf.c \
 cjson/lua_cjson.c \
 cjson/fpconv.c \
 luasocket/auxiliar.c \
 luasocket/buffer.c \
 luasocket/except.c \
 luasocket/inet.c \
 luasocket/io.c \
 luasocket/luasocket.c \
 luasocket/mime.c \
 luasocket/options.c \
 luasocket/select.c \
 luasocket/tcp.c \
 luasocket/timeout.c \
 luasocket/udp.c \
 luasocket/wsocket.c \
 -o Plugins/x86_64/tolua.dll \
 -I./ \
 -Iluajit-2.1/src \
 -Iluasocket \
 -lws2_32 \
 -Wl,--whole-archive window/x86_64/libluajit.a -Wl,--no-whole-archive -static-libgcc -static-libstdc++

cd luajit-2.1
make clean
