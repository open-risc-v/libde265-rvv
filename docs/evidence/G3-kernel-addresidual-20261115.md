# G3 内核尝试：add_residual（RVV）

日期 2026-11-15 / 板 `<BOARD_HOST>`（K3/Bianbu, rv64gcv, VLEN=256 推断）

## 1. 选它的理由

`libde265/transform.cc:324` 是**主反变换路径**的收尾，每个变换块都调用一次
`acceleration->add_residual()`；457/605 是 bypass / skip 路径。也就是说它是
"每块必调"的函数，虽然语义只是加法+钳位，但调用频次高。

实现：`src/libde265/riscv/add_residual_rvv.c`（`AddResidualU8/U16`，i32m2 分块、
`vzext` 抬像素、`vmax/vmin` 钳到 `(1<<bd)-1`、两级 `vnclipu` 收窄）。
接线：`fallback.cc` 的 `init_acceleration_functions_fallback()` 覆盖
`add_residual_8/16` 指针（`-DHAVE_RVV=1` 时生效，`#if HAVE_RVV`）。
`cmake/pkg-build.sh` 增加 `EXTRA_C_FLAGS` 入口，A/B 两侧只差这一条宏。

## 2. 功能验证（qemu, VLEN=128）

| 配置 | 输出 |
|---|---|
| off（C） | `dec frames=10 bytes=61440 fnv_px=cf67fb9981835c6a` |
| on（RVV 全尺寸） | `dec frames=10 bytes=61440 fnv_px=cf67fb9981835c6a` |

与 G2 基线（`cf67fb9981835c6a`）逐字节一致。`nm` 核对：on 侧有
`add_residual_8_rvv` / `add_residual_16_rvv`，off 侧 0 个 rvv 符号。

## 3. 内核级真机（200k 次/档，3 次取最优）

| nT | C | RVV | 比值 |
|---|---|---|---|
| 4 | 49.4–53.2 ns | 57.7–58.1 ns | **0.86–0.92×** |
| 8 | 176.1–180.0 | 93.1–93.2 | **1.89–1.93×** |
| 16 | 626.9–727.2 | 186.3–186.7 | **3.37–3.90×** |
| 32 | 2584.3–2701.6 | 759.5–767.3 | **3.37–3.56×** |

4x4 为负：16 个像素进不了向量，`vsetvlmax` + 函数调用 + 两次收窄的开销吃光算术量
（与 openh264 SAD 的"宽度决定胜负"同一结论）。

## 4. 整链（10 帧 64x64，同进程 300 轮解码，5 轮交替每轮 3 次取最优）

| 侧 | 配置 | total_us | 相对 off |
|---|---|---|---|
| off | 全 C | 806.2–807.4 k | 1.000 |
| on | 4/8/16/32 全走 RVV | 813.3–814.0 k | **0.991×（慢 0.9%）** |
| on2 | 仅 ≥8x8 走 RVV | 807.4–808.3 k | **0.999×（无收益）** |

三套配置 5 轮同向、波动 <0.2%。

## 5. 结论与归因

1. **4x4 负优化是主因**：关掉 4x4 后从慢 0.9% 回到持平 —— 与 openh264 的收窄方向一致。
2. **但收窄后仍无收益**：说明 `add_residual` 在解码总时间里占比极小（估 <1%），
   内核 3.4× 摊不进整链。**它的"每块必调"不等于"占比高"**，这是本轮最重要的教训。
3. 另一个隐藏成本：C 侧被 gcc 内联进 `transform_coefficients` 的循环里，
   RVV 侧必须走 `acceleration` 函数指针 → 每块多一次间接调用，块越小越吃亏。

**判定：本内核不升 G3。** 代码保留在 `-DHAVE_RVV` 下（默认不编），
`RVV_SEL_BIG=1` 保留"≥8x8 才走 RVV"的派发。
下一轮应换**单块算术量大**的内核（IDCT 8x8/16x16、deblocking、qpel MC），
不再是纯 elementwise。
