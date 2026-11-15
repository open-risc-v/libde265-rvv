/*
 * libde265 RVV：HEVC 8x8 反变换 + 加预测（transform_8x8_add_8）。
 *
 * 与 fallback 的 transform_idct_add<uint8_t>(dst,stride,8,coeffs,8) 逐位等价：
 *   V 通: g[c+i*8] = Clip16((Σ_j mat[fact*j][i]*coeffs[c+j*8] + 64) >> 7)
 *   H 通: dst[y*stride+i] = Clip8(dst + ((Σ_j mat[fact*j][i]*g[y*8+j] + 2048) >> 12))
 * nT=8 时 fact = 1<<(5-3) = 4。
 *
 * 向量化思路：两通都是"同一个 8x8 基矩阵的转置乘"——对输出下标 i 一次算 8 个，
 * 即 acc(8 lanes) += mat_vec[j](8 lanes) * scalar_g[j]。系数是标量广播，
 * 所以既不需要转置也不需要 vrgather。
 *
 * 矩阵表从上游 static int8_t mat_dct[32][32] 复制成 int32（本文件被 include 进
 * fallback-dct.cc，所以看得见 mat_dct）；只在 setup 时复制一次，避免每次调用转换。
 */
#include <stdint.h>
#include <stddef.h>
#include <riscv_vector.h>

static int32_t g_idct8_mat[8][8];   /* [j][i]，已按 fact=4 取行 */

void
transform_idct8_rvv_init (void)
{
  int j, i;
  for (j = 0; j < 8; j++)
    for (i = 0; i < 8; i++)
      g_idct8_mat[j][i] = (int32_t) mat_dct[4*j][i];
}

void
transform_8x8_add_8_rvv (uint8_t* pDst, const int16_t* pCoeffs, ptrdiff_t iStride)
{
  const size_t vl = 8;
  int16_t g[64];
  int c, j, y;

  /* ---- V 通：按列算，一次出 8 个 i ---- */
  for (c = 0; c < 8; c++) {
    vint32m2_t vAcc = __riscv_vmv_v_x_i32m2 (0, vl);
    for (j = 0; j < 8; j++) {
      int32_t s = (int32_t) pCoeffs[c + j*8];
      vint32m2_t vM = __riscv_vle32_v_i32m2 (&g_idct8_mat[j][0], vl);
      vAcc = __riscv_vadd_vv_i32m2 (vAcc, __riscv_vmul_vx_i32m2 (vM, s, vl), vl);
    }
    vAcc = __riscv_vsra_vx_i32m2 (__riscv_vadd_vx_i32m2 (vAcc, 64, vl), 7, vl);
    vAcc = __riscv_vmax_vx_i32m2 (vAcc, -32768, vl);
    vAcc = __riscv_vmin_vx_i32m2 (vAcc, 32767, vl);
    vint16m1_t vG = __riscv_vncvt_x_x_w_i16m1 (vAcc, vl);
    /* g[c + i*8]，步长 8 个 int16 */
    __riscv_vsse16_v_i16m1 (g + c, (ptrdiff_t) (8 * sizeof (int16_t)), vG, vl);
  }

  /* ---- H 通：按行算，同样一次出 8 个 i，直接加回像素 ---- */
  for (y = 0; y < 8; y++) {
    vint32m2_t vAcc = __riscv_vmv_v_x_i32m2 (0, vl);
    for (j = 0; j < 8; j++) {
      int32_t s = (int32_t) g[y*8 + j];
      vint32m2_t vM = __riscv_vle32_v_i32m2 (&g_idct8_mat[j][0], vl);
      vAcc = __riscv_vadd_vv_i32m2 (vAcc, __riscv_vmul_vx_i32m2 (vM, s, vl), vl);
    }
    vAcc = __riscv_vsra_vx_i32m2 (__riscv_vadd_vx_i32m2 (vAcc, 2048, vl), 12, vl);

    vuint32m2_t vPx = __riscv_vzext_vf4_u32m2 (__riscv_vle8_v_u8mf2 (pDst + y*iStride, vl), vl);
    vint32m2_t vSum = __riscv_vadd_vv_i32m2 (vAcc, __riscv_vreinterpret_v_u32m2_i32m2 (vPx), vl);
    vSum = __riscv_vmax_vx_i32m2 (vSum, 0, vl);
    vSum = __riscv_vmin_vx_i32m2 (vSum, 255, vl);
    vuint16m1_t v16 = __riscv_vnclipu_wx_u16m1 (__riscv_vreinterpret_v_i32m2_u32m2 (vSum), 0, vl);
    vuint8mf2_t v8 = __riscv_vnclipu_wx_u8mf2 (v16, 0, vl);
    __riscv_vse8_v_u8mf2 (pDst + y*iStride, v8, vl);
  }
}

/* ===================== 16x16 ===================== */
static int32_t g_idct16_mat[16][16];

void
transform_idct16_rvv_init (void)
{
  int j, i;
  for (j = 0; j < 16; j++)
    for (i = 0; i < 16; i++)
      g_idct16_mat[j][i] = (int32_t) mat_dct[2*j][i];
}

void
transform_16x16_add_8_rvv (uint8_t* pDst, const int16_t* pCoeffs, ptrdiff_t iStride)
{
  const size_t vl = 16;
  int16_t g[256];
  int c, j, y;

  for (c = 0; c < 16; c++) {
    vint32m4_t vAcc = __riscv_vmv_v_x_i32m4 (0, vl);
    for (j = 0; j < 16; j++) {
      int32_t s = (int32_t) pCoeffs[c + j*16];
      vint32m4_t vM = __riscv_vle32_v_i32m4 (&g_idct16_mat[j][0], vl);
      vAcc = __riscv_vadd_vv_i32m4 (vAcc, __riscv_vmul_vx_i32m4 (vM, s, vl), vl);
    }
    vAcc = __riscv_vsra_vx_i32m4 (__riscv_vadd_vx_i32m4 (vAcc, 64, vl), 7, vl);
    vAcc = __riscv_vmax_vx_i32m4 (vAcc, -32768, vl);
    vAcc = __riscv_vmin_vx_i32m4 (vAcc, 32767, vl);
    vint16m2_t vG = __riscv_vncvt_x_x_w_i16m2 (vAcc, vl);
    __riscv_vsse16_v_i16m2 (g + c, (ptrdiff_t) (16 * sizeof (int16_t)), vG, vl);
  }

  for (y = 0; y < 16; y++) {
    vint32m4_t vAcc = __riscv_vmv_v_x_i32m4 (0, vl);
    for (j = 0; j < 16; j++) {
      int32_t s = (int32_t) g[y*16 + j];
      vint32m4_t vM = __riscv_vle32_v_i32m4 (&g_idct16_mat[j][0], vl);
      vAcc = __riscv_vadd_vv_i32m4 (vAcc, __riscv_vmul_vx_i32m4 (vM, s, vl), vl);
    }
    vAcc = __riscv_vsra_vx_i32m4 (__riscv_vadd_vx_i32m4 (vAcc, 2048, vl), 12, vl);

    vuint32m4_t vPx = __riscv_vzext_vf4_u32m4 (__riscv_vle8_v_u8m1 (pDst + y*iStride, vl), vl);
    vint32m4_t vSum = __riscv_vadd_vv_i32m4 (vAcc, __riscv_vreinterpret_v_u32m4_i32m4 (vPx), vl);
    vSum = __riscv_vmax_vx_i32m4 (vSum, 0, vl);
    vSum = __riscv_vmin_vx_i32m4 (vSum, 255, vl);
    vuint16m2_t v16 = __riscv_vnclipu_wx_u16m2 (__riscv_vreinterpret_v_i32m4_u32m4 (vSum), 0, vl);
    vuint8m1_t v8 = __riscv_vnclipu_wx_u8m1 (v16, 0, vl);
    __riscv_vse8_v_u8m1 (pDst + y*iStride, v8, vl);
  }
}

/* ---- 16x16 稀疏门控 ----
 * 实测：16x16 全量矩阵乘在真机上比 C 的"lastCol 截断"慢，因为真实解码里 16x16
 * 残差绝大多数只有 DC 或个位数非零系数，C 会跳过大部分乘法。
 * 这里先扫一遍非零个数：很少就直接走 C，只有系数较密才用 RVV。
 * 阈值 8 取自真机二分（见 docs/evidence/G3-kernel-idct8-20261115.md）。 */
void
transform_16x16_add_8_rvv_sel (uint8_t* pDst, const int16_t* pCoeffs, ptrdiff_t iStride)
{
  int i, nnz = 0;
  for (i = 0; i < 256; i++) { nnz += (pCoeffs[i] != 0); }

  if (nnz <= 8) { transform_16x16_add_8_fallback (pDst, pCoeffs, iStride); }
  else          { transform_16x16_add_8_rvv      (pDst, pCoeffs, iStride); }
}
