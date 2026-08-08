#!/usr/bin/env python3

import argparse
import json
import re
from datetime import datetime, timezone
from pathlib import Path

SF_RE = re.compile(r"^SF:(.+)$")


def extract_source_files(info_path: Path) -> list[str]:
    files = []
    seen = set()
    for line in info_path.read_text().splitlines():
        m = SF_RE.match(line)
        if m and m.group(1) not in seen:
            seen.add(m.group(1))
            files.append(m.group(1))
    return files


def write_sources_txt(out_path: Path, repo_root: Path, rel_paths: list[str]):
    with out_path.open("w") as f:
        for rel_path in rel_paths:
            full_path = repo_root / rel_path
            if not full_path.is_file():
                continue
            f.write(f"### FILE: {rel_path}\n")
            f.write(full_path.read_text(errors="replace"))
            f.write("\n")


def write_config_json(out_path: Path, args):
    config = {
        "title": "Scorpio Utils Coverage",
        "repo": args.repo,
        "branch": args.branch,
        "commit": args.commit,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "datasets": {
            "colcon-test": {
                "coverage": ["coverage.info"],
            },
        },
    }
    out_path.write_text(json.dumps(config, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--info", required=True, type=Path)
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--branch", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()

    rel_paths = extract_source_files(args.info)
    write_sources_txt(args.out_dir / "sources.txt", args.repo_root, rel_paths)
    write_config_json(args.out_dir / "config.json", args)


if __name__ == "__main__":
    main()
