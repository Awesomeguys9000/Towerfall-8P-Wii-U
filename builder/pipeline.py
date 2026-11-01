"""TowerFall 8P Wii U builder: the build steps, shared by the GUI (app.py) and the command line.

    python3 builder/pipeline.py /path/to/TowerFall-folder [/path/to/SD-card]

Goes from YOUR copy of TowerFall (8-player, macOS/Linux FNA build) to a ready-to-copy Wii U SD card:
decompile -> port edits -> C# to C++ -> Wii U images -> Wii U build -> SD card layout.
.NET, CMake and Ninja are downloaded into the builder's own folder when they are missing.
devkitPro can't be bundled or downloaded silently; install_devkitpro() starts its installer.
Only the Python standard library is used (plus certifi when available, for HTTPS in the bundled app).
"""
import glob
import json
import os
import re
import shutil
import ssl
import stat
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

IS_WIN = sys.platform == "win32"
IS_MAC = sys.platform == "darwin"
EXE = ".exe" if IS_WIN else ""
FROZEN = getattr(sys, "frozen", False)   # running as the bundled .exe / .app

APP_HOME = os.path.join(os.path.expanduser("~"), "TowerFall8P-Builder")
TOOLS = os.path.join(APP_HOME, "tools")
if FROZEN:
    # The port's source ships inside the app; it is copied here because the build writes next to it.
    BUNDLED_SRC = os.path.join(getattr(sys, "_MEIPASS", os.path.dirname(sys.executable)), "portsrc")
    PORT = os.path.join(APP_HOME, "port")
    OUT = os.path.join(APP_HOME, "SD card files")
else:
    BUNDLED_SRC = None
    PORT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    OUT = os.path.join(PORT, "dist", "sd")

DOTNET_MAJOR = 10  # tools/cs2cpp targets net10.0
CMAKE_VER = "3.31.6"
NINJA_VER = "1.12.1"
DKP_PACKAGES = ["wiiu-dev", "wiiu-sdl2", "wiiu-sdl2_mixer"]

URLS = {
    "dotnet": "https://dotnet.microsoft.com/download/dotnet/10.0",
    "cmake": "https://cmake.org/download/",
    "devkitpro": "https://devkitpro.org/wiki/Getting_Started",
}
NOWINDOW = {"creationflags": 0x08000000} if IS_WIN else {}  # CREATE_NO_WINDOW


class BuildError(Exception):
    pass


class Cancelled(Exception):
    pass


def py_cmd(script, *args):
    """Command that runs one of the port's Python scripts (the bundled app runs them itself)."""
    if FROZEN:
        return [sys.executable, "--py", script, *args]
    return [sys.executable, script, *args]


def run_py_script(argv):
    """Entry for `app --py script args...` in the bundled app."""
    import runpy
    script = argv[0]
    sys.argv = list(argv)
    for name in ("stdout", "stderr"):  # a windowed app may have no console streams
        if getattr(sys, name) is None:
            setattr(sys, name, open(os.devnull, "w"))
    sys.path.insert(0, os.path.dirname(os.path.abspath(script)))
    runpy.run_path(script, run_name="__main__")


def open_path(p):
    if IS_WIN:
        os.startfile(p)
    else:
        subprocess.Popen(["open" if IS_MAC else "xdg-open", p])


# ------------------------------------------------------------------ game folder

def find_game(folder):
    """Returns (info dict, error message). info: content (folder holding Content/), refs (assemblies)."""
    if not folder or not os.path.isdir(folder):
        return None, "Choose the folder that holds your copy of TowerFall."
    folder = os.path.abspath(folder)
    bases = [folder, os.path.dirname(folder)]  # also accept picking TowerFall.app or Content/ itself
    content = refs = None
    for b in bases:
        for d in (b, os.path.join(b, "TowerFall.app", "Contents", "Resources"),
                  os.path.join(b, "Contents", "Resources"), os.path.join(b, "TowerFall.app", "Contents", "MacOS")):
            if content is None and os.path.isdir(os.path.join(d, "Content")):
                content = d
        for d in (os.path.join(b, "TowerFall.app", "Contents", "MacOS"), os.path.join(b, "Contents", "MacOS"),
                  b, os.path.join(b, "lib")):
            if refs is None and all(os.path.isfile(os.path.join(d, f)) for f in ("TowerFall.exe", "FNA.dll", "mscorlib.dll")):
                refs = d
    if content is None:
        return None, "No Content folder found here. Pick the TowerFall game folder."
    if refs is None:
        if any(os.path.isfile(os.path.join(b, "TowerFall.exe")) for b in bases):
            return None, ("This looks like the Windows (XNA) version. The port needs the macOS/Linux version "
                          "(TowerFall.exe next to FNA.dll and mscorlib.dll).")
        return None, "TowerFall.exe, FNA.dll and mscorlib.dll not found (the macOS/Linux FNA build is needed)."
    return {"content": content, "refs": refs,
            "darkworld": os.path.isdir(os.path.join(content, "DarkWorldContent"))}, None


# ------------------------------------------------------------------ tools

def _quiet(cmd, env=None):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=60, **NOWINDOW)
        return r.stdout + r.stderr
    except (OSError, subprocess.SubprocessError):
        return ""


def _which(name, dirs=()):
    for d in dirs:
        c = os.path.join(d, name + EXE)
        if os.path.isfile(c):
            return c
    p = shutil.which(name)
    if p:
        return p
    for d in ("/opt/homebrew/bin", "/usr/local/bin", "/usr/local/share/dotnet", os.path.expanduser("~/.dotnet"),
              "/Applications/CMake.app/Contents/bin", "C:\\Program Files\\dotnet", "C:\\Program Files\\CMake\\bin"):
        c = os.path.join(d, name + EXE)
        if os.path.isfile(c):
            return c
    return None


def dotnet_env(dotnet):
    env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1",
               DOTNET_SKIP_FIRST_TIME_EXPERIENCE="1")
    real = os.path.realpath(dotnet)
    for cand in (os.path.dirname(real), os.path.join(os.path.dirname(os.path.dirname(real)), "libexec")):
        if os.path.isdir(os.path.join(cand, "shared")):  # Homebrew keeps the runtime in libexec
            env["DOTNET_ROOT"] = cand
            break
    return env


def _dotnet_sdk_major(dn):
    sdks = re.findall(r"^(\d+)\.\d+\.\d+", _quiet([dn, "--list-sdks"], dotnet_env(dn)), re.M)
    return max((int(s) for s in sdks), default=0)


def find_dotnet():
    local = os.path.join(TOOLS, "dotnet", "dotnet" + EXE)
    for dn in (local, _which("dotnet")):
        if dn and os.path.isfile(dn):
            major = _dotnet_sdk_major(dn)
            if major >= DOTNET_MAJOR:
                return dn, f"SDK {major}"
    return None, "not installed (downloaded automatically)"


def find_cmake():
    local = glob.glob(os.path.join(TOOLS, "cmake", "*", "CMake.app", "Contents", "bin", "cmake")) + \
        glob.glob(os.path.join(TOOLS, "cmake", "*", "bin", "cmake" + EXE))
    cm = local[0] if local else _which("cmake")
    if not cm:
        return None, "not installed (downloaded automatically)"
    m = re.search(r"version (\d+)\.(\d+)\S*", _quiet([cm, "--version"]))
    if not m or (int(m.group(1)), int(m.group(2))) < (3, 16):
        return None, "too old (a newer one is downloaded automatically)"
    return cm, m.group(0).replace("version ", "")


def find_ninja():
    nj = _which("ninja", [os.path.join(TOOLS, "ninja")])
    return (nj, _quiet([nj, "--version"]).strip() or "found") if nj else (None, "not installed (downloaded automatically)")


def devkitpro_root():
    cands = []
    env = os.environ.get("DEVKITPRO")
    if env:
        if IS_WIN and env.startswith("/"):  # the Windows installer sets the MSYS2-style /opt/devkitpro
            env = os.environ.get("SystemDrive", "C:") + "\\devkitPro"
        cands.append(env)
    cands.append("C:\\devkitPro" if IS_WIN else "/opt/devkitpro")
    for c in cands:
        if os.path.isdir(c):
            return os.path.abspath(c)
    return None


def devkitpro_status():
    """Returns (root, has_wut, has_sdl)."""
    dkp = devkitpro_root()
    if not dkp:
        return None, False, False
    wut = os.path.isfile(os.path.join(dkp, "cmake", "WiiU.cmake")) and \
        os.path.isfile(os.path.join(dkp, "devkitPPC", "bin", "powerpc-eabi-gcc" + EXE)) and \
        os.path.isfile(os.path.join(dkp, "tools", "bin", "wuhbtool" + EXE))
    inc = os.path.join(dkp, "portlibs", "wiiu", "include", "SDL2")
    sdl = os.path.isfile(os.path.join(inc, "SDL.h")) and os.path.isfile(os.path.join(inc, "SDL_mixer.h"))
    return dkp, wut, sdl


def check_tools():
    """Returns rows for the tools list: dicts with key, name, ok, auto (installed by the builder), detail."""
    rows = []
    dn, d = find_dotnet()
    rows.append({"key": "dotnet", "name": f".NET SDK {DOTNET_MAJOR}", "ok": bool(dn), "auto": True, "detail": d})
    cm, d = find_cmake()
    rows.append({"key": "cmake", "name": "CMake", "ok": bool(cm), "auto": True, "detail": d})
    nj, d = find_ninja()
    rows.append({"key": "ninja", "name": "Ninja", "ok": bool(nj), "auto": True, "detail": d})
    dkp, wut, sdl = devkitpro_status()
    rows.append({"key": "devkitpro", "name": "devkitPro Wii U toolchain", "ok": wut, "auto": False,
                 "detail": ("installed" if wut else ("Wii U packages missing" if dkp else "not installed"))})
    rows.append({"key": "sdl", "name": "Wii U SDL2 libraries", "ok": sdl, "auto": False,
                 "detail": "installed" if sdl else "missing"})
    return rows


# ------------------------------------------------------------------ downloads / installers

def _ssl_context():
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def download(url, dest, log, progress=None, cancelled=lambda: False):
    log(f"Downloading {url}\n")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    try:
        return _download_urllib(url, dest, progress, cancelled)
    except Cancelled:
        raise
    except Exception as e:
        # e.g. a Python without access to the system's certificates: the system curl has them
        curl = shutil.which("curl")
        if not curl:
            raise
        log(f"({type(e).__name__}: {e}; trying curl)\n")
        r = subprocess.run([curl, "-fsSL", "-A", "TowerFall8P-Builder", "-o", dest, url], capture_output=True, text=True, **NOWINDOW)
        if r.returncode != 0:
            raise BuildError(f"Download failed: {url}\n{r.stderr.strip()}")
        return dest


def _download_urllib(url, dest, progress, cancelled):
    req = urllib.request.Request(url, headers={"User-Agent": "TowerFall8P-Builder"})
    tmp = dest + ".part"
    with urllib.request.urlopen(req, context=_ssl_context(), timeout=60) as r, open(tmp, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        got = 0
        while True:
            if cancelled():
                raise Cancelled()
            chunk = r.read(1 << 16)
            if not chunk:
                break
            f.write(chunk)
            got += len(chunk)
            if progress and total:
                progress(got / total)
    os.replace(tmp, dest)
    return dest


def _extract(archive, dest):
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                out = z.extract(info, dest)
                mode = info.external_attr >> 16
                if mode & 0o111:
                    os.chmod(out, os.stat(out).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    else:
        with tarfile.open(archive) as t:
            t.extractall(dest)
    os.remove(archive)


def _machine():
    import platform
    m = platform.machine().lower()
    return "arm64" if m in ("arm64", "aarch64") else "x64"


def install_dotnet(log, progress, cancelled, run):
    ext = ".ps1" if IS_WIN else ".sh"
    script = download("https://dot.net/v1/dotnet-install" + ext, os.path.join(TOOLS, "dotnet-install" + ext), log, None, cancelled)
    target = os.path.join(TOOLS, "dotnet")
    if IS_WIN:
        run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
             "-Channel", f"{DOTNET_MAJOR}.0", "-InstallDir", target, "-NoPath"])
    else:
        run(["bash", script, "--channel", f"{DOTNET_MAJOR}.0", "--install-dir", target, "--no-path"])


def install_cmake(log, progress, cancelled, run):
    base = f"https://github.com/Kitware/CMake/releases/download/v{CMAKE_VER}/cmake-{CMAKE_VER}-"
    if IS_WIN:
        name = "windows-arm64.zip" if _machine() == "arm64" else "windows-x86_64.zip"
    elif IS_MAC:
        name = "macos-universal.tar.gz"
    else:
        name = "linux-aarch64.tar.gz" if _machine() == "arm64" else "linux-x86_64.tar.gz"
    arc = download(base + name, os.path.join(TOOLS, "cmake-" + name), log, progress, cancelled)
    log("Unpacking CMake\n")
    _extract(arc, os.path.join(TOOLS, "cmake"))


def install_ninja(log, progress, cancelled, run):
    name = "ninja-win.zip" if IS_WIN else "ninja-mac.zip" if IS_MAC else \
        ("ninja-linux-aarch64.zip" if _machine() == "arm64" else "ninja-linux.zip")
    arc = download(f"https://github.com/ninja-build/ninja/releases/download/v{NINJA_VER}/{name}",
                   os.path.join(TOOLS, name), log, progress, cancelled)
    _extract(arc, os.path.join(TOOLS, "ninja"))
    nj = os.path.join(TOOLS, "ninja", "ninja" + EXE)
    if not IS_WIN:
        os.chmod(nj, 0o755)


def devkitpro_plan():
    """What install_devkitpro() will do next, as (step, user-facing explanation)."""
    dkp, wut, sdl = devkitpro_status()
    if wut and sdl:
        return "done", "devkitPro and the Wii U packages are installed."
    if not dkp or not _pacman(dkp):
        if IS_WIN:
            return "installer", ("The devkitPro installer will be downloaded and opened. In it, choose "
                                 "\"Wii U Development\" and finish the install, then click Install again "
                                 "to add the Wii U SDL2 packages.")
        if IS_MAC:
            return "installer", ("The devkitPro installer package will be downloaded and opened. Finish it "
                                 "(it asks for your password), then click Install again to add the Wii U packages.")
        return "manual", ("Install devkitPro's package manager from a terminal:\n\n"
                          "  wget https://apt.devkitpro.org/install-devkitpro-pacman\n"
                          "  chmod +x ./install-devkitpro-pacman\n"
                          "  sudo ./install-devkitpro-pacman\n\n"
                          "(For Arch and other distros see devkitpro.org/wiki/devkitPro_pacman.) "
                          "Then click Install again to add the Wii U packages.")
    who = "" if IS_WIN else " It asks for your password."
    return "packages", f"The Wii U packages ({', '.join(DKP_PACKAGES)}) will be installed with devkitPro's pacman.{who}"


def _pacman(dkp):
    if IS_WIN:
        p = os.path.join(dkp, "msys2", "usr", "bin", "pacman.exe")
    else:
        p = os.path.join(dkp, "pacman", "bin", "pacman")
        if not os.path.isfile(p):
            p = shutil.which("dkp-pacman") or "/usr/bin/dkp-pacman"
    return p if os.path.isfile(p) else None


def install_devkitpro(log, progress, cancelled, run):
    step, _ = devkitpro_plan()
    if step == "installer":
        if IS_WIN:
            api = download("https://api.github.com/repos/devkitPro/installer/releases/latest",
                           os.path.join(TOOLS, "devkitpro-release.json"), log, None, cancelled)
            with open(api, encoding="utf-8") as f:
                rel = json.load(f)
            assets = [a for a in rel.get("assets", []) if a["name"].lower().endswith(".exe")]
            if not assets:
                raise BuildError("Could not find the devkitPro installer download. Get it from " + URLS["devkitpro"])
            exe = download(assets[0]["browser_download_url"], os.path.join(TOOLS, assets[0]["name"]), log, progress, cancelled)
            log("Opening the devkitPro installer. Choose \"Wii U Development\".\n")
            os.startfile(exe)
        else:
            pkg = download("https://github.com/devkitPro/pacman/releases/latest/download/devkitpro-pacman-installer.pkg",
                           os.path.join(TOOLS, "devkitpro-pacman-installer.pkg"), log, progress, cancelled)
            log("Opening the devkitPro installer.\n")
            subprocess.Popen(["open", pkg])
        return "installer"
    if step == "packages":
        dkp = devkitpro_root()
        pac = _pacman(dkp)
        args = ["-Sy", "--needed", "--noconfirm"] + DKP_PACKAGES
        if IS_WIN:
            bash = os.path.join(dkp, "msys2", "usr", "bin", "bash.exe")
            run([bash, "-lc", "pacman " + " ".join(args)])
        elif IS_MAC:
            cmd = " ".join([pac] + args).replace("\\", "\\\\").replace('"', '\\"')
            run(["osascript", "-e", f'do shell script "{cmd}" with administrator privileges'])
        else:
            run(["pkexec", pac] + args)
        return "packages"
    return step


# ------------------------------------------------------------------ build

STEPS = [
    ("check", "Check game folder and tools"),
    ("tools", "Download missing build tools"),
    ("decompile", "Decompile your copy"),
    ("edits", "Apply the port's changes"),
    ("translate", "Translate C# to C++"),
    ("images", "Make Wii U icon and splash"),
    ("build", "Build the Wii U app"),
    ("stage", "Prepare SD card files"),
    ("sdcard", "Copy to SD card"),
]


class Builder:
    """Runs the steps. Callbacks: log(text), step(key, state) with state running/done/failed/skipped,
    progress(fraction or None) for the current step."""

    def __init__(self, game, sd=None, log=print, step=None, progress=None):
        self.game, self.sd = game, sd or None
        self.log = log
        self.step_cb = step or (lambda k, s: None)
        self.progress = progress or (lambda f: None)
        self.cancelled = False
        self.proc = None
        self.out = OUT

    def cancel(self):
        self.cancelled = True
        p = self.proc
        if p and p.poll() is None:
            try:
                p.terminate()
            except OSError:
                pass

    def _check_cancel(self):
        if self.cancelled:
            raise Cancelled()

    def run_cmd(self, cmd, cwd=None, env=None, percent=False):
        self._check_cancel()
        self.log("$ " + " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd) + "\n")
        os.makedirs(cwd or PORT, exist_ok=True)
        try:
            self.proc = subprocess.Popen([str(c) for c in cmd], cwd=cwd or PORT, env=env, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, **NOWINDOW)
        except OSError as e:
            raise BuildError(f"Could not start {cmd[0]}: {e}")
        for raw in iter(self.proc.stdout.readline, b""):
            line = raw.decode("utf-8", "replace")
            self.log(line)
            if percent:
                m = re.match(r"\[(\d+)/(\d+)\]", line)  # Ninja: [12/345]
                if m:
                    self.progress(int(m.group(1)) / max(1, int(m.group(2))))
        code = self.proc.wait()
        self.proc = None
        self._check_cancel()
        if code != 0:
            raise BuildError(f"{os.path.basename(str(cmd[0]))} failed (exit code {code}). See the log.")

    def install(self, fn):
        """Runs one of the install_* helpers with this builder's logging and cancel."""
        return fn(self.log, self.progress, lambda: self.cancelled, lambda c: self.run_cmd(c, cwd=APP_HOME))

    def run(self, steps=None):
        cur = None
        try:
            for key, title in STEPS:
                if steps and key not in steps:
                    continue
                cur = key
                if key == "sdcard" and not self.sd:
                    self.step_cb(key, "skipped")
                    continue
                self.step_cb(key, "running")
                self.progress(None)
                self.log(f"\n== {title}\n")
                getattr(self, "step_" + key)()
                self.step_cb(key, "done")
            self.log("\nAll done.\n")
            return True
        except Cancelled:
            self.step_cb(cur, "failed")
            self.log("\nCancelled.\n")
            raise
        except BuildError as e:
            self.step_cb(cur, "failed")
            self.log(f"\nERROR: {e}\n")
            raise
        except Exception as e:  # network errors, disk full, ...
            self.step_cb(cur, "failed")
            self.log(f"\nERROR: {type(e).__name__}: {e}\n")
            raise BuildError(str(e))

    # ---- steps

    def step_check(self):
        info, err = find_game(self.game)
        if err:
            raise BuildError(err)
        self.info = info
        self.log(f"Game data: {info['content']}\nGame assemblies: {info['refs']}\n")
        if not info["darkworld"]:
            self.log("warning: no DarkWorldContent folder next to Content; Dark World will be missing.\n")
        if self.sd and not os.path.isdir(self.sd):
            raise BuildError(f"SD card folder not found: {self.sd}")
        rows = check_tools()
        for t in rows:
            self.log(f"  {'ok     ' if t['ok'] else 'missing'}  {t['name']}: {t['detail']}\n")
        bad = [t["name"] for t in rows if not t["ok"] and not t["auto"]]
        if bad:
            raise BuildError("Install devkitPro first (the Install button next to it): " + ", ".join(bad) + " missing.")
        if FROZEN:
            self.log(f"Unpacking the port's source to {PORT}\n")
            for d in ("runtime", "tools", "wiiu"):
                shutil.copytree(os.path.join(BUNDLED_SRC, d), os.path.join(PORT, d), dirs_exist_ok=True)
            shutil.copy2(os.path.join(BUNDLED_SRC, "CMakeLists.txt"), os.path.join(PORT, "CMakeLists.txt"))

    def step_tools(self):
        for find, inst, name in ((find_dotnet, install_dotnet, ".NET SDK"), (find_cmake, install_cmake, "CMake"),
                                 (find_ninja, install_ninja, "Ninja")):
            if find()[0]:
                continue
            self.log(f"Getting {name}\n")
            self.install(inst)
            if not find()[0]:
                raise BuildError(f"{name} was downloaded but can't be used. See the log.")
        self.dotnet, self.cmake, self.ninja = find_dotnet()[0], find_cmake()[0], find_ninja()[0]
        self.denv = dotnet_env(self.dotnet)
        self.log(f".NET: {self.dotnet}\nCMake: {self.cmake}\nNinja: {self.ninja}\n")

    def step_decompile(self):
        ildir = os.path.join(TOOLS, "ilspy")
        ilspy = os.path.join(ildir, "ilspycmd" + EXE)
        if not os.path.isfile(ilspy):
            self.run_cmd([self.dotnet, "tool", "install", "ilspycmd", "--tool-path", ildir], cwd=APP_HOME, env=self.denv)
        env = dict(self.denv, DOTNET_ROLL_FORWARD="Major")  # let ilspycmd run on a newer runtime
        shutil.rmtree(os.path.join(PORT, "decomp"), ignore_errors=True)
        self.run_cmd([ilspy, "-p", "-o", os.path.join(PORT, "decomp"), os.path.join(self.info["refs"], "TowerFall.exe")], env=env)

    def step_edits(self):
        self.run_cmd(py_cmd(os.path.join(PORT, "tools", "port_edits.py"), os.path.join(PORT, "decomp"), os.path.join(PORT, "cs")))

    def step_translate(self):
        tool = os.path.join(PORT, "tools", "cs2cpp")
        self.run_cmd([self.dotnet, "build", "-c", "Release"], cwd=tool, env=self.denv)
        dlls = sorted(glob.glob(os.path.join(tool, "bin", "Release", "*", "cs2cpp.dll")))
        if not dlls:
            raise BuildError("cs2cpp.dll was not built")
        self.run_cmd([self.dotnet, dlls[-1], os.path.join(PORT, "cs"), os.path.join(PORT, "gen"), self.info["refs"]],
                     cwd=tool, env=self.denv)

    def step_images(self):
        self.run_cmd(py_cmd(os.path.join(PORT, "wiiu", "make_assets.py"), os.path.join(self.info["content"], "Content", "Atlas")))

    def step_build(self):
        dkp = devkitpro_root()
        fwd = lambda p: p.replace("\\", "/")  # CMake wants forward slashes, also on Windows
        env = dict(os.environ, DEVKITPRO=fwd(dkp), DEVKITPPC=fwd(dkp) + "/devkitPPC")
        extra = [os.path.join(dkp, "tools", "bin"), os.path.join(dkp, "devkitPPC", "bin")]
        env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
        bdir = os.path.join(PORT, "build", "wiiu-ninja")
        cache = os.path.join(bdir, "CMakeCache.txt")
        if os.path.isfile(cache):
            # a cache from another location or tool set makes CMake refuse to configure
            txt = open(cache, encoding="utf-8", errors="replace").read()
            home = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", txt, re.M)
            mk = re.search(r"^CMAKE_MAKE_PROGRAM:FILEPATH=(.*)$", txt, re.M)
            if (home and os.path.normcase(os.path.abspath(home.group(1).strip())) != os.path.normcase(PORT)) or \
                    (mk and os.path.normcase(mk.group(1).strip()) != os.path.normcase(fwd(self.ninja))):
                self.log("Starting a fresh build folder.\n")
                shutil.rmtree(bdir, ignore_errors=True)
        pycmd = ";".join(fwd(c) for c in py_cmd("")[:-1])  # CMake list: interpreter (and --py)
        self.run_cmd([self.cmake, "-S", PORT, "-B", bdir, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + fwd(self.ninja),
                      f"-DCMAKE_TOOLCHAIN_FILE={fwd(dkp)}/cmake/WiiU.cmake", "-DCMAKE_BUILD_TYPE=Release",
                      "-DTF_PYTHON_CMD=" + pycmd], env=env)
        self.progress(0.0)
        self.run_cmd([self.cmake, "--build", bdir], env=env, percent=True)
        self.wuhb = os.path.join(bdir, "towerfall.wuhb")
        if not os.path.isfile(self.wuhb):
            raise BuildError("The build finished but towerfall.wuhb is missing.")

    def step_stage(self):
        app = os.path.join(self.out, "wiiu", "apps", "towerfall")
        os.makedirs(app, exist_ok=True)
        shutil.copy2(self.wuhb, os.path.join(self.out, "wiiu", "apps", "towerfall.wuhb"))
        dirs = ["Content"] + (["DarkWorldContent"] if self.info["darkworld"] else [])
        for i, d in enumerate(dirs):
            self.mirror(os.path.join(self.info["content"], d), os.path.join(app, d), i / len(dirs), 1 / len(dirs))
        self.log(f"SD card files are in {self.out}\n")

    def step_sdcard(self):
        dst_apps = os.path.join(self.sd, "wiiu", "apps")
        os.makedirs(os.path.join(dst_apps, "towerfall"), exist_ok=True)
        self.log("Copying towerfall.wuhb\n")
        shutil.copyfile(os.path.join(self.out, "wiiu", "apps", "towerfall.wuhb"), os.path.join(dst_apps, "towerfall.wuhb"))
        src = os.path.join(self.out, "wiiu", "apps", "towerfall")
        dirs = [d for d in ("Content", "DarkWorldContent") if os.path.isdir(os.path.join(src, d))]
        # only these folders are mirrored: save/, controls.txt and other apps on the card are kept
        for i, d in enumerate(dirs):
            self.mirror(os.path.join(src, d), os.path.join(dst_apps, "towerfall", d), i / len(dirs), 1 / len(dirs))
        self.log(f"Copied to {self.sd}. Eject the card safely before removing it.\n")

    def mirror(self, src, dst, base=0.0, span=1.0):
        """Make dst an exact copy of src, copying only changed files (size or time differs)."""
        files = []
        for root, _, names in os.walk(src):
            for n in names:
                files.append(os.path.relpath(os.path.join(root, n), src))
        wanted = set(os.path.normcase(f) for f in files)
        copied = 0
        for i, rel in enumerate(files):
            self._check_cancel()
            s, d = os.path.join(src, rel), os.path.join(dst, rel)
            st = os.stat(s)
            try:
                dt = os.stat(d)
                same = dt.st_size == st.st_size and abs(dt.st_mtime - st.st_mtime) <= 2  # FAT keeps 2 s
            except OSError:
                same = False
            if not same:
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copy2(s, d)
                copied += 1
            if i % 25 == 0:
                self.progress(base + span * i / max(1, len(files)))
        for root, _, names in os.walk(dst, topdown=False):
            for n in names:
                p = os.path.join(root, n)
                if os.path.normcase(os.path.relpath(p, dst)) not in wanted:
                    os.remove(p)
            if root != dst and not os.listdir(root):
                os.rmdir(root)
        self.log(f"{os.path.basename(src)}: {len(files)} files, {copied} copied\n")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    b = Builder(argv[1], argv[2] if len(argv) > 2 else None, log=lambda t: (sys.stdout.write(t), sys.stdout.flush()))
    try:
        b.run()
    except (BuildError, Cancelled):
        return 1
    except KeyboardInterrupt:
        b.cancel()
        return 1
    if not b.sd:
        print(f"Copy everything inside {b.out} to the root of your SD card.")
    print("These files contain your game's data and artwork: keep them for personal use, don't share them.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
