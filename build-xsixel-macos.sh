#!/bin/sh
set -eu

prefix=${PREFIX:-/usr/local}
x11prefix=${X11_PREFIX:-/opt/X11}
build_dir=${BUILD_DIR:-build-xsixel}
export PKG_CONFIG_PATH="${prefix}/lib/pkgconfig:${x11prefix}/lib/pkgconfig:/opt/homebrew/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
# Set CHAFA=true to enable the optional Chafa renderer.
meson setup --reconfigure "$build_dir" --buildtype=debugoptimized \
    --prefix="$prefix" \
    -Dxkb_dir="${x11prefix}/share/X11/xkb" \
    -Dxkb_bin_dir="${x11prefix}/bin" \
    -Dxsixel=true \
    -Dchafa="${CHAFA:-false}" \
    -Dxorg=false -Dxnest=false -Dxvfb=false \
    -Dxquartz=false -Dxwin=false -Dxephyr=false \
    -Dxfake=false -Dxfbdev=false -Dxmodesetting=false \
    -Dglamor=false -Dglx=false -Ddri1=false -Ddri2=false -Ddri3=false \
    -Dinput_thread=false -Ddocs=false -Ddevel-docs=false \
    "$@"
meson compile -C "$build_dir"
