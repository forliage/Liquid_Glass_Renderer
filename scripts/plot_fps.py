#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_json(path: Path):
    with path.open() as handle:
        return json.load(handle)


def save_figure(fig, path: Path, saved_paths: list[str]):
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)
    saved_paths.append(str(path))


def plot_benchmark(data: dict, prefix: Path, saved_paths: list[str]):
    frames = [frame for frame in data.get("frames", []) if not frame.get("warmup", False)]
    if not frames:
        return

    indices = [frame.get("index", i) for i, frame in enumerate(frames)]
    cpu_ms = [frame.get("cpu_ms", 0.0) for frame in frames]
    gpu_ms = [frame.get("gpu_ms", 0.0) for frame in frames]
    fps = [1000.0 / value if value > 0.0 else 0.0 for value in cpu_ms]

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.plot(indices, fps, marker="o", linewidth=1.5)
    ax.set_title("FPS vs Frame")
    ax.set_xlabel("Frame")
    ax.set_ylabel("FPS")
    save_figure(fig, prefix.with_name(prefix.name + "_fps.png"), saved_paths)

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.plot(indices, cpu_ms, marker="o", label="CPU ms")
    ax.plot(indices, gpu_ms, marker="s", label="GPU ms")
    ax.set_title("Frame Time")
    ax.set_xlabel("Frame")
    ax.set_ylabel("Milliseconds")
    ax.legend()
    save_figure(fig, prefix.with_name(prefix.name + "_frame_time.png"), saved_paths)

    passes = data.get("passes", [])
    if passes:
        fig, ax = plt.subplots(figsize=(10, 4))
        ax.bar(
            [entry["name"] for entry in passes],
            [entry.get("avg_gpu_ms", 0.0) for entry in passes],
            color="#3a6ea5",
        )
        ax.set_title("GPU Pass Breakdown")
        ax.set_xlabel("Pass")
        ax.set_ylabel("Avg GPU ms")
        ax.tick_params(axis="x", rotation=45)
        save_figure(fig, prefix.with_name(prefix.name + "_breakdown.png"), saved_paths)


def record_list(data: dict):
    return data.get("records") or data.get("experiments") or []


def record_label(record: dict):
    label = record.get("label") or Path(record.get("benchmark_path", "record")).stem
    variant = record.get("ablation_variant", "")
    if variant and variant != "standard":
        return f"{label}:{variant}"
    mode = record.get("performance_mode", "")
    return f"{label}:{mode}" if mode else label


def plot_summary(data: dict, prefix: Path, saved_paths: list[str]):
    records = record_list(data)
    if not records:
        return

    labels = [record_label(record) for record in records]
    fps_values = [record.get("avg_fps", 0.0) for record in records]
    cpu_ms = [record.get("avg_cpu_ms", 0.0) for record in records]
    gpu_ms = [record.get("avg_gpu_ms", 0.0) for record in records]
    contrast = [record.get("contrast_preservation", 0.0) for record in records]
    tradeoff = [record.get("quality_performance_score", 0.0) for record in records]

    fig, ax = plt.subplots(figsize=(10, 4))
    ax.bar(labels, fps_values, color="#2c7a7b")
    ax.set_title("FPS vs Render Mode")
    ax.set_xlabel("Experiment")
    ax.set_ylabel("FPS")
    ax.tick_params(axis="x", rotation=30)
    save_figure(fig, prefix.with_name(prefix.name + "_fps.png"), saved_paths)

    x = range(len(labels))
    fig, ax = plt.subplots(figsize=(10, 4))
    ax.bar([value - 0.2 for value in x], cpu_ms, width=0.4, label="CPU ms", color="#dd6b20")
    ax.bar([value + 0.2 for value in x], gpu_ms, width=0.4, label="GPU ms", color="#3182ce")
    ax.set_title("Frame Time")
    ax.set_xlabel("Experiment")
    ax.set_ylabel("Milliseconds")
    ax.set_xticks(list(x), labels, rotation=30)
    ax.legend()
    save_figure(fig, prefix.with_name(prefix.name + "_frame_time.png"), saved_paths)

    pass_names = sorted({entry["name"] for record in records for entry in record.get("passes", [])})
    if pass_names:
        fig, ax = plt.subplots(figsize=(11, 5))
        bottoms = [0.0] * len(records)
        cmap = plt.get_cmap("tab20")
        for idx, pass_name in enumerate(pass_names):
            values = []
            for record in records:
                match = next((entry for entry in record.get("passes", []) if entry["name"] == pass_name), None)
                values.append(match.get("avg_gpu_ms", 0.0) if match else 0.0)
            ax.bar(labels, values, bottom=bottoms, label=pass_name, color=cmap(idx % 20))
            bottoms = [bottom + value for bottom, value in zip(bottoms, values)]
        ax.set_title("Frame Time Breakdown")
        ax.set_xlabel("Experiment")
        ax.set_ylabel("Avg GPU ms")
        ax.tick_params(axis="x", rotation=30)
        ax.legend(ncols=2, fontsize=8)
        save_figure(fig, prefix.with_name(prefix.name + "_breakdown.png"), saved_paths)

    fig, ax = plt.subplots(figsize=(8, 5))
    y_values = contrast
    y_label = "Contrast Preservation"
    if not any(value > 0.0 for value in y_values):
        y_values = [1.0 - record.get("fold_over_mean", 0.0) for record in records]
        y_label = "1 - Fold-over Mean"
    ax.scatter(gpu_ms, y_values, s=80, color="#805ad5")
    for x_value, y_value, label in zip(gpu_ms, y_values, labels):
        ax.annotate(label, (x_value, y_value), textcoords="offset points", xytext=(5, 4), fontsize=8)
    ax.set_title("Quality-Performance Tradeoff")
    ax.set_xlabel("Avg GPU ms")
    ax.set_ylabel(y_label)
    save_figure(fig, prefix.with_name(prefix.name + "_tradeoff.png"), saved_paths)

    best_index = max(range(len(records)), key=lambda idx: tradeoff[idx]) if any(tradeoff) else 0
    group_key = records[best_index].get("group_key")
    group_records = [record for record in records if record.get("group_key") == group_key] or records[:3]
    original = Path(group_records[0].get("input_path", ""))
    montage_paths = [original] + [Path(record.get("output_artifact", "")) for record in group_records]
    montage_images = []
    montage_titles = []
    for idx, image_path in enumerate(montage_paths):
        if not image_path or not image_path.exists():
            continue
        montage_images.append(plt.imread(image_path))
        montage_titles.append("Original" if idx == 0 else record_label(group_records[idx - 1]))
    if montage_images:
        fig, axes = plt.subplots(1, len(montage_images), figsize=(4 * len(montage_images), 4))
        if len(montage_images) == 1:
            axes = [axes]
        for axis, image, title in zip(axes, montage_images, montage_titles):
            axis.imshow(image)
            axis.set_title(title)
            axis.axis("off")
        save_figure(fig, prefix.with_name(prefix.name + "_montage.png"), saved_paths)


def main():
    parser = argparse.ArgumentParser(description="Plot Liquid Glass benchmark or ablation figures.")
    parser.add_argument("input_json", help="benchmark json or ablation summary json")
    parser.add_argument("output_prefix", nargs="?", help="output prefix for generated figures")
    args = parser.parse_args()

    input_path = Path(args.input_json)
    prefix = Path(args.output_prefix) if args.output_prefix else input_path.with_suffix("")
    data = load_json(input_path)

    saved_paths: list[str] = []
    if "frames" in data and "avg_fps" in data:
        plot_benchmark(data, prefix, saved_paths)
    else:
        plot_summary(data, prefix, saved_paths)

    for path in saved_paths:
        print(path)


if __name__ == "__main__":
    main()
