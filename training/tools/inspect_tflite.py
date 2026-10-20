#!/usr/bin/env python3
"""查看 TFLite 模型的输入/输出和量化参数。

用法：
    python tools/inspect_tflite.py
    python tools/inspect_tflite.py models/model_12feat.tflite
    python tools/inspect_tflite.py --ops
"""
import argparse
from pathlib import Path

DEFAULT_MODEL = Path(__file__).resolve().parents[2] / "models" / "model_12feat.tflite"


def main():
    parser = argparse.ArgumentParser(description="TFLite 模型信息")
    parser.add_argument("model", nargs="?", default=str(DEFAULT_MODEL))
    parser.add_argument("--ops", action="store_true", help="打印算子/张量分析")
    args = parser.parse_args()

    try:
        import tensorflow as tf
    except ImportError as error:
        raise SystemExit("需要 TensorFlow：pip install tensorflow") from error

    path = Path(args.model)
    if not path.exists():
        raise SystemExit(f"模型不存在：{path}")

    interpreter = tf.lite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()

    print(f"model: {path}  ({path.stat().st_size} bytes)")
    for title, details in (("input", interpreter.get_input_details()),
                           ("output", interpreter.get_output_details())):
        print(f"\n{title}:")
        for item in details:
            print(f"  name={item['name']}")
            print(f"  shape={item['shape'].tolist()} dtype={item['dtype'].__name__}")
            print(f"  quantization={item['quantization']}")

    if args.ops:
        print("\n--- analyzer ---")
        tf.lite.experimental.Analyzer.analyze(model_path=str(path))


if __name__ == "__main__":
    main()
