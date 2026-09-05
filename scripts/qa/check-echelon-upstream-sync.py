#!/usr/bin/env python3
"""Verify subtree update mapping using synthetic commits; leave refs and checkout intact."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def git(*args, data=None, env=None):
    return subprocess.check_output(["git", *args], cwd=root, input=data, env=env).strip()


baseline = json.loads((root / "docs/WORKDIR/reports/ECHELON_ENGINE_DELTA.json").read_text())["upstream"]
revision = git("rev-parse", "HEAD").decode()
updates = {
    "README.md": b"\nSynthetic subtree mapping check.\n",
    "Core/GameEngine/Include/Common/AcademyStats.h": b"\n// Synthetic subtree mapping check.\n",
}
new_path = "echelon-sync-probe.txt"
deleted_path = "stlport.diff"
with tempfile.TemporaryDirectory(prefix="echelon-sync-") as temporary:
    env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "index"))
    git("read-tree", baseline, env=env)
    for path, suffix in updates.items():
        original = subprocess.check_output(["git", "show", f"{baseline}:{path}"], cwd=root)
        blob = git("hash-object", "-w", "--stdin", data=original + suffix).decode()
        git("update-index", "--add", "--cacheinfo", f"100644,{blob},{path}", env=env)
    blob = git("hash-object", "-w", "--stdin", data=b"Synthetic new upstream file.\n").decode()
    git("update-index", "--add", "--cacheinfo", f"100644,{blob},{new_path}", env=env)
    git("update-index", "--force-remove", deleted_path, env=env)
    incoming_tree = git("write-tree", env=env).decode()
    incoming = git("commit-tree", incoming_tree, "-p", baseline,
                   data=b"test: probe upstream subtree mapping\n").decode()
    merged = git("merge-tree", "--write-tree", "-Xsubtree=GeneralsX", revision, incoming).splitlines()[0].decode()
    changed = set(git("diff", "--name-only", revision, merged).decode().splitlines())
    expected = {"GeneralsX/" + path for path in (*updates, new_path, deleted_path)}
    if changed != expected:
        raise SystemExit(f"FAIL: subtree merge changed unexpected paths: {sorted(changed ^ expected)}")
    for path in (*updates, new_path):
        if git("rev-parse", f"{merged}:GeneralsX/{path}") != git("rev-parse", f"{incoming}:{path}"):
            raise SystemExit("FAIL: incoming contents not preserved: " + path)
print("PASS: upstream edits, addition and deletion stay inside GeneralsX; product tree is unchanged")
