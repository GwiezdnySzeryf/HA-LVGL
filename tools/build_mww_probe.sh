#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TFLM_ROOT=${TFLM_ROOT:-/tmp/opencode/tflite-micro}
SHELLY_ROOT=${SHELLY_ROOT:-/tmp/opencode/ShellyElevate}
TOOLCHAIN_ROOT=${TOOLCHAIN_ROOT:-/tmp/opencode/toolchain/bin}
BUILD_ROOT=${BUILD_ROOT:-/tmp/opencode/mww-aarch64}
TFLM_BUILD=gen/linux_aarch64_release_with_logs_gcc
FRONTEND_ROOT="$SHELLY_ROOT/app/src/main/cpp/tflm"
FRONTEND_LIB="$FRONTEND_ROOT/tensorflow/lite/experimental/microfrontend/lib"
DOWNLOADS="$TFLM_ROOT/tensorflow/lite/micro/tools/make/downloads"
CC="$TOOLCHAIN_ROOT/aarch64-none-linux-gnu-gcc"
CXX="$TOOLCHAIN_ROOT/aarch64-none-linux-gnu-g++"
EXPECTED_TFLM_REV=330b1747c9d51c0e394f51a2a34ff42deb9b95f5
EXPECTED_SHELLY_REV=45bf8211

[ "$(git -C "$TFLM_ROOT" rev-parse HEAD)" = "$EXPECTED_TFLM_REV" ] || {
    printf 'Unexpected TFLM revision\n' >&2
    exit 1
}
case "$(git -C "$SHELLY_ROOT" rev-parse HEAD)" in
    "$EXPECTED_SHELLY_REV"*) ;;
    *) printf 'Unexpected ShellyElevate revision\n' >&2; exit 1 ;;
esac

mkdir -p "$BUILD_ROOT"
rm -f "$BUILD_ROOT"/*.o "$BUILD_ROOT/mww_probe"

make -C "$TFLM_ROOT" -f tensorflow/lite/micro/tools/make/Makefile \
    TARGET=linux TARGET_ARCH=aarch64 \
    TARGET_TOOLCHAIN_ROOT="$TOOLCHAIN_ROOT/" \
    TARGET_TOOLCHAIN_PREFIX=aarch64-none-linux-gnu- \
    BUILD_TYPE=release_with_logs microlite

for SOURCE in \
    frontend.c frontend_util.c window.c window_util.c \
    filterbank.c filterbank_util.c noise_reduction.c noise_reduction_util.c \
    pcan_gain_control.c pcan_gain_control_util.c \
    log_scale.c log_scale_util.c log_lut.c kiss_fft.c tools/kiss_fftr.c
do
    OBJECT=$(printf '%s' "$SOURCE" | tr '/' '_' | sed 's/\.c$/.o/')
    "$CC" -O2 -std=c11 -DFIXED_POINT=16 -ffunction-sections -fdata-sections \
        -I"$FRONTEND_ROOT" -I"$FRONTEND_LIB" -I"$FRONTEND_LIB/tools" \
        -c "$FRONTEND_LIB/$SOURCE" -o "$BUILD_ROOT/$OBJECT"
done

for SOURCE in fft.cc fft_util.cc
do
    "$CXX" -O2 -std=c++17 -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections \
        -I"$FRONTEND_ROOT" -I"$FRONTEND_LIB" -I"$FRONTEND_LIB/tools" \
        -c "$FRONTEND_LIB/$SOURCE" -o "$BUILD_ROOT/${SOURCE%.cc}.o"
done

"$CXX" -O2 -std=c++17 -static -s -fno-exceptions -fno-rtti \
    -DTF_LITE_STATIC_MEMORY -ffunction-sections -fdata-sections \
    -I"$TFLM_ROOT" -I"$FRONTEND_ROOT" \
    -I"$DOWNLOADS" -I"$DOWNLOADS/flatbuffers/include" \
    -I"$DOWNLOADS/gemmlowp" -I"$DOWNLOADS/kissfft" -I"$DOWNLOADS/ruy" \
    "$PROJECT_ROOT/prototypes/mww_probe.cpp" "$BUILD_ROOT"/*.o \
    "$TFLM_ROOT/$TFLM_BUILD/lib/libtensorflow-microlite.a" \
    -Wl,--gc-sections -lm -o "$BUILD_ROOT/mww_probe"

printf 'Built %s\n' "$BUILD_ROOT/mww_probe"
