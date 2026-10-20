# 硬件文件

本仓库 **不包含** 硬件 CAD（Altium 原理图 / PCB / Gerber / BOM）和结构文件。

原因：

- 原始工程里的 `altium designer/` 有多个历史版本、History、Project Logs 和厂商库，体积接近 9 GB，不适合直接公开；
- 部分库文件和封装可能来自第三方，需要逐个确认再分发许可；
- 硬件开源是独立决策，建议单独选择许可证（如 CERN-OHL-S-2.0 / CERN-OHL-W-2.0），而不是跟随代码的 Apache-2.0。

如果你决定开源硬件，建议只放 **最终量产版本**：

```text
hardware/
├── schematic/        # SchDoc / PDF 原理图
├── pcb/              # PcbDoc / Gerber
├── bom/              # BOM（含替代料说明）
├── mechanical/       # 外壳 / 3D 模型
└── README.md         # 版本、板号、变更记录
```

不要提交：

- Altium `History/`、`Project Logs/`、`.PrjPcbStructure` 的临时文件；
- Altium 安装目录或厂商库；
- 芯片数据手册、蓝牙 Assigned Numbers 等版权 PDF（改为链接）；
- 含个人信息的采购单、快递单、序列号表。
