# 进度 —— `media-libs/libde265`

倒序追加，每次一个条目。门禁变更必须同时更新 `project.conf` 的 `GATE`，
否则主仓库看板会与事实脱节。

## 2026-11-13 · G0 planned

- 生成骨架（`rvv-port-project` Skill，模板来自 `libspeex-rvv`）
- 范围与通过标准写入 `docs/plan.md`
- 上游 URL：https://github.com/strukturag/libde265；固定点：`4fb298ec1af0686211dc2def73f4f41c2ce19ce5`
- 下一步（G1）：
  1. `./scripts/fetch-upstream.sh --dry-run` 确认 URL/SHA 后再拉取
  2. `src/` 初始化为 upstream 的副本（一次性提交，便于 diff）
  3. `./scripts/build.sh riscv64` + `./test/run_ab.sh` 跑通

## 2026-11-15 G3 内核尝试：add_residual（负结论）
- 内核 `src/libde265/riscv/add_residual_rvv.c`，接线 `fallback.cc`（`HAVE_RVV`），
  `cmake/pkg-build.sh` 增 `EXTRA_C_FLAGS`；`scripts/g3-wire-ab.sh` 双配置。
- 功能：qemu 两侧 `fnv_px=cf67fb9981835c6a`，与 G2 一致。
- 内核：4x4 0.86–0.92×、8x8 1.9×、16x16 3.4–3.9×、32x32 3.4–3.6×。
- 整链：全尺寸 0.991×；仅 ≥8x8 0.999× → 无收益，**不升 G3**。
- 归因：add_residual 占解码时间 <1%；C 侧被内联、RVV 侧走函数指针，每块多一次调用。
- 详见 `docs/evidence/G3-kernel-addresidual-20261115.md`。
