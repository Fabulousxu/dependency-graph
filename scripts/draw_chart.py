#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import LogLocator, NullFormatter, StrMethodFormatter

ENGINE_LABELS = {
    "cpu": "XPGraph-CPU",
    "gpu": "XPGraph-GPU",
    "uncompacted": "XPGraph-Uncompacted",
    "memgraph": "Memgraph",
    "xpgraph_cpu": "XPGraph-CPU",
    "xpgraph_gpu": "XPGraph-GPU",
}

ENGINE_MARKERS = {
    "cpu": "o",
    "gpu": "s",
    "uncompacted": "^",
    "memgraph": "D",
    "xpgraph_cpu": "o",
    "xpgraph_gpu": "s",
}

QUERY_KEYS = {
    "tree": "tree_query",
    "flat": "flat_query",
}

QUERY_LABELS = {
    "tree_query": "Tree query",
    "flat_query": "Flat query",
}

SUPPORTED_FORMATS = {"png", "pdf", "svg"}

TITLE_FONT_SIZE = 18
LABEL_FONT_SIZE = 16
TICK_FONT_SIZE = 14
LEGEND_FONT_SIZE = 14


def module_root():
    return Path(__file__).resolve().parents[1]


def split_csv(values):
    items = []
    for value in values:
        items.extend(part.strip() for part in value.split(","))
    return [item for item in items if item]


def parse_depth(label):
    match = re.search(r"\d+", label)
    if not match:
        raise ValueError(f"Cannot parse depth from {label!r}")
    return int(match.group(0))


def report_name_part(json_path):
    return json_path.stem.removesuffix("_report")


def iter_input_files(input_path):
    if input_path.is_dir():
        return sorted(input_path.glob("*.json"))
    if input_path.is_file():
        return [input_path]
    raise FileNotFoundError(f"Input path does not exist: {input_path}")


def ordered_engines(latency_data):
    preferred = ["memgraph", "cpu", "gpu", "uncompacted", "xpgraph_cpu", "xpgraph_gpu"]
    engines = [engine for engine in preferred if engine in latency_data]
    engines.extend(engine for engine in latency_data if engine not in engines)
    return engines


def collect_points(latency_data, engine, query_key, metric):
    points = []
    for depth_label, values in latency_data.get(engine, {}).get(query_key, {}).items():
        value = values.get(metric)
        if value is None:
            continue
        points.append((parse_depth(depth_label), float(value)))
    return sorted(points)


def draw_chart(report, json_path, query_name, metric, output_path, log_scale):
    query_key = QUERY_KEYS[query_name]
    latency_data = report.get("latency_milliseconds", {})
    engines = ordered_engines(latency_data)

    fig, ax = plt.subplots(figsize=(9.8, 6.4))
    found = False
    all_depths = set()

    for engine in engines:
        points = collect_points(latency_data, engine, query_key, metric)
        points = [(depth, value) for depth, value in points if value > 0]
        if not points:
            continue

        depths = [depth for depth, _ in points]
        values = [value for _, value in points]
        all_depths.update(depths)
        ax.plot(
            depths,
            values,
            marker=ENGINE_MARKERS.get(engine, "o"),
            linewidth=2,
            markersize=5,
            label=ENGINE_LABELS.get(engine, engine),
        )
        found = True

    if not found:
        plt.close(fig)
        raise ValueError(f"No data for query={query_name!r}, metric={metric!r} in {json_path}")

    ax.set_title(
        f"{report.get('title') or json_path.stem}\n"
        f"{QUERY_LABELS[query_key]} - {metric} latency",
        fontsize=TITLE_FONT_SIZE,
    )
    ax.set_xlabel("Dependency depth", fontsize=LABEL_FONT_SIZE)
    ax.set_ylabel(f"{metric} latency (ms)", fontsize=LABEL_FONT_SIZE)
    ax.set_xticks(sorted(all_depths))
    ax.xaxis.set_major_formatter(StrMethodFormatter("{x:,.0f}"))
    ax.yaxis.set_major_formatter(StrMethodFormatter("{x:,.0f}"))
    ax.tick_params(axis="both", labelsize=TICK_FONT_SIZE)
    if log_scale:
        ax.set_yscale("log")
        ax.yaxis.set_major_locator(LogLocator(base=10))
        ax.yaxis.set_major_formatter(StrMethodFormatter("{x:,.0f}"))
        ax.yaxis.set_minor_formatter(NullFormatter())
    ax.grid(True, which="both", linestyle="--", linewidth=0.7, alpha=0.4)
    ax.legend(fontsize=LEGEND_FONT_SIZE)
    fig.tight_layout()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=300)
    plt.close(fig)


def plot_report(json_path, output_dir, metrics, queries, formats, log_scale):
    with json_path.open("r", encoding="utf-8") as f:
        report = json.load(f)

    if "latency_milliseconds" not in report:
        raise ValueError(f"No latency_milliseconds data found in {json_path}")

    output_paths = []
    scale_suffix = "_log" if log_scale else ""
    for query_name in queries:
        for metric in metrics:
            for output_format in formats:
                output_path = (
                        output_dir
                        / f"{report_name_part(json_path)}_{query_name}_{metric}{scale_suffix}.{output_format}"
                )
                draw_chart(report, json_path, query_name, metric, output_path, log_scale)
                output_paths.append(output_path)
    return output_paths


def parse_args():
    root = module_root()
    parser = argparse.ArgumentParser(description="Plot XPGraph benchmark report JSON files.")
    parser.add_argument(
        "-i",
        "--input",
        type=Path,
        default=root / "reports" / "benchmarks",
        help="Input benchmark JSON file or directory. Defaults to reports/benchmarks.",
    )
    parser.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        default=root / "reports" / "figures",
        help="Output directory. Defaults to reports/figures.",
    )
    parser.add_argument(
        "--metrics",
        nargs="+",
        default=["avg"],
        help="Metrics to plot, e.g. avg or avg,p50,p95. Defaults to avg.",
    )
    parser.add_argument(
        "--queries",
        nargs="+",
        default=["tree", "flat"],
        help="Queries to plot: tree, flat, or tree,flat. Defaults to both.",
    )
    parser.add_argument(
        "--formats",
        nargs="+",
        default=["svg"],
        help="Output formats: png, pdf, svg, or png,svg. Defaults to svg.",
    )
    parser.add_argument(
        "--log-scale",
        action="store_true",
        help="Use a logarithmic Y axis. Defaults to a linear Y axis.",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    metrics = split_csv(args.metrics)
    queries = split_csv(args.queries)
    formats = [item.lower() for item in split_csv(args.formats)]

    unknown_queries = sorted(set(queries) - set(QUERY_KEYS))
    if unknown_queries:
        raise SystemExit(f"Unsupported queries: {unknown_queries}. Supported: {sorted(QUERY_KEYS)}")

    unknown_formats = sorted(set(formats) - SUPPORTED_FORMATS)
    if unknown_formats:
        raise SystemExit(f"Unsupported formats: {unknown_formats}. Supported: {sorted(SUPPORTED_FORMATS)}")

    input_files = iter_input_files(args.input)
    if not input_files:
        raise SystemExit(f"No JSON files found in {args.input}")

    for json_path in input_files:
        for output_path in plot_report(json_path, args.output_dir, metrics, queries, formats, args.log_scale):
            print(output_path)


if __name__ == "__main__":
    main()
