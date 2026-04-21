import json
import re
from pathlib import Path
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator

series_keys = {
    "In-Memory, CPU": "in_memory_results",
    "Storage, CPU": "disk_results",
    "Storage, GPU": "gpu_results",
    "Storage, Cold load, CPU": "loaded_results",
}


def to_number(value):
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        match = re.search(r"-?\d+(?:\.\d+)?", value)
        if match:
            return float(match.group(0))
    return None


def extract_series_points(records, latency_key):
    points = []
    for item in records or []:
        depth = to_number(item.get("depth"))
        latency = to_number(item.get(latency_key))
        if depth is None or latency is None:
            continue
        points.append((depth, latency))
    points.sort(key=lambda x: x[0])
    return points


def draw_chart(json_path, latency_key="avg", title="", output_path=None):
    json_path = Path(json_path)
    with json_path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    found_any = False
    plt.figure(figsize=(10, 6))

    for label, result_key in series_keys.items():
        points = extract_series_points(data.get(result_key), latency_key)
        if not points:
            print(f"[warn] No valid points for '{label}' with metric '{latency_key}'.")
            continue
        depths = [p[0] for p in points]
        latencies = [p[1] for p in points]
        plt.plot(depths, latencies, marker="o", linewidth=2, label=label)
        found_any = True

    if not found_any:
        raise ValueError(f"No plottable data found in {json_path} for latency_key='{latency_key}'.")

    plt.title(title, fontsize=20)
    plt.xlabel("depth", fontsize=20)
    ax = plt.gca()
    ax.xaxis.set_major_locator(MultipleLocator(1))
    ax.tick_params(axis="both", labelsize=20)
    plt.ylabel("mean latency (ms)", fontsize=20)
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.legend(fontsize=20)
    plt.tight_layout()

    if output_path is not None:
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        plt.savefig(output_path, dpi=150)
        print(f"Saved chart to: {output_path}")

    plt.show()


if __name__ == "__main__":
    draw_chart("../results/query_dependencies_benchmark_50_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nNo Memory Limit, Dataset B",
               "../docs/figures/query_dependencies_benchmark_dataset_b.png")
    draw_chart("../results/query_dependencies_benchmark_100_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nNo Memory Limit, Dataset C",
               "../docs/figures/query_dependencies_benchmark_dataset_c.png")
    draw_chart("../results/query_dependencies_benchmark_200_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nNo Memory Limit, Dataset D",
               "../docs/figures/query_dependencies_benchmark_dataset_d.png")
    draw_chart("../results/query_dependencies_benchmark_388_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nNo Memory Limit, Dataset E",
               "../docs/figures/query_dependencies_benchmark_dataset_e.png")

    draw_chart("../results/query_dependencies_benchmark_50_memory_256m_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nMemory Limit 256MB, Dataset B",
               "../docs/figures/query_dependencies_benchmark_dataset_b_memory_256m.png")
    draw_chart("../results/query_dependencies_benchmark_100_memory_256m_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nMemory Limit 256MB, Dataset C",
               "../docs/figures/query_dependencies_benchmark_dataset_c_memory_256m.png")
    draw_chart("../results/query_dependencies_benchmark_200_memory_256m_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nMemory Limit 256MB, Dataset D",
               "../docs/figures/query_dependencies_benchmark_dataset_d_memory_256m.png")

    series_keys = {
        "Storage, CPU": "disk_results",
        "Storage, GPU": "gpu_results",
    }

    draw_chart("../results/query_dependencies_benchmark_50_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nNo Memory Limit, Dataset B",
               "../docs/figures/query_dependencies_benchmark_dataset_b_gpu_only.png")
    draw_chart("../results/query_dependencies_benchmark_100_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nNo Memory Limit, Dataset C",
               "../docs/figures/query_dependencies_benchmark_dataset_c_gpu_only.png")
    draw_chart("../results/query_dependencies_benchmark_200_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nNo Memory Limit, Dataset D",
               "../docs/figures/query_dependencies_benchmark_dataset_d_gpu_only.png")
    draw_chart("../results/query_dependencies_benchmark_388_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nNo Memory Limit, Dataset E",
               "../docs/figures/query_dependencies_benchmark_dataset_e_gpu_only.png")

    draw_chart("../results/query_dependencies_benchmark_50_memory_256m_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nMemory Limit 256MB, Dataset B",
               "../docs/figures/query_dependencies_benchmark_dataset_b_memory_256m_gpu_only.png")
    draw_chart("../results/query_dependencies_benchmark_100_memory_256m_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nMemory Limit 256MB, Dataset C",
               "../docs/figures/query_dependencies_benchmark_dataset_c_memory_256m_gpu_only.png")
    draw_chart("../results/query_dependencies_benchmark_200_memory_256m_result.json", "avg",
               "Query Dependencies Benchmark (Mean Latency)\nMemory Limit 256MB, Dataset D",
               "../docs/figures/query_dependencies_benchmark_dataset_d_memory_256m_gpu_only.png")
