#!/bin/sh
set -e
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/build"
mkdir -p "$out"
srcs="
  $root/RoseGoldC/RoseGoldC.cpp
  $root/RoseGoldC/lexer.cpp
  $root/RoseGoldC/parser.cpp
  $root/RoseGoldC/value.cpp
  $root/RoseGoldC/modules.cpp
  $root/RoseGoldC/constexpr.cpp
  $root/RoseGoldC/typecheck.cpp
  $root/RoseGoldC/eval.cpp
  $root/RoseGoldC/harness.cpp
  $root/RoseGoldC/lsp.cpp
  $root/RoseGoldC/format.cpp
  $root/RoseGoldC/dap.cpp
  $root/RoseGoldC/host_ui.cpp
"
cxx=${CXX:-}
if [ -z "$cxx" ]; then
  if command -v clang++ >/dev/null 2>&1; then
    cxx=clang++
  else
    cxx=c++
  fi
fi
libs=""
wayland_flags=""
os=$(uname -s)
case "$os" in
  Darwin)
    libs="-framework Cocoa -framework Foundation"
    ;;
  Linux|*BSD)
    libs="-lX11"
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists wayland-client; then
      wayland_flags="-DROSEGOLD_WAYLAND $(pkg-config --cflags wayland-client)"
      libs="$libs $(pkg-config --libs wayland-client)"
      if pkg-config --exists xkbcommon; then
        wayland_flags="$wayland_flags -DROSEGOLD_XKB $(pkg-config --cflags xkbcommon)"
        libs="$libs $(pkg-config --libs xkbcommon)"
      fi
    fi
    ;;
esac
exe="$out/RoseGoldC"
$cxx -std=c++20 -g -O0 -Wall -I "$root/RoseGoldC" $wayland_flags -o "$exe" $srcs $libs
echo "wrote $exe"
