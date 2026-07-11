#!/usr/bin/env python3
"""Extract MassBattleFrame fog/minimap performance curves from UE logs.

Example:
    python Scripts/AnalyzeMassBattleFrameFogPerf.py \
        --log D:/UE5Project/Winyunq/Saved/Logs/Winyunq.log

The script writes a CSV, a PNG curve, and a short text summary.  Fog CPU
records and minimap GPU records are kept as separate series; minimap GPU
numbers are never relabeled as scene-fog GPU timings.
"""

from __future__ import annotations

import argparse
import csv
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import mean

import matplotlib.pyplot as plt


FOG_RE = re.compile(
    r"LogMassBattleFrameFog: \[FogOfWarPerf\]\[MassBattleFrameFog\] "
    r"ParameterPush=(?P<push>[0-9.]+)ms ArrayUpload=(?P<upload>[0-9.]+)ms "
    r"Sources=(?P<count>\d+) Batches=(?P<batches>\d+) SceneGPU=(?P<scene_gpu>\w+)"
)
MINIMAP_GT_RE = re.compile(
    r"MassBattleMinimapPerf GT: Agents=(?P<count>\d+) Batches=(?P<batches>\d+) "
    r"BulkMergeAndSchedule=(?P<merge>[0-9.]+)ms UploadBytes=(?P<bytes>\d+)"
)
MINIMAP_RT_RE = re.compile(
    r"MassBattleMinimapPerf RT: Agents=(?P<count>\d+) "
    r"BufferCreateAndUpload=(?P<upload>[0-9.]+)ms"
)
MINIMAP_GPU_RE = re.compile(
    r"MassBattleMinimapPerf GPU: Agents=(?P<count>\d+) Samples=(?P<samples>\d+) "
    r"UnitsAvg=(?P<units>[0-9.]+)ms VisionAvg=(?P<vision>[0-9.]+)ms "
    r"FogAvg=(?P<fog>[0-9.]+)ms TotalAvg=(?P<total>[0-9.]+)ms "
    r"TotalMin=(?P<min>[0-9.]+)ms TotalMax=(?P<max>[0-9.]+)ms"
)


@dataclass
class FogRecord:
    count: int
    batches: int
    push_ms: float
    upload_ms: float
    scene_gpu: str
    source: str
    line: int


@dataclass
class MinimapGpuRecord:
    count: int
    samples: int
    units_ms: float
    vision_ms: float
    fog_ms: float
    total_ms: float
    total_min_ms: float
    total_max_ms: float
    source: str
    line: int


def parse_logs(paths: list[Path]) -> tuple[list[FogRecord], list[MinimapGpuRecord]]:
    fog: list[FogRecord] = []
    minimap_gpu: list[MinimapGpuRecord] = []
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_number, line in enumerate(handle, 1):
                match = FOG_RE.search(line)
                if match:
                    fog.append(
                        FogRecord(
                            count=int(match["count"]),
                            batches=int(match["batches"]),
                            push_ms=float(match["push"]),
                            upload_ms=float(match["upload"]),
                            scene_gpu=match["scene_gpu"],
                            source=str(path),
                            line=line_number,
                        )
                    )
                match = MINIMAP_GPU_RE.search(line)
                if match:
                    minimap_gpu.append(
                        MinimapGpuRecord(
                            count=int(match["count"]),
                            samples=int(match["samples"]),
                            units_ms=float(match["units"]),
                            vision_ms=float(match["vision"]),
                            fog_ms=float(match["fog"]),
                            total_ms=float(match["total"]),
                            total_min_ms=float(match["min"]),
                            total_max_ms=float(match["max"]),
                            source=str(path),
                            line=line_number,
                        )
                    )
    return fog, minimap_gpu


def grouped(records, value_name: str):
    values = defaultdict(list)
    for record in records:
        values[record.count].append(getattr(record, value_name))
    return {
        count: (mean(items), min(items), max(items), len(items))
        for count, items in sorted(values.items())
    }


def slope(records: list[FogRecord], value_name: str) -> float | None:
    if len(records) < 2:
        return None
    xs = [record.count for record in records]
    ys = [getattr(record, value_name) for record in records]
    x_mean = mean(xs)
    y_mean = mean(ys)
    denominator = sum((x - x_mean) ** 2 for x in xs)
    if denominator == 0:
        return None
    return sum((x - x_mean) * (y - y_mean) for x, y in zip(xs, ys)) / denominator


def write_csv(path: Path, fog: list[FogRecord], minimap_gpu: list[MinimapGpuRecord]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "kind",
                "count",
                "batches",
                "push_ms",
                "array_upload_ms",
                "scene_gpu",
                "samples",
                "units_ms",
                "vision_ms",
                "fog_ms",
                "total_ms",
                "total_min_ms",
                "total_max_ms",
                "source",
                "line",
            ]
        )
        for record in fog:
            writer.writerow(
                [
                    "scene_fog_cpu",
                    record.count,
                    record.batches,
                    record.push_ms,
                    record.upload_ms,
                    record.scene_gpu,
                    "",
                    "",
                    "",
                    "",
                    "",
                    "",
                    "",
                    record.source,
                    record.line,
                ]
            )
        for record in minimap_gpu:
            writer.writerow(
                [
                    "minimap_gpu_reference",
                    record.count,
                    "",
                    "",
                    "",
                    "",
                    record.samples,
                    record.units_ms,
                    record.vision_ms,
                    record.fog_ms,
                    record.total_ms,
                    record.total_min_ms,
                    record.total_max_ms,
                    record.source,
                    record.line,
                ]
            )


def write_plot(path: Path, fog: list[FogRecord], minimap_gpu: list[MinimapGpuRecord]) -> None:
    fog_push = grouped(fog, "push_ms")
    fog_upload = grouped(fog, "upload_ms")
    gpu_units = grouped(minimap_gpu, "units_ms")
    gpu_vision = grouped(minimap_gpu, "vision_ms")
    gpu_fog = grouped(minimap_gpu, "fog_ms")
    gpu_total = grouped(minimap_gpu, "total_ms")

    has_gpu_curve = len(gpu_total) >= 2
    fig, axes = plt.subplots(
        2 if has_gpu_curve else 1,
        1,
        figsize=(13, 9 if has_gpu_curve else 6),
        sharex=False,
        constrained_layout=True,
    )
    if not isinstance(axes, (list, tuple)):
        try:
            axes = list(axes)
        except TypeError:
            axes = [axes]
    if not has_gpu_curve:
        axes = [axes[0]] if len(axes) > 0 else axes
    fig.suptitle("MassBattleFrame Fog performance curve from UE logs", fontsize=16)

    ax = axes[0]
    for label, grouped_values, color in [
        ("Fog ParameterPush", fog_push, "#2563eb"),
        ("Fog ArrayUpload", fog_upload, "#dc2626"),
    ]:
        if not grouped_values:
            continue
        xs = list(grouped_values)
        averages = [grouped_values[x][0] for x in xs]
        lows = [grouped_values[x][1] for x in xs]
        highs = [grouped_values[x][2] for x in xs]
        ax.plot(xs, averages, marker="o", label=label, color=color)
        ax.fill_between(xs, lows, highs, color=color, alpha=0.12)
    ax.set_title("Scene fog CPU push/upload")
    ax.set_xlabel("Sources / units")
    ax.set_ylabel("Milliseconds")
    ax.grid(True, alpha=0.25)
    ax.legend()

    if has_gpu_curve:
        ax = axes[1]
        for label, grouped_values, color in [
            ("Minimap Units", gpu_units, "#7c3aed"),
            ("Minimap Vision", gpu_vision, "#059669"),
            ("Minimap Fog", gpu_fog, "#d97706"),
            ("Minimap Total", gpu_total, "#111827"),
        ]:
            if not grouped_values:
                continue
            xs = list(grouped_values)
            averages = [grouped_values[x][0] for x in xs]
            lows = [grouped_values[x][1] for x in xs]
            highs = [grouped_values[x][2] for x in xs]
            ax.plot(xs, averages, marker="o", label=label, color=color)
            ax.fill_between(xs, lows, highs, color=color, alpha=0.10)
        ax.set_title("Minimap GPU reference (not scene-fog GPU timing)")
        ax.set_xlabel("Agents / units")
        ax.set_ylabel("Milliseconds")
        ax.grid(True, alpha=0.25)
        ax.legend()

    fig.savefig(path, dpi=160)
    plt.close(fig)


def write_summary(path: Path, fog: list[FogRecord], minimap_gpu: list[MinimapGpuRecord]) -> None:
    counts = sorted({record.count for record in fog})
    lines = [
        "MassBattleFrame fog performance analysis",
        f"Fog records: {len(fog)}",
        f"Minimap GPU reference records: {len(minimap_gpu)}",
        f"Fog source counts: {', '.join(map(str, counts)) or 'none'}",
    ]
    if fog:
        lines.extend(
            [
                f"Fog push average: {mean(record.push_ms for record in fog):.3f} ms",
                f"Fog push maximum: {max(record.push_ms for record in fog):.3f} ms",
                f"Fog array upload average: {mean(record.upload_ms for record in fog):.3f} ms",
                f"Fog array upload maximum: {max(record.upload_ms for record in fog):.3f} ms",
            ]
        )
        push_slope = slope(fog, "push_ms")
        upload_slope = slope(fog, "upload_ms")
        if push_slope is not None:
            lines.append(f"Least-squares Fog push slope: {push_slope * 1000.0:.4f} microseconds/source")
        if upload_slope is not None:
            lines.append(f"Least-squares Fog array slope: {upload_slope * 1000.0:.4f} microseconds/source")
    if minimap_gpu:
        lines.append("Minimap GPU values are reference measurements; scene-fog GPU time requires the named GPU profiler passes.")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, action="append", help="UE log file; repeat for multiple logs")
    parser.add_argument("--output-dir", type=Path, default=None)
    args = parser.parse_args()

    plugin_root = Path(__file__).resolve().parents[1]
    project_root = plugin_root.parents[1]
    logs = args.log or [project_root / "Saved" / "Logs" / "Winyunq.log"]
    logs = [path.resolve() for path in logs if path.exists()]
    if not logs:
        parser.error("no log files found")

    output_dir = (args.output_dir or plugin_root / "Docs" / "Performance").resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    fog, minimap_gpu = parse_logs(logs)
    write_csv(output_dir / "MassBattleFrameFogPerfCurve.csv", fog, minimap_gpu)
    write_plot(output_dir / "MassBattleFrameFogPerfCurve.png", fog, minimap_gpu)
    write_summary(output_dir / "MassBattleFrameFogPerfSummary.txt", fog, minimap_gpu)
    print(f"Parsed {len(fog)} fog records and {len(minimap_gpu)} minimap GPU records")
    print(f"Wrote {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
