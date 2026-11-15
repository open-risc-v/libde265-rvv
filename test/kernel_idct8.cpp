/* 8x8 反变换内核基准：走库里的 acceleration_functions 指针（与真实解码路径一致，
   两侧都含一次间接调用）。off 二进制里该指针指向 C，on 二进制里指向 RVV。
   用法：kernel_idct8 <reps> [v] */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libde265/acceleration.h>
#include <libde265/fallback.h>
#include <libde265/fallback-dct.h>

static uint32_t st = 12345u;
static int32_t lcg(void) { st = st * 1103515245u + 12345u; return (int32_t)(st >> 8) - (1 << 23); }
static long long now_us(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int main(int argc, char **argv) {
  int nT = argc > 1 ? atoi(argv[1]) : 8;      /* 8 | 16 | 32 */
  int reps = argc > 2 ? atoi(argv[2]) : 100000;
  int verbose = argc > 3 ? atoi(argv[3]) : 0;
  int idx = nT == 8 ? 1 : (nT == 16 ? 2 : 3);

  struct acceleration_functions accel;
  init_acceleration_functions_fallback(&accel);

  const int stride = nT + 16;
  uint8_t *dst0 = (uint8_t *)malloc((size_t)stride * nT);
  uint8_t *work = (uint8_t *)malloc((size_t)stride * nT);
  uint8_t *out2 = (uint8_t *)malloc((size_t)stride * nT);
  static int16_t coeffs[32 * 32];

  for (int i = 0; i < stride * nT; i++) dst0[i] = (uint8_t)(lcg() & 0xff);
  /* 接近真实残差：大部分为 0，少量非零（x265 的典型稀疏度） */
  for (int i = 0; i < nT * nT; i++) coeffs[i] = 0;
  for (int i = 0; i < 10; i++) coeffs[lcg() % (nT * nT)] = (int16_t)(lcg() % 2000);

  /* 正确性：fallback 与指针指向的实现逐像素比对 */
  memcpy(work, dst0, stride * 8);
  transform_8x8_add_8_fallback(work, coeffs, stride);
  memcpy(out2, dst0, stride * 8);
  accel.transform_add_8[1](out2, coeffs, stride);
  int bad = 0;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      if (work[y * stride + x] != out2[y * stride + x]) {
        if (!bad) printf("MISMATCH y=%d x=%d c=%d acc=%d\n", y, x, work[y * stride + x], out2[y * stride + x]);
        bad++;
      }
  if (bad) { printf("FAIL mismatches=%d\n", bad); return 1; }
  if (verbose) printf("verify: %dx%d 逐像素一致\n", nT, nT);

  memcpy(work, dst0, (size_t)stride * nT);
  long long t0 = now_us();
  for (int i = 0; i < reps; i++) accel.transform_add_8[idx](work, coeffs, stride);
  long long dt = now_us() - t0;
  printf("nT=%d reps=%d ns_per_call=%.1f\n", nT, reps, (double)dt * 1000.0 / reps);
  return 0;
}
