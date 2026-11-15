#!/usr/bin/env bash
# G3 内核 A/B（libde265）：HAVE_RVV 编译期开关把 add_residual 换成 RVV 内核。
# 两套构建只差 EXTRA_C_FLAGS 里的一个宏，其余（-march、Release、ENABLE_ASSEMBLY=OFF）一致。
#
#   ./scripts/g3-wire-ab.sh      # 编 on/off，qemu 下解同一段 265 码流并比对 YUV 哈希
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=${IMAGE:-rvv-cross:2}
MARCH=${MARCH:-rv64gcv}
OUT=build/g3-ab

for side in off on; do
  if [ "$side" = on ]; then
    RVFLAG="-DHAVE_RVV=1"
  else
    RVFLAG=""
  fi
  echo "=== 构建 ${side}（额外 CFLAGS: '${RVFLAG:-<无>}'）==="
  docker run --rm -v "$PWD":/work -w /work -e SIDE="$side" -e MARCH="$MARCH" \
    -e RVFLAG="$RVFLAG" -e OUT="$OUT" "${IMAGE}" bash -lc '
set -e
rm -rf "${OUT}/${SIDE}"
mkdir -p "${OUT}/${SIDE}"
HOST=riscv64-linux-gnu MARCH="${MARCH}" EXTRA_C_FLAGS="${RVFLAG}" \
  ./cmake/pkg-build.sh /work/src "/work/${OUT}/${SIDE}/pkg" > /tmp/pkg.log 2>&1 \
  || { echo "pkg-build ${SIDE} FAIL"; tail -30 /tmp/pkg.log; exit 1; }
cmake -S . -B "${OUT}/${SIDE}/harness" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/work/cmake/toolchain-riscv64-linux.cmake \
  -DCMAKE_C_FLAGS="-march=${MARCH}" -DCMAKE_CXX_FLAGS="-march=${MARCH}" \
  -DPKG_PREFIX="/work/${OUT}/${SIDE}/pkg/prefix" > /tmp/h.log 2>&1 \
  || { tail -20 /tmp/h.log; exit 1; }
cmake --build "${OUT}/${SIDE}/harness" -j > /tmp/hb.log 2>&1 || { tail -20 /tmp/hb.log; exit 1; }
mkdir -p "${OUT}/${SIDE}/out"
qemu-riscv64 -L /usr/riscv64-linux-gnu "${OUT}/${SIDE}/harness/libde265_harness" \
  /work/test/data/tiny-64x64.265 "${OUT}/${SIDE}/out/de265.yuv" > "${OUT}/${SIDE}/out/hash.txt"
echo "--- ${SIDE} ---"; cat "${OUT}/${SIDE}/out/hash.txt"
'
done

echo '=== 比对 ==='
if diff -q "$OUT/off/out/hash.txt" "$OUT/on/out/hash.txt" > /dev/null; then
  echo 'PASS：开/关 RVV add_residual 的解码 YUV 完全一致'
else
  echo 'FAIL：两侧哈希不同'; diff "$OUT/off/out/hash.txt" "$OUT/on/out/hash.txt" || true; exit 1
fi
