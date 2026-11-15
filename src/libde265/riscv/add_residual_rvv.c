/*
 * libde265 RVV 移植：add_residual（残差加法）。
 *
 * 语义与 fallback 完全一致：
 *   dst[y*stride+x] = Clip_BitDepth(dst[y*stride+x] + r[y*nT+x], bit_depth)
 * 其中 r 是 int32 残差（反变换后的中间结果），nT ∈ {4,8,16,32}。
 *
 * 这个函数在 transform.cc 里每个变换块都会调用一次（主路径，不只是 bypass），
 * 所以它虽然"简单"，但热度不低。
 *
 * 注意：不假设 VLEN/MVL，chunk 上限用 vsetvlmax 取。
 */
#include <stdint.h>
#include <stddef.h>
#include <riscv_vector.h>

static void AddResidualU8 (uint8_t* pDst, ptrdiff_t iStride, const int32_t* pR,
                           int iN, int iMax) {
  size_t vmax = __riscv_vsetvlmax_e32m2 ();
  int y;

  for (y = 0; y < iN; y++) {
    int x = 0;
    while (x < iN) {
      size_t vl = (size_t) (iN - x) < vmax ? (size_t) (iN - x) : vmax;
      vint32m2_t vR = __riscv_vle32_v_i32m2 (pR + y * iN + x, vl);
      vuint32m2_t vPx = __riscv_vzext_vf4_u32m2 (__riscv_vle8_v_u8mf2 (pDst + y * iStride + x, vl), vl);
      vint32m2_t vSum = __riscv_vadd_vv_i32m2 (vR, __riscv_vreinterpret_v_u32m2_i32m2 (vPx), vl);
      vSum = __riscv_vmax_vx_i32m2 (vSum, 0, vl);
      vSum = __riscv_vmin_vx_i32m2 (vSum, iMax, vl);
      vuint32m2_t vClamped = __riscv_vreinterpret_v_i32m2_u32m2 (vSum);
      vuint16m1_t v16 = __riscv_vnclipu_wx_u16m1 (vClamped, 0, vl);
      vuint8mf2_t v8 = __riscv_vnclipu_wx_u8mf2 (v16, 0, vl);
      __riscv_vse8_v_u8mf2 (pDst + y * iStride + x, v8, vl);
      x += (int) vl;
    }
  }
}

static void AddResidualU16 (uint16_t* pDst, ptrdiff_t iStride, const int32_t* pR,
                            int iN, int iMax) {
  size_t vmax = __riscv_vsetvlmax_e32m2 ();
  int y;

  for (y = 0; y < iN; y++) {
    int x = 0;
    while (x < iN) {
      size_t vl = (size_t) (iN - x) < vmax ? (size_t) (iN - x) : vmax;
      vint32m2_t vR = __riscv_vle32_v_i32m2 (pR + y * iN + x, vl);
      vuint32m2_t vPx = __riscv_vzext_vf2_u32m2 (__riscv_vle16_v_u16m1 (pDst + y * iStride + x, vl), vl);
      vint32m2_t vSum = __riscv_vadd_vv_i32m2 (vR, __riscv_vreinterpret_v_u32m2_i32m2 (vPx), vl);
      vSum = __riscv_vmax_vx_i32m2 (vSum, 0, vl);
      vSum = __riscv_vmin_vx_i32m2 (vSum, iMax, vl);
      vuint32m2_t vClamped = __riscv_vreinterpret_v_i32m2_u32m2 (vSum);
      vuint16m1_t v16 = __riscv_vnclipu_wx_u16m1 (vClamped, 0, vl);
      __riscv_vse16_v_u16m1 (pDst + y * iStride + x, v16, vl);
      x += (int) vl;
    }
  }
}

void add_residual_8_rvv (uint8_t* pDst, ptrdiff_t iStride, const int32_t* pR,
                         int iN, int iBitDepth) {
  AddResidualU8 (pDst, iStride, pR, iN, (1 << iBitDepth) - 1);
}

void add_residual_16_rvv (uint16_t* pDst, ptrdiff_t iStride, const int32_t* pR,
                          int iN, int iBitDepth) {
  AddResidualU16 (pDst, iStride, pR, iN, (1 << iBitDepth) - 1);
}
