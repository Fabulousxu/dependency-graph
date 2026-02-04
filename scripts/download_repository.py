import gzip
import os
import requests

repo_infos = [
    {
        "repo": "Debian",
        "url": "https://mirror.sjtu.edu.cn/debian",
        "dists": ["bookworm", "bullseye", "forky", "trixie"],
        "archs": ["all", "alpha", "amd64", "arm", "arm64", "armel", "armhf", "hppa", "i386", "ia64", "m68k", "mips",
                  "mips64el", "mipsel", "powerpc", "ppc64el", "riscv64", "s390", "s390x", "sparc"]
    },
    {
        "repo": "Debian",
        "url": "https://archive.debian.org/debian",
        "dists": ["buster", "etch", "hamm", "jessie", "lenny", "potato", "sarge", "slink", "squeeze", "stretch",
                  "wheezy", "woody"],
        "archs": ["all", "alpha", "amd64", "arm", "arm64", "armel", "armhf", "hppa", "i386", "ia64", "m68k", "mips",
                  "mips64el", "mipsel", "powerpc", "ppc64el", "riscv64", "s390", "s390x", "sparc"]
    },
    {
        "repo": "Deepin",
        "url": "https://ftp.sjtu.edu.cn/deepin",
        "dists": ["apricot"],
        "archs": ["amd64", "i386"]
    },
    {
        "repo": "Kali",
        "url": "https://mirrors.sjtug.sjtu.edu.cn/kali",
        "dists": ["kali-rolling"],
        "archs": ["amd64", "arm64", "armel", "armhf", "i386"]
    },
    {
        "repo": "openKylin",
        "url": "https://mirror.sjtu.edu.cn/openkylin",
        "dists": ["huanghe", "nile", "yangtze"],
        "archs": ["amd64", "arm64", "i386", "loong64", "riscv64", "rv64g"]
    },
    {
        "repo": "Ubuntu",
        "url": "https://ftp.sjtu.edu.cn/ubuntu/",
        "dists": ["bionic", "devel", "focal", "jammy", "noble", "plucky", "questing", "resolute", "trusty", "xenial"],
        "archs": ["amd64", "arm64", "armel", "armhf", "hppa", "i386", "ia64", "powerpc", "ppc64el", "riscv64", "s390x",
                  "sparc"]
    },
    {
        "repo": "Ubuntu",
        "url": "https://old-releases.ubuntu.com/ubuntu",
        "dists": ["artful", "breezy", "cosmic", "dapper", "disco", "edgy", "eoan", "feisty", "groovy", "gutsy", "hardy",
                  "hirsute", "hoary", "impish", "intrepid", "jaunty", "karmic", "kinetic", "lucid", "lunar", "mantic",
                  "maverick", "natty", "oneiric", "oracular", "precise", "quantal", "raring", "saucy", "utopic",
                  "vivid", "warty", "wily", "yakkety", "zesty"],
        "archs": ["amd64", "arm64", "armel", "armhf", "hppa", "i386", "ia64", "powerpc", "ppc64el", "riscv64", "s390x",
                  "sparc"]
    }
]


def download_repositories(output_dir):
    os.makedirs(output_dir, exist_ok=True)
    for repo_info in repo_infos:
        repo = repo_info["repo"]
        url = repo_info["url"]
        for dist in repo_info["dists"]:
            for arch in repo_info["archs"]:
                pkg_url = f"{url}/dists/{dist}/main/binary-{arch}/Packages.gz"
                try:
                    print(f"Downloading from {pkg_url}...")
                    res = requests.get(pkg_url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
                    res.raise_for_status()
                    output_file = f"{output_dir}/{repo}-{dist}-{arch}"
                    with open(output_file, "wb") as f:
                        f.write(gzip.decompress(res.content))
                except Exception as e:
                    print(f"Failed: {e}. Skipping.")
                    continue


if __name__ == "__main__":
    download_repositories("../repo")
