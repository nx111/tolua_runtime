#!/bin/bash
# 64 Bit Version
# build for Ubuntu18.04

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: build_ubuntu.sh [-fr2]

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

UBUNTU_LUAJIT_XCFLAGS="${UBUNTU_LUAJIT_XCFLAGS:-}"
if [ "$ENABLE_FR2" -eq 1 ]; then
  UBUNTU_LUAJIT_XCFLAGS="${UBUNTU_LUAJIT_XCFLAGS:+$UBUNTU_LUAJIT_XCFLAGS }-DLUAJIT_ENABLE_GC64"
  echo "[INFO] LuaJIT GC64: enabled (-fr2)"
fi

mkdir -p ubuntu
mkdir -p Plugins/ubuntu

cd luajit-2.1
make clean

make BUILDMODE=static CC="gcc -fPIC -m64 -O2" XCFLAGS="$UBUNTU_LUAJIT_XCFLAGS"
cp src/libluajit.a ../ubuntu/libluajit.a
make clean

echo -e "\n[MAINTAINCE] build libluajit.a done\n"

cd ..

gcc -m64 -O2 -std=gnu99 $UBUNTU_LUAJIT_XCFLAGS -shared \
 tolua.c \
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
 luasocket/usocket.c \
 -fPIC\
 -o Plugins/ubuntu/libtolua.so \
 -I./ \
 -Iluajit-2.1/src \
 -Iluasocket \
 -Wl,--whole-archive ubuntu/libluajit.a -Wl,--no-whole-archive -static-libgcc -static-libstdc++

if [ "$?" = "0" ]; then
	echo -e "\n[MAINTAINCE] build libtolua.so success"
else
	echo -e "\n[MAINTAINCE] build libtolua.so failed"
fi
