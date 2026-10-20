# 硬件工程：ESP32-S3 Smart Wear

本目录是 **`版本19_four`** 的 Altium Designer 源文件，对应 ESP32-S3 可穿戴板（POYU）。

## 目录

```text
hardware/
├── LICENSE                     # CERN-OHL-W-2.0
├── README.md
├── altium/POYU/
│   ├── poyu.PRJPCB             # Altium 工程
│   ├── poyu.SchDoc             # 原理图
│   ├── poyu.pcbdoc             # PCB
│   ├── poyu.PcbLib             # 工程内封装库
│   └── poyu.PRJPCBStructure
└── lib/
    ├── 0402/                   # 工程引用的电阻/电容封装
    └── SC7A22H/                # 加速度计封装与符号
```

## 打开

- 需要 **Altium Designer 19 或更新版本**（工程路径来自 AD19）。
- 打开 `altium/POYU/poyu.PRJPCB`，先 Compile Project，再打开 SchDoc / PcbDoc。
- `lib/` 里只放了工程直接引用的两个外部库；ESP32-S3 模组、ICM-20948、Type-C 等其他库没有随仓库分发。PCB 封装已嵌入 `poyu.pcbdoc`，原理图符号多数也已嵌入，缺库主要影响重新放置或编辑元件。
- 3D 模型的原绝对路径已脱敏；打开时可能提示找不到模型，重新链接即可，不影响电气设计。
- 仓库不包含 Gerber、BOM、坐标文件、装配图，请在 Altium 中自行生成。

## 版本与验证

- 版本：`19_four`
- 原理图最后修改：2026-02-07
- PCB 最后修改：2025-12-14（早于原理图；打样前请重新编译、核对网络并跑 DRC）
- 没有做电气、EMC、安规或量产验证，仅供参考。

## 许可证

- **硬件设计文件：CERN-OHL-W-2.0**，全文见 [LICENSE](LICENSE)。
- 固件与训练代码：Apache-2.0，见仓库根目录 `LICENSE`。
- `lib/` 中的封装/符号如来自第三方，请再分发前确认其来源与许可。

## 贡献

欢迎提交封装修正、BOM、Gerber 或打样反馈；请勿提交 Altium `History/`、`Project Logs/`、厂商 datasheet 或带个人路径的文件。
