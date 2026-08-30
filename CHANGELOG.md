# 更新日志

本项目的显著变更记录。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [1.0.0] - 2026-08-30

### 新增
- 算法层主机端单元测试（`tests/`）：合成 PPG/加速度信号验证心率、血氧、计步、
  温度补偿，13 项断言；`make -C tests` 一键运行，不依赖硬件
- GitHub Actions CI：push / PR 自动编译并运行单元测试
- MIT License
- 统一驱动错误码 `crouse.h/errors.h`（ERR_IO / ERR_TIMEOUT / ERR_CRC /
  ERR_PARAM / ERR_NO_DATA），各驱动返回值改用命名常量
- 空闲任务 WFI 休眠（`vApplicationIdleHook`），降低空闲期功耗
- README 重写：同步 6 页界面 / 6 条语音指令 / 蓝牙单向广播等当前实现，
  补充软件架构、开发与测试、文档索引章节
- docs/ 文档索引 README

### 修复 / 变更
- 语音翻页改为 FreeRTOS 队列解耦：语音任务只入队页号，显示任务取队列跳页，
  消除语音画的屏幕被 500ms 周期刷新覆盖的问题
- 温度补偿基础温差 TEMP_BASE_OFFSET 实测回调 3.0 → 1.5
- 移除计步算法中的死变量 settle_cnt

### 工程整理
- 清理 AI 工具配置与中间产物（.claude / .codex / AGENTS.md / 解包目录等），
  更新 .gitignore
- 文档与图片从仓库根目录归档至 docs/
- 移除误提交的 node_modules

## 历史版本（未打 tag，按提交概要）

- **e7d24c3** Fix: 全工程代码审查修复 — CRC8/协议恢复/状态简化/竞态与溢出防护
- **a16fb2c** Simplify: 全文件代码精简 — PPG 统计合并/语音命令重编/翻页6页
- **c53ab4e** Simplify: 精简 DS1302 驱动 — 合并 Init/自检
- **f314a41** Simplify: 精简按键/I2C/蓝牙代码 + 移除动态校准与蓝牙指令
- **8b6fffc** Fix: UART2 RX 改上拉输入防噪声误触发
- **f5caf4b** 优化: 实机调参+信号抗扰+步态连续性+OLED布局修正
- **12bee81** 优化: PPG心率/血氧稳定性 + 步数滞回检测 + DS18B20余量
- **7c41499** 优化: 体温补偿 + OLED 全新居中大字体界面
