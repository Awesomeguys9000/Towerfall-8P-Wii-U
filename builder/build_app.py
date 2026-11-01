"""Package the builder as a standalone app with PyInstaller (Python included).

    pip install pyinstaller certifi
    python builder/build_app.py

Output (in builder/dist/): "TowerFall 8P Builder.app" on macOS, "TowerFall 8P Builder.exe" on Windows,
"towerfall-8p-builder" on Linux, plus a .zip of it ready to upload. The app bundles only the port's
own source (from git), never game files.
"""
import os
import platform
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
NAME = "TowerFall 8P Builder"
# the port's own files the build needs (tracked in git only, so no build outputs or game data)
SOURCE = ["CMakeLists.txt", "runtime/", "tools/cs2cpp/", "tools/port_cs/", "tools/port_edits.py",
          "wiiu/make_assets.py", "wiiu/patch_rpx_stack.py"]


def stage_source(dst):
    shutil.rmtree(dst, ignore_errors=True)
    files = subprocess.run(["git", "ls-files"], cwd=PORT, capture_output=True, text=True, check=True).stdout.split("\n")
    n = 0
    for f in files:
        if f and any(f == s or (s.endswith("/") and f.startswith(s)) for s in SOURCE):
            os.makedirs(os.path.dirname(os.path.join(dst, f)), exist_ok=True)
            shutil.copy2(os.path.join(PORT, f), os.path.join(dst, f))
            n += 1
    print(f"staged {n} source files")


def main():
    work = os.path.join(HERE, "_build")
    dist = os.path.join(HERE, "dist")
    src = os.path.join(work, "portsrc")
    stage_source(src)
    shutil.rmtree(dist, ignore_errors=True)
    sep = ";" if sys.platform == "win32" else ":"
    linux = sys.platform.startswith("linux")
    args = [sys.executable, "-m", "PyInstaller", os.path.join(HERE, "app.py"), "--noconfirm", "--clean",
            "--name", "towerfall-8p-builder" if linux else NAME,
            "--distpath", dist, "--workpath", os.path.join(work, "pyi"), "--specpath", work,
            "--add-data", f"{src}{sep}portsrc", "--paths", HERE,
            "--hidden-import", "certifi", "--hidden-import", "zlib", "--hidden-import", "struct"]
    if sys.platform == "darwin":
        args += ["--windowed", "--osx-bundle-identifier", "io.github.towerfall8p.builder"]
    elif sys.platform == "win32":
        args += ["--onefile", "--windowed"]
    else:
        args += ["--onefile"]
    subprocess.run(args, check=True)

    arch = {"x86_64": "x64", "amd64": "x64", "arm64": "arm64", "aarch64": "arm64"}.get(platform.machine().lower(), platform.machine())
    osname = {"darwin": "macOS", "win32": "Windows"}.get(sys.platform, "Linux")
    zpath = os.path.join(dist, f"TowerFall8P-Builder-{osname}-{arch}.zip")
    if sys.platform == "darwin":
        # ditto keeps the .app's symlinks, permissions and signature intact
        subprocess.run(["ditto", "-c", "-k", "--keepParent", os.path.join(dist, NAME + ".app"), zpath], check=True)
    else:
        exe = os.path.join(dist, ("towerfall-8p-builder" if linux else NAME + ".exe"))
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
            info = zipfile.ZipInfo.from_file(exe, os.path.basename(exe))
            info.external_attr = 0o100755 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(exe, "rb") as f:
                z.writestr(info, f.read())
    print("built", zpath)


if __name__ == "__main__":
    main()
