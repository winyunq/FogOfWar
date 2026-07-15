#!/usr/bin/env python3
"""Print the latest MassBattleFrame scene-fog performance records.

Run from any directory:

    python ReportLatestMassBattleFrameFogPerf.py

The default log is the newest *.log file under the project's Saved/Logs
directory.  Pass --log when the project is elsewhere.

The reported scene-fog total is deliberately explicit:

    serialized total = CPU ParameterPush + GPU VisionMask + GPU Composite

ParameterPush already contains the ArrayUpload work, so ArrayUpload is shown
as a diagnostic sub-timing and is not added a second time.  CPU and GPU can
overlap; therefore serialized total is an upper-bound accounting value, not a
claim about wall-clock frame time.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from statistics import mean
from typing import Iterable, Optional


NUMBER = r"[-+]?\d+(?:\.\d+)?"

CPU_RE = re.compile(
    rf"\[FogOfWarPerf\]\[MassBattleFrameFog\].*?"
    rf"ParameterPush=(?P<push>{NUMBER})ms.*?"
    rf"ArrayUpload=(?P<upload>{NUMBER})ms.*?"
    rf"Sources=(?P<sources>\d+).*?"
    rf"Batches=(?P<batches>\d+).*?"
    rf"SceneGPU=(?P<scene_gpu>\w+)",
    re.IGNORECASE,
)

GPU_RE = re.compile(
    rf"\[FogOfWarPerf\]\[MassBattleFrameFogGPU\].*?"
    rf"Sources=(?P<sources>\d+).*?"
    rf"Samples=(?P<samples>\d+).*?"
    rf"VisionMask=(?P<vision>{NUMBER})ms.*?"
    rf"Composite=(?P<composite>{NUMBER})ms.*?"
    rf"Total=(?P<total>{NUMBER})ms.*?"
    rf"TotalMin=(?P<minimum>{NUMBER})ms.*?"
    rf"TotalMax=(?P<maximum>{NUMBER})ms",
    re.IGNORECASE,
)

MINIMAP_RE = re.compile(
    rf"MassBattleMinimapPerf GPU:.*?"
    rf"Agents=(?P<agents>\d+).*?"
    rf"Samples=(?P<samples>\d+).*?"
    rf"UnitsAvg=(?P<units>{NUMBER})ms.*?"
    rf"VisionAvg=(?P<vision>{NUMBER})ms.*?"
    rf"FogAvg=(?P<fog>{NUMBER})ms.*?"
    rf"TotalAvg=(?P<total>{NUMBER})ms",
    re.IGNORECASE,
)


def number(value: str) -> float:
    return float(value)


def choose_log(script_path: Path, explicit_log: Optional[Path], project_root: Optional[Path]) -> Path:
    if explicit_log:
        path = explicit_log.expanduser().resolve()
        if not path.is_file():
            raise FileNotFoundError(f"找不到日志: {path}")
        return path

    if project_root:
        root = project_root.expanduser().resolve()
    else:
        # .../<Project>/Plugins/FogOfWar/Scripts/this_file.py
        root = script_path.resolve().parents[3]

    log_dir = root / "Saved" / "Logs"
    candidates = [path for path in log_dir.glob("*.log") if path.is_file()]
    if not candidates:
        raise FileNotFoundError(f"{log_dir} 下没有 .log 文件")
    return max(candidates, key=lambda path: path.stat().st_mtime)


def parse_log(path: Path) -> tuple[list[dict], list[dict], list[dict]]:
    cpu: list[dict] = []
    gpu: list[dict] = []
    minimap: list[dict] = []

    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line_number, line in enumerate(stream, 1):
            match = CPU_RE.search(line)
            if match:
                item = {key: value for key, value in match.groupdict().items()}
                item.update(
                    line=line_number,
                    push=number(item.pop("push")),
                    upload=number(item.pop("upload")),
                    sources=int(item.pop("sources")),
                    batches=int(item.pop("batches")),
                )
                cpu.append(item)

            match = GPU_RE.search(line)
            if match:
                item = {key: value for key, value in match.groupdict().items()}
                item.update(
                    line=line_number,
                    sources=int(item.pop("sources")),
                    samples=int(item.pop("samples")),
                    vision=number(item.pop("vision")),
                    composite=number(item.pop("composite")),
                    total=number(item.pop("total")),
                    minimum=number(item.pop("minimum")),
                    maximum=number(item.pop("maximum")),
                )
                gpu.append(item)

            match = MINIMAP_RE.search(line)
            if match:
                item = {key: value for key, value in match.groupdict().items()}
                item.update(
                    line=line_number,
                    agents=int(item.pop("agents")),
                    samples=int(item.pop("samples")),
                    units=number(item.pop("units")),
                    vision=number(item.pop("vision")),
                    fog=number(item.pop("fog")),
                    total=number(item.pop("total")),
                )
                minimap.append(item)

    return cpu, gpu, minimap


def stats(values: Iterable[float]) -> str:
    values = list(values)
    if not values:
        return "n/a"
    return f"avg {mean(values):.3f} ms | min {min(values):.3f} ms | max {max(values):.3f} ms"


def print_report(path: Path, cpu: list[dict], gpu: list[dict], minimap: list[dict], tail: int) -> None:
    print(f"日志: {path}")
    print(f"场景 Fog CPU 记录: {len(cpu)} | 场景 Fog GPU 记录: {len(gpu)} | 小地图 GPU 参考: {len(minimap)}")

    if not cpu and not gpu:
        print("未找到 MassBattleFrame 场景战争迷雾性能记录。请确认游戏已加载包含 Fog Actor 的新二进制。")
        return

    recent_cpu = cpu[-tail:] if cpu else []
    recent_gpu = gpu[-tail:] if gpu else []
    print(f"\n最近 {max(len(recent_cpu), len(recent_gpu))} 条场景记录:")
    print("类型       行号   数量     CPU推送   GPU Vision   GPU Composite   GPU总计   串行总计")
    print("-" * 90)

    gpu_by_sources: dict[int, dict] = {}
    for item in gpu:
        gpu_by_sources[item["sources"]] = item

    for item in recent_cpu:
        gpu_item = gpu_by_sources.get(item["sources"])
        gpu_total = gpu_item["total"] if gpu_item else None
        serialized = item["push"] + gpu_total if gpu_total is not None else None
        print(
            f"CPU        {item['line']:>5} {item['sources']:>7} "
            f"{item['push']:>9.3f} "
            f"{gpu_item['vision']:>11.3f} {gpu_item['composite']:>15.3f} {gpu_total:>9.3f} {serialized:>9.3f}"
            if gpu_item
            else
            f"CPU        {item['line']:>5} {item['sources']:>7} {item['push']:>9.3f} "
            f"{'-':>11} {'-':>15} {'-':>9} {'-':>9}"
        )

    for item in recent_gpu:
        if any(item["line"] == cpu_item["line"] for cpu_item in recent_cpu):
            continue
        print(
            f"GPU        {item['line']:>5} {item['sources']:>7} "
            f"{'-':>9} {item['vision']:>11.3f} {item['composite']:>15.3f} "
            f"{item['total']:>9.3f} {'-':>9}"
        )

    print("\n最近记录统计:")
    if recent_cpu:
        print(f"CPU ParameterPush: {stats(item['push'] for item in recent_cpu)}")
        print(f"CPU ArrayUpload 子计时: {stats(item['upload'] for item in recent_cpu)}（不与 ParameterPush 相加）")
    if recent_gpu:
        print(f"GPU VisionMask: {stats(item['vision'] for item in recent_gpu)}")
        print(f"GPU Composite: {stats(item['composite'] for item in recent_gpu)}")
        print(f"GPU Total: {stats(item['total'] for item in recent_gpu)}")
        paired = [
            cpu_item["push"] + gpu_by_sources[cpu_item["sources"]]["total"]
            for cpu_item in recent_cpu
            if cpu_item["sources"] in gpu_by_sources
        ]
        if paired:
            print(f"串行总时间（CPU ParameterPush + GPU Total）: {stats(paired)}")
        print("注：串行总时间是 CPU/GPU 不扣重叠的核算值，不等同于严格墙钟帧时间。")
    else:
        print("GPU Total: 当前日志没有场景 Fog GPU 记录，无法计算完整总时间。")
        print("请用包含 MassBattleFrameFogGPU 日志输出的新插件二进制重新运行刷兵测试。")

    if minimap:
        latest = minimap[-1]
        print(
            f"\n小地图 GPU 仅作参考（不计入场景 Fog 总时间）: "
            f"Agents={latest['agents']} Total={latest['total']:.3f} ms"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="查看最新 MassBattleFrame 场景战争迷雾性能日志")
    parser.add_argument("--log", type=Path, help="指定日志文件；默认自动选择 Saved/Logs 下最新的 .log")
    parser.add_argument("--project-root", type=Path, help="指定 UE 项目根目录；默认从脚本位置推断")
    parser.add_argument("--tail", type=int, default=20, help="显示最近多少条 CPU/GPU 记录（默认 20）")
    args = parser.parse_args()
    if args.tail < 1:
        parser.error("--tail 必须大于 0")

    try:
        path = choose_log(Path(__file__), args.log, args.project_root)
        cpu, gpu, minimap = parse_log(path)
        print_report(path, cpu, gpu, minimap, args.tail)
        return 0
    except (OSError, ValueError) as error:
        print(f"读取性能日志失败: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
