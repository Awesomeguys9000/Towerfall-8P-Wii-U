"""Package the builder as a standalone app with PyInstaller (Python included).

    pip install pyinstaller certifi
    python builder/build_app.py

macOS signing (optional, all from environment variables):
    MACOS_SIGN_IDENTITY   e.g. "Developer ID Application: Your Name (TEAMID)": sign with the hardened runtime
    NOTARY_PROFILE        a keychain profile from `xcrun notarytool store-credentials`: also notarize and staple
    or NOTARY_APPLE_ID, NOTARY_PASSWORD (app-specific password), NOTARY_TEAM_ID (used in CI)

Output (in builder/dist/): "TowerFall 8P Builder.app" on macOS, "TowerFall 8P Builder.exe" on Windows,
"towerfall-8p-builder" on Linux, plus a .zip of it ready to upload. The app bundles only the port's
own source (from git), never game files.
"""
import os
import platform
import re
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
        sign_and_notarize(os.path.join(dist, NAME + ".app"), zpath)
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


def sign_and_notarize(app, zpath):
    ident = os.environ.get("MACOS_SIGN_IDENTITY")
    if not ident:
        print("not signing (MACOS_SIGN_IDENTITY not set)")
        return
    ent = os.path.join(HERE, "entitlements.plist")
    sign = ["codesign", "--force", "--timestamp", "--options", "runtime", "--entitlements", ent, "--sign", ident]
    # sign every Mach-O inside first (innermost first), then the bundle itself
    machos = []
    for root, _, names in os.walk(os.path.join(app, "Contents")):
        for n in names:
            p = os.path.join(root, n)
            if os.path.islink(p):
                continue
            with open(p, "rb") as f:
                magic = f.read(4)
            if magic in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
                machos.append(p)
    for p in sorted(machos, key=lambda x: -x.count(os.sep)):
        subprocess.run(sign + [p], check=True, capture_output=True)
    subprocess.run(sign + [app], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", app], check=True)
    print(f"signed {len(machos)} binaries and the app as {ident}")

    profile = os.environ.get("NOTARY_PROFILE")
    creds = [os.environ.get(k) for k in ("NOTARY_APPLE_ID", "NOTARY_PASSWORD", "NOTARY_TEAM_ID")]
    auth = ["--keychain-profile", profile] if profile else \
        (["--apple-id", creds[0], "--password", creds[1], "--team-id", creds[2]] if all(creds) else None)
    if not auth:
        print("not notarizing (set NOTARY_PROFILE, or NOTARY_APPLE_ID / NOTARY_PASSWORD / NOTARY_TEAM_ID)")
    else:
        subprocess.run(["ditto", "-c", "-k", "--keepParent", app, zpath], check=True)
        print("submitting to Apple for notarization (usually a few minutes)")
        r = subprocess.run(["xcrun", "notarytool", "submit", zpath, "--wait"] + auth, capture_output=True, text=True)
        print(r.stdout, r.stderr)
        if r.returncode != 0 or "status: Accepted" not in r.stdout:
            sub = re.search(r"id: ([0-9a-f-]{36})", r.stdout)
            if sub:  # show Apple's reasons
                subprocess.run(["xcrun", "notarytool", "log", sub.group(1)] + auth)
            raise SystemExit("notarization failed")
        subprocess.run(["xcrun", "stapler", "staple", app], check=True)
        print("notarized and stapled")
    subprocess.run(["spctl", "--assess", "--type", "execute", "--verbose=2", app])


if __name__ == "__main__":
    main()
