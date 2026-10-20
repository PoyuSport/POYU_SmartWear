# 外壳：POYU Pocket Tag

58 × 47 mm 口袋标签外壳，含 SolidWorks 源文件和可直接切片的 STL。

## 文件

| 文件 | 说明 |
|---|---|
| `stl/poyu_pocket_tag_upper_shell_pcb1_post_v6_low_lanyard_ivory_9mm.STL` | 上壳，v6，已导出 STL |
| `stl/poyu_pocket_tag_lower_shell_pcb1_post_v6_low_lanyard_ivory_4mm_2mm_lip.STL` | 下壳，v6，已导出 STL |
| `solidworks/poyu_post_v8_pillow_thin_edge_ivory_debug.SLDASM` | v8 装配体（最新可编辑版本） |
| `solidworks/poyu_pocket_tag_upper_shell_pcb1_post_v8_pillow_thin_edge_ivory_9mm.SLDPRT` | v8 上壳 |
| `solidworks/poyu_pocket_tag_lower_shell_pcb1_post_v8_pillow_thin_edge_ivory_4mm_2mm_lip.SLDPRT` | v8 下壳 |
| `solidworks/poyu_pocket_tag_product_real_58x47x12.SLDPRT` | 整机外形参考 |

> v8 是最新的可编辑版本；v6 是目前唯一带 STL 导出的版本。两者外形可能有差异，打印前请先确认。仓库未包含 v9 的表面参考模型。

## 打印与装配

- STL 可直接切片打印；材料、层高、支撑和公差没有做正式验证。
- 命名中的 `9mm` / `4mm` / `2mm` 分别对应上壳、下壳和唇边尺寸；整机外形约 58 × 47 × 12–13 mm，以模型为准。
- 建议先打印下壳，核对 PCB、电池、microSD 卡座和 USB-C 开口的干涉，再打印上壳。
- 四角为螺丝柱；螺丝规格以模型内孔为准，仓库不提供紧固件清单。
- 没有做跌落、防水、佩戴舒适性验证。

## 许可证

外壳设计文件与 `hardware/` 下其他硬件文件一样，使用 **CERN-OHL-W-2.0**，见 [../LICENSE](../LICENSE)。

## 说明

- 这些文件来自实际打样迭代，不是量产结构设计。
- 如果修改外壳，请保留版本号和变更说明；不要提交 SolidWorks 的临时文件、Toolbox 缓存或个人路径。
