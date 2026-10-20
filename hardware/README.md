# 硬件工程：ESP32-S3 Smart Wear

本目录是 **`版本18_four`** 的 Altium Designer 源文件，对应 ESP32-S3 可穿戴板（POYU）。

## 实物与版图

<table>
<tr>
<td align="center"><img src="images/board-front.jpg" width="340" alt="实物正面"><br>实物正面</td>
<td align="center"><img src="images/board-back.jpg" width="340" alt="实物背面"><br>实物背面</td>
</tr>
<tr>
<td align="center"><img src="images/pcb-front.jpg" width="340" alt="PCB 顶层"><br>PCB 顶层（元件面）</td>
<td align="center"><img src="images/pcb-back.png" width="340" alt="PCB 底层"><br>PCB 底层（SD 卡座面）</td>
</tr>
</table>

> 实物照片为装在橙色外壳内的测试板；PCB 版图由 Altium 导出。板上有 ESP32-S3-WROOM-1、USB-C、microSD 卡座、IMU、气压计、电量计等，具体以原理图/PCB 为准。

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
├── enclosure/
│   ├── stl/                    # v6 上/下壳 STL
│   └── solidworks/             # v8 装配体与零件
└── lib/
    ├── 0402/                   # 工程引用的电阻/电容封装
    └── SC7A22H/                # 加速度计封装与符号
```

## 外壳

- `enclosure/stl/`：v6 上壳、下壳，可直接切片打印；
- `enclosure/solidworks/`：v8 装配体、上下壳零件和整机外形参考；
- 版本差异、打印建议见 [enclosure/README.md](enclosure/README.md)。

## 打开

- 需要 **Altium Designer 19 或更新版本**（工程路径来自 AD19）。
- 打开 `altium/POYU/poyu.PRJPCB`，先 Compile Project，再打开 SchDoc / PcbDoc。
- `lib/` 里只放了工程直接引用的两个外部库；ESP32-S3 模组、ICM-20948、Type-C 等其他库没有随仓库分发。PCB 封装已嵌入 `poyu.pcbdoc`，原理图符号多数也已嵌入，缺库主要影响重新放置或编辑元件。
- 3D 模型的原绝对路径已脱敏；打开时可能提示找不到模型，重新链接即可，不影响电气设计。
- 仓库不包含 Gerber、BOM、坐标文件、装配图，请在 Altium 中自行生成。

## 版本与验证

- 版本：`18_four`
- 原理图最后修改：2026-01-07
- PCB 最后修改：2026-01-06
- 没有做电气、EMC、安规或量产验证，仅供参考。

## 许可证

- **硬件设计文件：CERN-OHL-W-2.0**，全文见 [LICENSE](LICENSE)。
- 固件与训练代码：Apache-2.0，见仓库根目录 `LICENSE`。
- `lib/` 中的封装/符号如来自第三方，请再分发前确认其来源与许可。

## 贡献

欢迎提交封装修正、BOM、Gerber 或打样反馈；请勿提交 Altium `History/`、`Project Logs/`、厂商 datasheet 或带个人路径的文件。
