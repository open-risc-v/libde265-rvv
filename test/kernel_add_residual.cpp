/* add_residual 内核微基准（C vs RVV）：
   C 参照逐字拷贝上游 libde265/fallback-dct.h 的 add_residual_fallback 循环。
   RVV 侧同时算 C 与 RVV 并逐值比对，先判正确再计时。
   用法：kernel_add_residual <nT 4|8|16|32> <reps> [v]
*/
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void add_residual_8_rvv(uint8_t *dst, ptrdiff_t stride, const int32_t *r, int nT, int bit_depth);

static inline uint8_t clip_bd(int v, int bd) {
  int max = (1 << bd) - 1;
  return (uint8_t)(v < 0 ? 0 : (v > max ? max : v));
}

/* ===== 逐字拷贝上游 ===== */
static void add_residual_ref(uint8_t *dst, ptrdiff_t stride, const int32_t *r, int nT, int bit_depth) {
  for (int y = 0; y < nT; y++)
    for (int x = 0; x < nT; x++)
      dst[y * stride + x] = clip_bd(dst[y * stride + x] + r[y * nT + x], bit_depth);
}

static uint32_t lcg_state = 12345u;
static int32_t lcg(void) { lcg_state = lcg_state * 1103515245u + 12345u; return (int32_t)(lcg_state >> 8) - (1 << 23); }

static long long now_us(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int main(int argc, char **argv) {
  int nT = argc > 1 ? atoi(argv[1]) : 16;
  int reps = argc > 2 ? atoi(argv[2]) : 200000;
  int verbose = argc > 3 ? atoi(argv[3]) : 0;
  const int stride = nT + 16;

  uint8_t *dst = (uint8_t *)malloc((size_t)stride * nT);
  uint8_t *work = (uint8_t *)malloc((size_t)stride * nT);
  int32_t *res = (int32_t *)malloc(sizeof(int32_t) * nT * nT);
  for (int y = 0; y < nT; y++) for (int x = 0; x < stride; x++) dst[y * stride + x] = (uint8_t)(lcg() & 0xff);
  for (int i = 0; i < nT * nT; i++) res[i] = lcg() % 4096;

  /* 正确性：两侧各自从同一初始图开始 */
  memcpy(work, dst, (size_t)stride * nT);
  add_residual_ref(work, stride, res, nT, 8);
  uint8_t *rvv_out = (uint8_t *)malloc((size_t)stride * nT);
  memcpy(rvv_out, dst, (size_t)stride * nT);
  add_residual_8_rvv(rvv_out, stride, res, nT, 8);

  int bad = 0;
  for (int y = 0; y < nT; y++)
    for (int x = 0; x < nT; x++)
      if (work[y * stride + x] != rvv_out[y * stride + x]) {
        if (!bad) printf("MISMATCH y=%d x=%d c=%d rvv=%d\n", y, x, work[y * stride + x], rvv_out[y * stride + x]);
        bad++;
      }
  if (bad) { printf("FAIL mismatches=%d\n", bad); return 1; }
  if (verbose) printf("verify: %dx%d 全部一致\n", nT, nT);

  /* 计时：C */
  memcpy(work, dst, (size_t)stride * nT);
  long long t0 = now_us();
  for (int i = 0; i < reps; i++) add_residual_ref(work, stride, res, nT, 8);
  long long tc = now_us() - t0;

  /* 计时：RVV */
  memcpy(work, dst, (size_t)stride * nT);
  t0 = now_us();
  for (int i = 0; i < reps; i++) add_residual_8_rvv(work, stride, res, nT, 8);
  long long tr = now_us() - t0;

  printf("nT=%d reps=%d C=%.1fns RVV=%.1fns ratio=%.2fx\n",
         nT, reps, (double)tc * 1000.0 / reps, (double)tr * 1000.0 / reps, (double)tc / (double)tr);
  return 0;
}
