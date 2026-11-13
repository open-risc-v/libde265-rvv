#!/usr/bin/env bash
# 真实包用例：解码仓库内固定的 HEVC 语料（10 帧 64x64），比像素。
set -euo pipefail
: "${STAGE:=run}" "${SIDE:=A}" "${BIN:=build/native}" "${DATA:=build/ab-data}" "${OUT:=build/ab-data/out}"
mkdir -p "$DATA" "$OUT"
case "$STAGE" in
  gen) cp "$(dirname "$0")/../data/tiny-64x64.265" "$DATA/frames.265" ;;
  run) "$BIN/libde265_harness" "$DATA/frames.265" "$OUT/de265.yuv" > "$OUT/hash.txt" ;;
  *) echo "unknown STAGE=$STAGE" >&2; exit 2 ;;
esac
