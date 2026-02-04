import json
import os


def generate_dataset(repos_dir: str, size: int, output_file: str, base_dir: str | None = None):
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    with open(output_file, "w") as f:
        curr_size = 0
        for entry in os.scandir(repos_dir):
            if entry.is_file():
                if base_dir is None:
                    filepath = os.path.abspath(entry).replace('\\', '/')
                else:
                    filepath = f"{base_dir}/{entry.name}"
                item = {"path": filepath}
                f.write(f"{json.dumps(item)}\n")
                curr_size += 1
            if curr_size >= size:
                break


if __name__ == "__main__":
    generate_dataset("../repos", 50, "../datasets/windows/repos-50.jsonl")
    generate_dataset("../repos", 100, "../datasets/windows/repos-100.jsonl")
    generate_dataset("../repos", 200, "../datasets/windows/repos-200.jsonl")
    generate_dataset("../repos", 388, "../datasets/windows/repos-388.jsonl")

    generate_dataset("../repos", 50, "../datasets/linux/repos-50.jsonl", "/depgraph/repos")
    generate_dataset("../repos", 100, "../datasets/linux/repos-100.jsonl", "/depgraph/repos")
    generate_dataset("../repos", 200, "../datasets/linux/repos-200.jsonl", "/depgraph/repos")
    generate_dataset("../repos", 388, "../datasets/linux/repos-388.jsonl", "/depgraph/repos")
