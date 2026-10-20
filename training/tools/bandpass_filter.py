#!/usr/bin/env python3
"""对 CSV 中的某一列做零相位 Butterworth 带通滤波，输出新 CSV。

用法：
    python tools/bandpass_filter.py data.csv --column accel_mag --fs 104 --low 0.1 --high 20
    python tools/bandpass_filter.py data.csv --column accel_mag --timestamp timestamp --low 0.5 --high 15
"""
import argparse
import csv
import os


def infer_fs(rows, column):
    values = []
    for row in rows:
        try:
            values.append(float(row[column]))
        except (KeyError, TypeError, ValueError):
            continue
    if len(values) < 3:
        return None
    diffs = [b - a for a, b in zip(values, values[1:]) if b > a]
    if not diffs:
        return None
    diffs.sort()
    step = diffs[len(diffs) // 2]
    if step <= 0:
        return None
    # 常见时间戳单位：秒 / 毫秒 / 微秒
    if step > 1000:
        return 1.0 / (step / 1e6)
    if step > 1:
        return 1.0 / (step / 1e3)
    return 1.0 / step


def main():
    parser = argparse.ArgumentParser(description="CSV 单列带通滤波")
    parser.add_argument("input", help="输入 CSV")
    parser.add_argument("--column", required=True, help="要滤波的列名")
    parser.add_argument("--out", default=None, help="输出 CSV，默认 <输入名>_filtered.csv")
    parser.add_argument("--fs", type=float, default=None, help="采样率 Hz；不给则尝试从 --timestamp 推断")
    parser.add_argument("--timestamp", default=None, help="时间戳列名，用于推断采样率")
    parser.add_argument("--low", type=float, default=0.1, help="低截止频率 Hz")
    parser.add_argument("--high", type=float, default=20.0, help="高截止频率 Hz")
    parser.add_argument("--order", type=int, default=4, help="Butterworth 阶数")
    args = parser.parse_args()

    try:
        import numpy as np
        from scipy.signal import butter, filtfilt
    except ImportError as error:
        raise SystemExit("需要 numpy 和 scipy：pip install numpy scipy") from error

    with open(args.input, "r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if args.column not in (reader.fieldnames or []):
            raise SystemExit(f"找不到列 {args.column}，现有列：{reader.fieldnames}")
        rows = list(reader)

    if not rows:
        raise SystemExit("CSV 没有数据行")

    series = []
    for index, row in enumerate(rows):
        try:
            series.append(float(row[args.column]))
        except (TypeError, ValueError):
            raise SystemExit(f"第 {index + 2} 行的 {args.column} 不是数字：{row.get(args.column)!r}")

    fs = args.fs
    if fs is None and args.timestamp:
        fs = infer_fs(rows, args.timestamp)
    if fs is None:
        fs = 104.0
        print("未提供 --fs 且无法从时间戳推断，按 104 Hz 处理")

    if not (0 < args.low < args.high < fs / 2):
        raise SystemExit(f"频率不合法：需要 0 < low < high < fs/2（fs={fs}）")

    nyquist = fs / 2
    b, a = butter(args.order, [args.low / nyquist, args.high / nyquist], btype="band")
    minimum = 3 * max(len(a), len(b))
    if len(series) <= minimum:
        raise SystemExit(f"数据太短：{len(series)} 点，至少需要 {minimum + 1} 点")

    filtered = filtfilt(b, a, np.asarray(series, dtype=float))

    out = args.out or os.path.splitext(args.input)[0] + "_filtered.csv"
    fieldnames = list(rows[0].keys())
    extra = f"{args.column}_filtered"
    if extra not in fieldnames:
        fieldnames.append(extra)
    with open(out, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row, value in zip(rows, filtered):
            row[extra] = f"{float(value):.6f}"
            writer.writerow(row)

    print(f"{len(rows)} 行，fs={fs:g} Hz，带通 {args.low:g}-{args.high:g} Hz -> {out}")


if __name__ == "__main__":
    main()
