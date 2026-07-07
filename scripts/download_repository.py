import argparse
import gzip
import json
import re
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import unquote, urlparse
import xml.etree.ElementTree as ET
import requests


def get_rpm_primary_url(session, key, repo, base_url, dist, arch):
    if key.lower() == "fedora":
        if arch == "source":
            repo_root = f"{base_url}/{dist}/Everything/source/tree"
        else:
            repo_root = f"{base_url}/{dist}/Everything/{arch}/os"
    elif key.lower() == "openeuler":
        repo_root = f"{base_url}/{dist}/everything/{arch}"
    else:
        raise ValueError(f"Unsupported RPM repository: {key}")
    repomd_url = f"{repo_root}/repodata/repomd.xml"
    repomd_response = session.get(repomd_url, timeout=180)
    repomd_response.raise_for_status()
    root = ET.fromstring(repomd_response.content)
    namespace = {"repo": "http://linux.duke.edu/metadata/repo"}
    for data in root.findall("repo:data", namespace):
        if data.get("type") != "primary":
            continue
        location = data.find("repo:location", namespace)
        if location is None:
            break
        href = location.get("href")
        if href:
            return f"{repo_root}/{href}"
        break
    raise ValueError(f"Primary metadata not found in repomd.xml for RPM repository {dist}/{arch}")


def list_directories(session, url):
    try:
        response = session.get(url, timeout=180)
        response.raise_for_status()
    except Exception as exc:
        print(f"Failed to scan {url}: {exc}", flush=True)
        return []

    hrefs = []

    def handle_starttag(tag, attrs):
        if tag != "a":
            return
        for name, value in attrs:
            if name == "href" and value:
                hrefs.append(value)

    parser = HTMLParser()
    parser.handle_starttag = handle_starttag
    parser.feed(response.text)
    names = set()
    for href in hrefs:
        parsed = urlparse(href)
        if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment:
            continue
        name = unquote(parsed.path).strip("/")
        if name and "/" not in name and name != ".." and re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+~-]*$").match(name):
            names.add(name)
    return sorted(names)


def configured_targets(repo):
    for dist in repo["distributions"]:
        for arch in repo["architectures"]:
            yield dist, arch


def scanned_targets(session, key, repo):
    for url in repo["urls"]:
        dists = set()
        if repo["type"].upper() == "DEB":
            dists.update(list_directories(session, f"{url}/dists/"))
        elif repo["type"].upper() == "RPM" and key.lower() in ("fedora", "openeuler"):
            dists.update(list_directories(session, url))
        dists = sorted(dists)
        print(f"Scanned distributions for {key}: {len(dists)}", flush=True)
        for dist in dists:
            archs = set()
            if repo["type"].upper() == "DEB":
                for name in list_directories(session, f"{url}/dists/{dist}/main/"):
                    if name.startswith("binary-"):
                        archs.add(name.removeprefix("binary-"))
            elif repo["type"].upper() == "RPM":
                if key.lower() == "fedora":
                    archs.update(list_directories(session, f"{url}/{dist}/Everything/"))
                elif key.lower() == "openeuler":
                    archs.update(list_directories(session, f"{url}/{dist}/everything/"))
            archs = sorted(archs)
            print(f"Scanned architectures for {key}/{dist}: {len(archs)}", flush=True)
            for arch in archs:
                yield dist, arch


def sync_repository_cache(repository_config, cache_directory, scan_all=False):
    session = requests.Session()
    session.headers.update({"User-Agent": "Mozilla/5.0"})
    downloaded = 0
    skipped_existing = 0
    skipped_filtered = 0
    skipped_failed = 0
    cache_root = Path(cache_directory)

    for key, repo in repository_config.items():
        if not repo.get("enabled", True):
            print(f"Skip disabled repository: {key}", flush=True)
            continue
        if repo["type"].upper() not in ("DEB", "RPM"):
            print(f"Skip unsupported repository: {key} (type={repo['type']})", flush=True)
            continue
        targets = scanned_targets(session, key, repo) if scan_all else configured_targets(repo)
        for dist, arch in targets:
            if repo["type"].upper() == "DEB":
                output_path = cache_root / key / dist / arch
            elif repo["type"].upper() == "RPM":
                output_path = cache_root / key / dist / f"{arch}.xml"
            if output_path.is_file():
                skipped_existing += 1
                print(f"Skip existing: {output_path}", flush=True)
                continue

            success = False
            last_error = None
            for base_url in repo["urls"]:
                source = None
                try:
                    if repo["type"].upper() == "DEB":
                        source = f"{base_url}/dists/{dist}/main/binary-{arch}/Packages.gz"
                        print(f"Downloading {source} -> {output_path}", flush=True)
                        response = session.get(source, timeout=180)
                        response.raise_for_status()
                        output_path.parent.mkdir(parents=True, exist_ok=True)
                        temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
                        with open(temp_path, "wb") as handle:
                            handle.write(gzip.decompress(response.content))
                        temp_path.replace(output_path)
                    elif repo["type"].upper() == "RPM":
                        source = f"{key}/{dist}/{arch} ({base_url})"
                        print(f"Downloading {source} -> {output_path}", flush=True)
                        primary_url = get_rpm_primary_url(session, key, repo, base_url, dist, arch)
                        response = session.get(primary_url, timeout=180)
                        response.raise_for_status()
                        output_path.parent.mkdir(parents=True, exist_ok=True)
                        temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
                        with open(temp_path, "wb") as handle:
                            handle.write(gzip.decompress(response.content))
                        temp_path.replace(output_path)
                    downloaded += 1
                    success = True
                    break
                except Exception as exc:
                    last_error = exc
                    print(f"Failed {source}: {exc}", flush=True)

            if not success:
                skipped_failed += 1
                print(f"Skip missing package index: {key}/{dist}/{arch} ({last_error})", flush=True)

    print(
        "Done. "
        f"downloaded={downloaded}, "
        f"skipped_existing={skipped_existing}, "
        f"skipped_filtered={skipped_filtered}, "
        f"skipped_failed={skipped_failed}",
        flush=True,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Download repository package indexes into the XPGraph cache directory."
    )
    parser.add_argument(
        "--repository-config",
        required=True,
        help="Repository config file to download",
    )
    parser.add_argument(
        "--cache-directory",
        required=True,
        help="Directory where downloaded indexes are cached as <repo>/<dist>/<arch> or <repo>/<dist>/<arch>.xml.",
    )
    parser.add_argument(
        "--scan-all",
        action="store_true",
        help="Ignore configured distributions/architectures and scan repository URLs for every available dist/arch.",
    )
    args = parser.parse_args()
    with open(args.repository_config, "r", encoding="utf-8") as file:
        config = json.load(file)
    sync_repository_cache(config, args.cache_directory, scan_all=args.scan_all)
