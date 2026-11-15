#!/usr/bin/env bash
# 构建上游 libde265（cmake）→ <out>/prefix。
# 关掉 dec265 的 SDL 显示与示例，只留库；基线两侧都 -DENABLE_ASSEMBLY=OFF，
# 让 ARM NEON / riscv 的差异不进入基线（留给 G3 差分）。
set -euo pipefail
SRC=$1
OUT=$2
rm -rf "${OUT}"
mkdir -p "${OUT}/prefix" "${OUT}/src"
cp -R "${SRC}" "${OUT}/src/pkg"
cd "${OUT}/src/pkg"
ARGS=(-S . -B "${OUT}/bld" -G Ninja -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX="${OUT}/prefix"
      -DBUILD_SHARED_LIBS=OFF -DENABLE_SDL=OFF -DENABLE_ASSEMBLY=OFF
      -DENABLE_DECODER=OFF -DENABLE_ENCODER=OFF)
if [ -n "${HOST:-}" ]; then
  ARGS+=(-DCMAKE_TOOLCHAIN_FILE=/work/cmake/toolchain-riscv64-linux.cmake
         -DCMAKE_C_FLAGS="-march=${MARCH:-rv64gc} ${EXTRA_C_FLAGS:-}"
         -DCMAKE_CXX_FLAGS="-march=${MARCH:-rv64gc} ${EXTRA_C_FLAGS:-}")
fi
cmake "${ARGS[@]}" >/dev/null
ninja -C "${OUT}/bld" >/dev/null
ninja -C "${OUT}/bld" install >/dev/null
echo "== 上游 libde265 构建完成：${OUT}/prefix"
