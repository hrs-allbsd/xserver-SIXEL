#!/bin/sh
set -eu

# Requires Meson, Ninja, libsixel development files and the XLibre build dependencies.
# Set CHAFA=true to also build the optional Chafa renderer (requires libchafa-dev).
build_dir=${BUILD_DIR:-build-xsixel}
meson setup --reconfigure "$build_dir" --buildtype=debugoptimized \
    -Dxkb_dir=/usr/share/X11/xkb \
    -Dxkb_bin_dir=/usr/bin \
    -Ddefault_font_path=built-ins \
    -Dxsixel=true \
    -Dchafa="${CHAFA:-false}" \
    -Dxorg=false -Dxnest=false -Dxvfb=false \
    -Dxquartz=false -Dxwin=false -Dxephyr=false \
    -Dxfake=false -Dxfbdev=false -Dxmodesetting=false \
    -Dglamor=false -Dglx=false -Ddri1=false -Ddri2=false -Ddri3=false \
    -Dinput_thread=false -Ddocs=false -Ddevel-docs=false \
    "$@"
meson compile -C "$build_dir"
