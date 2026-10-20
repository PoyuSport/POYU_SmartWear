#!/usr/bin/env python3
"""把大 CSV 按固定行数拆成多个小文件（流式读写，内存占用低）。

用法：
    python tools/split_csv.py big.csv --rows 50000
    python tools/split_csv.py big.csv --rows 50000 --outdir split/
"""
import argparse
import csv
import os


def split_csv(input_file, rows_per_file=50000, outdir=None):
    if rows_per_file <= 0:
        raise ValueError("--rows 必须是正整数")

    outdir = outdir or os.path.dirname(os.path.abspath(input_file))
    os.makedirs(outdir, exist_ok=True)
    stem, ext = os.path.splitext(os.path.basename(input_file))

    with open(input_file, "r", encoding="utf-8-sig", newline="") as source:
        reader = csv.reader(source)
        try:
            header = next(reader)
        except StopIteration:
            print(f"{input_file}: 空文件，跳过")
            return 0

        chunk_index = 1
        row_count = 0
        total = 0
        writer = None
        out = None
        try:
            for row in reader:
                if row_count % rows_per_file == 0:
                    if out:
                        out.close()
                    path = os.path.join(outdir, f"{stem}_{chunk_index}{ext or '.csv'}")
                    out = open(path, "w", encoding="utf-8", newline="")
                    writer = csv.writer(out)
                    writer.writerow(header)
                    chunk_index += 1
                writer.writerow(row)
                row_count += 1
                total += 1
        finally:
            if out:
                out.close()

    print(f"{input_file}: {total} 行 -> {chunk_index - 1} 个文件，输出目录 {outdir}")
    return chunk_index - 1


def main():
    parser = argparse.ArgumentParser(description="按行数拆分 CSV")
    parser.add_argument("input", help="输入 CSV")
    parser.add_argument("--rows", type=int, default=50000, help="每个文件最多行数（不含表头）")
    parser.add_argument("--outdir", default=None, help="输出目录，默认与输入同目录")
    args = parser.parse_args()
    split_csv(args.input, args.rows, args.outdir)


if __name__ == "__main__":
    main()
