# libde265 G3：8x8 反变换（IDCT）RVV 内核

- 日期：2026-11-15
- 上游：`strukturag/libde265` @ `4fb298ec1af0686211dc2def73f4f41c2ce19ce5`
- 目标板：SpacemiT K3 / Bianbu，`Linux k3 6.18.3 riscv64`，RVV 1.0
- 变量：`-DHAVE_RVV=1`，两侧同为 `-march=rv64gcv -O2 -static`

## 1. 为什么选 IDCT

`add_residual` 的负结论已经说明"每块必调"不等于"占比高"。反变换则是真正的算术密集路径：
每块 `2 × nT` 次 `nT` 长度的点积（V 通 + H 通），且在 `fallback-dct.cc` 的
`transform_idct_add<pixel_t>` 里是唯一的热点；上游 SSE/NEON 实现也是围绕这一步做的。

## 2. 内核设计（`src/libde265/riscv/transform_idct8_rvv.c`）

关键观察：`transform_idct_add` 的两通都是**同一个 `nT×nT` 基矩阵乘**（`mat_dct[fact*j][i]`），
区别只是输入不同（V 通吃 `coeffs[c + j*nT]`，H 通吃中间量 `g[y*nT + j]`）。所以不需要
转置、也不需要 `vrgather`：

```
对输出下标 i 一次算 nT 个（= 一个向量车道）：
   acc[i] = Σ_j mat[j][i] * s[j]
```

即"矩阵的行当向量装载、标量系数广播、累加器按 lane 求和"。`mat` 从上游
`static int8_t mat_dct[32][32]` 在 `fact=4` 取行复制成 `int32`，只在
`transform_idct8_rvv_init()` 里做一次。

- V 通：vl=8、`i32m2`；`(Σ + 64) >> 7` 后钳到 `int16` 范围，再 `vsse16` 跨步写 `g[c + i*8]`。
- H 通：同一矩阵；`(Σ + 2048) >> postShift`（`postShift = 20 - bit_depth`，8bit 时 12），
  `vzext` 抬像素、`vmax/vmin` 钳 0…255、两级 `vnclipu` 收窄，`vse8` 写回。
- 接线：`fallback.cc` 的 `init_acceleration_functions_fallback()` 里
  `transform_add_8[1] ← transform_8x8_add_8_rvv`（4x4/16x16/32x32 仍是 C）。
  `transform.cc:324` 的主路径自动生效。

因为内核要用 `mat_dct`，`#if HAVE_RVV #include "riscv/transform_idct8_rvv.c"` 放在
`fallback-dct.cc` 里 `mat_dct` 定义之后、`transform_idct_fallback` 之前；
`fallback.cc` 只做 init + 指针覆盖。

## 3. 功能验证（qemu，逐字节）

`./scripts/g3-wire-ab.sh`（off/on 双配置 + `qemu-riscv64`）：

```
off: dec frames=10 bytes=61440 fnv_px=cf67fb9981835c6a
on : dec frames=10 bytes=61440 fnv_px=cf67fb9981835c6a
PASS
```

与 G2 基线哈希一致。内核级另有 `test/kernel_idct8.cpp`：在 on 二进制里把
`accel.transform_add_8[idx]` 与 `transform_8x8_add_8_fallback` 逐像素比对后才计时。

## 4. 真机性能

### 4.1 内核级（200k 次/档，走 `acceleration_functions` 指针，两侧各含一次间接调用）

| nT | C | RVV | 比值 |
|---|---|---|---|
| 8 | 888–897 ns | 606–609 ns | **1.46–1.48×** |
| 16 | 6964–6978 ns | 5066–5091 ns | **1.38×** |

注：内核级 8x8 在另一轮测得 1046→608（1.72×），差异来自该轮 off 侧被更高频率的
C 路径命中；**两轮都取"每档 3 次取最优、3 轮交替"的最小值**，本表取保守值 1.46×。

### 4.2 整链（10 帧 64×64，同进程 300 轮解码，5–6 轮交替每轮 3 次取最优）

| 侧 | total_us | 相对 off |
|---|---|---|
| off（全 C） | 806.6–807.6 k | 1.000 |
| on（8x8 RVV） | 803.3–803.8 k | **1.004×（+0.4%，6/6 轮同向）** |

即：8x8 内核 1.46× → 整链 +0.4%。幅度小但方向一致、方差很窄（on 侧 803.3–803.8 k，
off 侧 806.6–807.6 k），不是噪声。

**一次不可复现的读数已作废**：早前 8x8-only 版本曾测到 806.2→785.9 k（1.026×），
后续重建同配置二进制复测稳定回到 803.5 k，疑为当时宿主与板子之间的部署/缓存状态差异；
本证据只采信可复现的 1.004×。

## 5. 16x16 的负结论（实现保留、不接线）

16x16 用同一套方法（vl=16、`i32m4`）实现后：

| 配置 | 整链 total_us | 相对 off |
|---|---|---|
| off | 806.2–807.7 k | 1.000 |
| 8x8 + 16x16（全量算） | 826.7–827.8 k | **0.975×（退步）** |
| 8x8 + 16x16（`nnz<=8` 走 C 的门控） | 808.7–809.9 k | **0.997×（仍退步）** |

归因：真实码流里 16x16 残差极稀疏（多数是 DC 或个位数非零），C 侧
`lastCol` 截断把每通从 `nT` 次乘法砍到 `lastCol+1` 次，几乎免费；而 RVV 侧
要么全量算 16 个 tap，要么先扫 256 个系数做门控——256 次 load/compare 本身就比
它想省的 16 次乘法贵一个量级。**结论：16x16 想向量化，必须先让上层把 nnz/lastCol
传下来（改函数签名），否则门控成本 > 收益。**

代码保留在 `-DHAVE_RVV` 下但不接线（`transform_add_8[2]` 仍是 C），
`transform_16x16_add_8_rvv_sel` 留作后续接 nnz 的接口。

## 6. 结论

- libde265 的第一个**正值**内核：8x8 IDCT，内核 1.46×、整链 +0.4%，功能逐字节一致。
- 32x32（`transform_add_8[3]`）与 4x4（`transform_4x4_dst_add_8` luma）仍未做；
  按 16x16 的结论，它们同样受稀疏性限制，优先级低于 deblocking / qpel 这类
  "无截断优化可用"的路径。
- 复现命令：
  - 功能：`./scripts/g3-wire-ab.sh`
  - 内核：`/root/rvv-k/libde265/idct_off|idct_on <8|16> <reps>`
  - 整链：`/root/rvv-k/libde265/dec_off|dec_on tiny-64x64.265 300`
