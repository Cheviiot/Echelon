#!/usr/bin/env python3
"""Check product identity, hosted/standalone compile boundaries and the private ABI."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path)
parser.add_argument("--standalone", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]


def check(condition, message):
    if not condition:
        raise SystemExit("FAIL: " + message)


brand = dict(re.findall(r'set\((ECHELON_\w+) "([^"]+)"\)', (root / "cmake/brand.cmake").read_text()))
app_id = brand["ECHELON_FLATPAK_ID"]
desktop = (root / "flatpak" / (app_id + ".desktop")).read_text()
metadata = ET.parse(root / "flatpak" / (app_id + ".metainfo.xml")).getroot()
manifest = (root / "flatpak" / (app_id + ".yml")).read_text()
check(metadata.findtext("id") == app_id and f"Icon={app_id}" in desktop, "package IDs diverge from brand.cmake")
check(f"app-id: {app_id}" in manifest, "Flatpak ID diverges from brand.cmake")
check(brand["ECHELON_DATA_DIRECTORY"] == ".Echelon", "clean data root changed")
check((root / "GeneralsX/Core").is_dir() and not (root / "Core").exists(), "upstream source escaped GeneralsX/")
ui = (root / "Launcher/LauncherMain.cpp").read_text()
for forbidden in ("RefreshRepositoryCatalog", "LoadCachedRepositoryCatalog", "DownloadAndInstallModification"):
    check(forbidden not in ui, "retired catalog operation returned to the UI: " + forbidden)
launcher_sources = (root / "Launcher/CMakeLists.txt").read_text().split("add_executable(echelon_launcher", 1)[1].split(")", 1)[0]
check(not any(name in launcher_sources for name in ("LauncherRepositories.cpp", "LauncherDownloads.cpp", "LauncherS3.cpp")),
      "generic remote transport was linked into the product UI")

if args.build:
    build = args.build.resolve()
    commands = json.loads((build / "compile_commands.json").read_text())
    hosted, standalone = set(), set()
    for entry in commands:
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        match = re.search(r"CMakeFiles/([gz]_gameengine(?:device)?)(_host)?\.dir", command)
        if not match:
            continue
        target, suffix = match.groups()
        flags = ("ECHELON_BRAND", "ECHELON_ENGINE_HOSTED", "ECHELON_ENGINE_MODULE_ALLOCATOR")
        if suffix:
            hosted.add(target)
            check(all(f"-D{flag}=1" in command for flag in flags), "hosted flags missing: " + target)
        else:
            standalone.add(target)
            check(not any(flag in command for flag in flags), "hosted flags leaked into standalone: " + target)
        check("-ffp-contract=off" in command or "/fp:precise" in command, "deterministic FP flag missing")
    expected = {"g_gameengine", "z_gameengine", "g_gameenginedevice", "z_gameenginedevice"}
    check(standalone == expected, "standalone compilation targets missing")
    if args.standalone:
        check(not hosted, "launcher-disabled configuration still contains hosted libraries")
        check(not any("/Launcher/" in entry["file"] for entry in commands), "launcher code compiled in standalone configuration")
    else:
        check(hosted == expected, "hosted compilation targets missing")
        for game in ("Generals", "ZeroHour"):
            module = build / "Echelon" / f"libEchelon{game}Engine.so"
            symbols = subprocess.check_output(["nm", "-D", "--defined-only", str(module)], text=True)
            exported = {line.split()[-1] for line in symbols.splitlines()}
            check(exported == {brand["ECHELON_MODULE_EXPORT"]}, "unexpected engine exports: " + repr(exported))

print("PASS: Echelon identity, source ownership and build boundaries")
