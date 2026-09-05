#!/usr/bin/env python3
"""Build a relocatable, ad-hoc-signed Echelon.app with a closed dylib dependency set."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def system_library(name):
    return name.startswith(("/usr/lib/", "/System/Library/"))


def dependencies(binary):
    return [line.strip().split(" (", 1)[0] for line in run("otool", "-L", str(binary)).splitlines()[1:]]


def rpaths(binary):
    lines = run("otool", "-l", str(binary)).splitlines()
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            yield lines[index + 2].strip().split(" (", 1)[0].removeprefix("path ")


def main():
    if sys.platform != "darwin":
        raise RuntimeError("Run this packager on macOS with Xcode command-line tools")
    repo = Path(__file__).resolve().parents[3]
    build = repo / "build" / (sys.argv[1] if len(sys.argv) > 1 else "macos-vulkan")
    brand = json.loads((build / "generated/brand.json").read_text())
    source = build / brand["output_directory"]
    executable = source / brand["executable"]
    modules = [source / f"libEchelon{game}Engine.so" for game in ("Generals", "ZeroHour")]
    # CMake MODULE targets on Darwin use .so unless a toolchain explicitly changes the suffix.
    modules = [path if path.is_file() else path.with_suffix(".dylib") for path in modules]
    sdk_candidates = [Path(os.environ[key]) for key in ("VULKAN_SDK", "VULKAN_SDK_ROOT") if os.environ.get(key)]
    sdk_candidates += sorted((Path.home() / "VulkanSDK").glob("*/macOS"), reverse=True)
    sdk = next((path for path in sdk_candidates if (path / "lib/libMoltenVK.dylib").is_file()), None)
    if not sdk:
        raise RuntimeError("Set VULKAN_SDK to an SDK containing libvulkan and libMoltenVK")
    roots = [executable, *modules, sdk / "lib/libvulkan.1.dylib", sdk / "lib/libMoltenVK.dylib"]
    sage_patch = build / "Patches/SagePatch/libsage_patch.dylib"
    if sage_patch.is_file():
        roots.append(sage_patch)
    for path in roots:
        if not path.is_file():
            raise RuntimeError(f"Missing build artifact: {path}")
    search = [sdk / "lib", Path("/opt/homebrew/lib"), Path("/usr/local/lib")]
    index = {}
    for path in build.rglob("*.dylib"):
        index.setdefault(path.name, []).append(path)

    def resolve(binary, name):
        candidates = []
        if name.startswith("@loader_path/"):
            candidates.append(binary.parent / name.removeprefix("@loader_path/"))
        elif name.startswith("@executable_path/"):
            candidates.append(executable.parent / name.removeprefix("@executable_path/"))
        elif name.startswith("@rpath/"):
            tail = name.removeprefix("@rpath/")
            for parent in rpaths(binary):
                parent = parent.replace("@loader_path", str(binary.parent)).replace("@executable_path", str(executable.parent))
                candidates.append(Path(parent) / tail)
        else:
            candidates.append(Path(name))
        candidates.extend(parent / Path(name).name for parent in search)
        candidates.extend(index.get(Path(name).name, []))
        found = next((path.resolve() for path in candidates if path.is_file()), None)
        if not found:
            raise RuntimeError(f"Unresolved dependency {name} of {binary}")
        return found

    with tempfile.TemporaryDirectory(prefix="echelon-macos-") as temporary:
        app = Path(temporary) / f'{brand["name"]}.app'
        macos, resources = app / "Contents/MacOS", app / "Contents/Resources"
        macos.mkdir(parents=True)
        resources.mkdir()
        pending = [(path.resolve(), path.name) for path in roots]
        staged, origins = {}, {}
        while pending:
            original, name = pending.pop()
            digest = hashlib.sha256(original.read_bytes()).hexdigest()
            if name in staged:
                if staged[name] != digest:
                    raise RuntimeError(f"Conflicting dylibs named {name}: {original} and {origins[name]}")
                continue
            staged[name], origins[name] = digest, original
            target = macos / name
            shutil.copy2(original, target)
            target.chmod(target.stat().st_mode | 0o200)
            mappings = []
            for dependency in dependencies(original):
                if system_library(dependency):
                    continue
                resolved = resolve(original, dependency)
                # Use the install-name basename, retaining versioned aliases when required.
                dep_name = Path(dependency).name
                mappings.extend(["-change", dependency, f"@loader_path/{dep_name}"])
                pending.append((resolved, dep_name))
            if original != executable.resolve():
                mappings.extend(["-id", f"@loader_path/{name}"])
            if mappings:
                run("install_name_tool", *mappings, str(target))
        for name in staged:
            for dependency in dependencies(macos / name):
                if not system_library(dependency) and not (dependency.startswith("@loader_path/") and (macos / Path(dependency).name).is_file()):
                    raise RuntimeError(f"Bundle dependency escapes app: {name}: {dependency}")
        for name in ("echelon-logo.png", "echelon-launcher-background.png", "echelon-icon.png"):
            shutil.copy2(repo / "assets/launcher" / name, resources / name)
        # Do not redistribute an Apple font: use the pinned DejaVu font supplied to CI.
        font = Path(os.environ.get("ECHELON_BUNDLE_FONT", str(repo / "assets/launcher/fonts/DejaVuSans.ttf")))
        if not font.is_file():
            raise RuntimeError("Set ECHELON_BUNDLE_FONT to a redistributable DejaVuSans.ttf")
        shutil.copy2(font, resources / "DejaVuSans.ttf")
        license_path = font.parent / "LICENSE"
        if not license_path.is_file():
            raise RuntimeError("Place the DejaVu LICENSE next to ECHELON_BUNDLE_FONT")
        shutil.copy2(license_path, resources / "DejaVu-LICENSE")
        shutil.copy2(repo / "LICENSE.md", resources / "LICENSE.md")
        shutil.copy2(repo / "resources/dxvk/dxvk.conf", resources / "dxvk.conf")
        (resources / "MoltenVK_icd.json").write_text(json.dumps({"file_format_version": "1.0.0", "ICD": {
            "library_path": "../MacOS/libMoltenVK.dylib", "api_version": "1.4.0", "is_portability_driver": True}}))
        wrapper = macos / "run.sh"
        wrapper.write_text('''#!/bin/bash
set -euo pipefail
app_bin="$(cd "$(dirname "$0")" && pwd)"
resources="${app_bin}/../Resources"
export DXVK_WSI_DRIVER=SDL3
export DXVK_HUD="${DXVK_HUD:-0}"
export VK_DRIVER_FILES="${resources}/MoltenVK_icd.json"
export VK_ICD_FILENAMES="${VK_DRIVER_FILES}"
export DXVK_CONFIG_FILE="${resources}/dxvk.conf"
export DYLD_LIBRARY_PATH="${app_bin}:${DYLD_LIBRARY_PATH:-}"
if [[ -f "${app_bin}/libsage_patch.dylib" && "${SAGE_PATCH_DISABLED:-0}" != 1 ]]; then
    export DYLD_INSERT_LIBRARIES="${app_bin}/libsage_patch.dylib${DYLD_INSERT_LIBRARIES:+:${DYLD_INSERT_LIBRARIES}}"
fi
exec "${app_bin}/''' + brand["executable"] + '''" "$@"
''')
        wrapper.chmod(0o755)
        plist = {"CFBundleName": brand["name"], "CFBundleDisplayName": brand["name"],
                 "CFBundleIdentifier": brand["application_id"], "CFBundleExecutable": "run.sh",
                 "CFBundleVersion": "1", "CFBundleShortVersionString": "0.1.0",
                 "CFBundlePackageType": "APPL", "LSMinimumSystemVersion": "15.0",
                 "NSHighResolutionCapable": True}
        (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))
        for name in staged:
            run("codesign", "--force", "--sign", "-", str(macos / name))
        run("codesign", "--force", "--sign", "-", str(app))
        run("codesign", "--verify", "--deep", "--strict", str(app))
        output = build / f'{brand["name"]}-macos-arm64.zip'
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(output))
        print(f"Bundle created: {output}")


if __name__ == "__main__":
    main()
