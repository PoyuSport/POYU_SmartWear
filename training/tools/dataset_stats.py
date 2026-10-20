#!/usr/bin/env python3
"""扫描训练用 CSV，检查列、行数和标签分布，便于先筛数据再训练。

用法：
    python tools/dataset_stats.py /path/to/train_data
    python tools/dataset_stats.py /path/to/train_data --json stats.json
"""
import argparse
import csv
import json
import os
from collections import Counter

FEATURES = [
    "roll", "pitch",
    "accel_x", "accel_y", "accel_z",
    "gyro_x", "gyro_y", "gyro_z",
    "vert_line_accel", "accel_mag",
    "hor_accel", "vert_percent",
]
LABELS = {0: "其他", 1: "步行", 2: "骑行", 3: "跑步"}


def label_from_path(path):
    upper = path.upper()
    if "WALK" in upper:
        return 1
    if "BYCLE" in upper or "CYCLE" in upper:
        return 2
    if "RUN" in upper:
        return 3
    return 0


def normalize_header(header):
    """允许表头被截断，例如 vert_perce -> vert_percent。"""
    clean = {str(name).strip(): name for name in header}
    mapping = {}
    for feature in FEATURES:
        if feature in clean:
            mapping[feature] = feature
            continue
        prefix = feature[:8]
        match = next((name for name in clean if name.startswith(prefix)), None)
        if match:
            mapping[feature] = match
    return mapping


def scan_file(path):
    with open(path, "r", encoding="utf-8-sig", errors="replace", newline="") as handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration:
            return {"path": path, "rows": 0, "missing": FEATURES, "labels": {}, "error": "空文件"}

        mapping = normalize_header(header)
        missing = [name for name in FEATURES if name not in mapping]
        column_index = {name: header.index(mapping[name]) for name in mapping}
        label_index = None
        for name in ("Train_Label", "Train_Labe"):
            if name in header:
                label_index = header.index(name)
                break

        labels = Counter()
        rows = 0
        try:
            for row in reader:
                if not row:
                    continue
                rows += 1
                if label_index is not None and label_index < len(row):
                    value = row[label_index].strip()
                    labels[value or "空"] += 1
        except csv.Error as error:
            return {"path": path, "rows": rows, "missing": missing, "labels": dict(labels), "error": str(error)}

    if not labels:
        labels[str(label_from_path(path))] = rows
    return {"path": path, "rows": rows, "missing": missing, "labels": dict(labels), "error": None}


def main():
    parser = argparse.ArgumentParser(description="训练 CSV 数据检查")
    parser.add_argument("root", help="数据根目录（递归扫描 .csv）")
    parser.add_argument("--json", default=None, help="把结果写入 JSON")
    args = parser.parse_args()

    files = []
    for dirpath, _, filenames in os.walk(args.root):
        for name in filenames:
            if name.lower().endswith(".csv"):
                files.append(os.path.join(dirpath, name))
    files.sort()

    if not files:
        raise SystemExit(f"{args.root} 下没有 CSV")

    results = [scan_file(path) for path in files]
    total_rows = sum(item["rows"] for item in results)
    total_labels = Counter()
    for item in results:
        for label, count in item["labels"].items():
            total_labels[label] += count

    print(f"文件: {len(results)}  行数: {total_rows}")
    print("标签分布:")
    for label, count in sorted(total_labels.items(), key=lambda item: str(item[0])):
        name = LABELS.get(int(label), "未知") if str(label).isdigit() else "未知"
        print(f"  {label} ({name}): {count}")

    bad = [item for item in results if item["missing"] or item["error"] or item["rows"] == 0]
    if bad:
        print(f"\n需要检查的文件: {len(bad)}")
        for item in bad[:20]:
            reason = item["error"] or f"缺少列 {item['missing']}" or "0 行"
            print(f"  {item['path']}: {reason}")
    else:
        print("\n所有文件的必需列和行数正常")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({"root": args.root, "total_rows": total_rows,
                       "labels": dict(total_labels), "files": results},
                      handle, ensure_ascii=False, indent=2)
        print(f"已写入 {args.json}")


if __name__ == "__main__":
    main()
