"""TowerFall 8P Wii U Builder: a small window around pipeline.py.

Run from the repository with `python3 builder/app.py`, or use the bundled .exe / .app from the
GitHub releases (Python included). `app --cli GAME [SD]` runs without a window.
"""
import json
import os
import queue
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pipeline as P  # noqa: E402

SETTINGS = os.path.join(P.APP_HOME, "settings.json")
TITLE = "TowerFall 8P Wii U Builder"


def load_settings():
    try:
        with open(SETTINGS, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_settings(d):
    try:
        os.makedirs(P.APP_HOME, exist_ok=True)
        with open(SETTINGS, "w", encoding="utf-8") as f:
            json.dump(d, f, indent=1)
    except OSError:
        pass


def run_gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    root = tk.Tk()
    root.title(TITLE)
    root.minsize(640, 600)
    settings = load_settings()
    q = queue.Queue()          # worker thread -> UI: (kind, payload)
    state = {"worker": None, "builder": None}

    style = ttk.Style()
    if P.IS_WIN:
        style.theme_use("vista")
    elif not P.IS_MAC:
        style.theme_use("clam")
    style.configure("Head.TLabel", font=("TkDefaultFont", 15, "bold"))
    style.configure("Big.TButton", padding=(18, 8))
    GOOD, BAD, MUTED = "#1a7f37", "#cf222e", "#6e7781"

    main = ttk.Frame(root, padding=16)
    main.pack(fill="both", expand=True)
    main.columnconfigure(0, weight=1)

    ttk.Label(main, text=TITLE, style="Head.TLabel").grid(row=0, column=0, sticky="w")
    ttk.Label(main, text="Builds the Wii U port from your own copy of TowerFall (8-player, Mac/Linux version) "
                         "and puts it on an SD card.", wraplength=600, foreground=MUTED).grid(row=1, column=0, sticky="w", pady=(2, 12))

    # ---- 1. game folder
    gbox = ttk.LabelFrame(main, text=" 1. Your TowerFall folder ", padding=10)
    gbox.grid(row=2, column=0, sticky="ew")
    gbox.columnconfigure(0, weight=1)
    game_var = tk.StringVar(value=settings.get("game", ""))
    ttk.Entry(gbox, textvariable=game_var).grid(row=0, column=0, sticky="ew")
    game_msg = ttk.Label(gbox, text="", wraplength=560)
    game_msg.grid(row=1, column=0, columnspan=2, sticky="w", pady=(6, 0))

    def pick_game():
        d = filedialog.askdirectory(title="Choose your TowerFall folder", initialdir=game_var.get() or os.path.expanduser("~"))
        if d:
            game_var.set(d)
    ttk.Button(gbox, text="Choose…", command=pick_game).grid(row=0, column=1, padx=(8, 0))

    def check_game(*_):
        info, err = P.find_game(game_var.get().strip())
        if err:
            game_msg.configure(text=err, foreground=BAD if game_var.get().strip() else MUTED)
        else:
            extra = "" if info["darkworld"] else " (no DarkWorldContent: Dark World will be missing)"
            game_msg.configure(text="✓ Found TowerFall (Mac/Linux version)" + extra, foreground=GOOD)
        update_buttons()
        return info
    game_var.trace_add("write", check_game)

    # ---- 2. tools
    tbox = ttk.LabelFrame(main, text=" 2. Build tools ", padding=10)
    tbox.grid(row=3, column=0, sticky="ew", pady=(10, 0))
    tbox.columnconfigure(1, weight=1)
    tool_rows = {}
    for i, key in enumerate(["dotnet", "cmake", "ninja", "devkitpro", "sdl"]):
        icon = ttk.Label(tbox, text="…", width=2)
        icon.grid(row=i, column=0, sticky="w")
        name = ttk.Label(tbox, text="")
        name.grid(row=i, column=1, sticky="w")
        detail = ttk.Label(tbox, text="", foreground=MUTED)
        detail.grid(row=i, column=2, sticky="e", padx=(8, 0))
        tool_rows[key] = (icon, name, detail)
    trow = ttk.Frame(tbox)
    trow.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(8, 0))
    trow.columnconfigure(0, weight=1)
    tools_msg = ttk.Label(trow, text=".NET, CMake and Ninja are downloaded automatically when you build.",
                          foreground=MUTED, wraplength=420)
    tools_msg.grid(row=0, column=0, sticky="w")
    dkp_btn = ttk.Button(trow, text="Install devkitPro…")
    dkp_btn.grid(row=0, column=1, padx=(8, 0))
    ttk.Button(trow, text="Check again", command=lambda: refresh_tools()).grid(row=0, column=2, padx=(8, 0))
    tools_ok = {"dkp": False}

    def refresh_tools():
        rows = P.check_tools()
        for r in rows:
            icon, name, detail = tool_rows[r["key"]]
            name.configure(text=r["name"])
            if r["ok"]:
                icon.configure(text="✓", foreground=GOOD)
            elif r["auto"]:
                icon.configure(text="↓", foreground=MUTED)
            else:
                icon.configure(text="✗", foreground=BAD)
            detail.configure(text=r["detail"])
        tools_ok["dkp"] = all(r["ok"] for r in rows if not r["auto"])
        dkp_btn.grid() if not tools_ok["dkp"] else dkp_btn.grid_remove()
        update_buttons()

    # ---- 3. SD card
    sbox = ttk.LabelFrame(main, text=" 3. SD card (optional) ", padding=10)
    sbox.grid(row=4, column=0, sticky="ew", pady=(10, 0))
    sbox.columnconfigure(1, weight=1)
    sd_on = tk.BooleanVar(value=settings.get("sd_on", False))
    sd_var = tk.StringVar(value=settings.get("sd", ""))
    ttk.Checkbutton(sbox, text="Copy to SD card:", variable=sd_on, command=lambda: update_buttons()).grid(row=0, column=0, sticky="w")
    sd_entry = ttk.Entry(sbox, textvariable=sd_var)
    sd_entry.grid(row=0, column=1, sticky="ew", padx=(6, 0))

    def pick_sd():
        d = filedialog.askdirectory(title="Choose your SD card", initialdir=sd_var.get() or ("/Volumes" if P.IS_MAC else "/"))
        if d:
            sd_var.set(d)
            sd_on.set(True)
            update_buttons()
    ttk.Button(sbox, text="Choose…", command=pick_sd).grid(row=0, column=2, padx=(8, 0))
    ttk.Label(sbox, text="Your saves and controls.txt on the card are kept. Without a card, the files are "
                         "prepared in a folder for you to copy.", foreground=MUTED, wraplength=560).grid(
        row=1, column=0, columnspan=3, sticky="w", pady=(6, 0))

    # ---- build controls and progress
    bbar = ttk.Frame(main)
    bbar.grid(row=5, column=0, sticky="ew", pady=(14, 0))
    bbar.columnconfigure(2, weight=1)
    build_btn = ttk.Button(bbar, text="Build", style="Big.TButton", command=lambda: start_build())
    build_btn.grid(row=0, column=0)
    cancel_btn = ttk.Button(bbar, text="Cancel", command=lambda: state["builder"] and state["builder"].cancel())
    cancel_btn.grid(row=0, column=1, padx=(8, 0))
    open_btn = ttk.Button(bbar, text="Open SD card files", command=lambda: P.open_path(P.OUT))
    open_btn.grid(row=0, column=3)
    status = ttk.Label(main, text="", wraplength=600)
    status.grid(row=6, column=0, sticky="w", pady=(10, 2))
    bar = ttk.Progressbar(main, mode="determinate", maximum=1.0)
    bar.grid(row=7, column=0, sticky="ew")

    logf = ttk.Frame(main)
    logf.grid(row=8, column=0, sticky="nsew", pady=(10, 0))
    main.rowconfigure(8, weight=1)
    logf.columnconfigure(0, weight=1)
    logf.rowconfigure(0, weight=1)
    log = tk.Text(logf, height=10, wrap="none", font=("Menlo" if P.IS_MAC else "Consolas" if P.IS_WIN else "Monospace", 10))
    log.grid(row=0, column=0, sticky="nsew")
    sb = ttk.Scrollbar(logf, command=log.yview)
    sb.grid(row=0, column=1, sticky="ns")
    log.configure(yscrollcommand=sb.set, state="disabled")

    def append_log(text):
        log.configure(state="normal")
        log.insert("end", text)
        lines = int(log.index("end-1c").split(".")[0])
        if lines > 4000:
            log.delete("1.0", f"{lines - 4000}.0")
        log.see("end")
        log.configure(state="disabled")

    def busy():
        return state["worker"] is not None and state["worker"].is_alive()

    def update_buttons():
        b = busy()
        ok = P.find_game(game_var.get().strip())[1] is None and tools_ok["dkp"] and (not sd_on.get() or os.path.isdir(sd_var.get()))
        build_btn.configure(state="normal" if ok and not b else "disabled")
        cancel_btn.configure(state="normal" if b else "disabled")
        dkp_btn.configure(state="disabled" if b else "normal")
        open_btn.configure(state="normal" if os.path.isdir(P.OUT) and not b else "disabled")
        sd_entry.configure(state="normal" if sd_on.get() else "disabled")

    titles = dict(P.STEPS)

    def run_worker(target):
        def body():
            try:
                q.put(("result", target()))
            except P.Cancelled:
                q.put(("result", "cancelled"))
            except Exception as e:  # BuildError and anything unexpected
                q.put(("error", str(e)))
        state["worker"] = threading.Thread(target=body, daemon=True)
        state["worker"].start()
        update_buttons()

    def make_builder(sd):
        return P.Builder(game_var.get().strip(), sd,
                         log=lambda t: q.put(("log", t)),
                         step=lambda k, s: q.put(("step", (k, s))),
                         progress=lambda f: q.put(("progress", f)))

    def start_build():
        sd = sd_var.get().strip() if sd_on.get() else None
        save_settings({"game": game_var.get().strip(), "sd": sd_var.get().strip(), "sd_on": sd_on.get()})
        b = make_builder(sd)
        state["builder"] = b
        state["mode"] = "build"
        status.configure(text="Starting…", foreground="")
        run_worker(b.run)

    def install_dkp():
        step, text = P.devkitpro_plan()
        if step == "done":
            refresh_tools()
            return
        if step == "manual":
            messagebox.showinfo("Install devkitPro", text)
            return
        if not messagebox.askokcancel("Install devkitPro", text):
            return
        b = make_builder(None)
        state["builder"] = b
        state["mode"] = "dkp"
        status.configure(text="Installing devkitPro…", foreground="")
        run_worker(lambda: b.install(P.install_devkitpro))
    dkp_btn.configure(command=install_dkp)

    def poll():
        try:
            while True:
                kind, val = q.get_nowait()
                if kind == "log":
                    append_log(val)
                elif kind == "step":
                    k, s = val
                    if s == "running":
                        n = [x[0] for x in P.STEPS].index(k) + 1
                        status.configure(text=f"Step {n} of {len(P.STEPS)}: {titles[k]}…", foreground="")
                elif kind == "progress":
                    if val is None:
                        bar.configure(mode="indeterminate")
                        bar.start(15)
                    else:
                        bar.stop()
                        bar.configure(mode="determinate", value=val)
                elif kind in ("result", "error"):
                    bar.stop()
                    bar.configure(mode="determinate", value=0)
                    state["worker"] = None
                    if kind == "error":
                        status.configure(text="Failed: " + val, foreground=BAD)
                        messagebox.showerror(TITLE, val + "\n\nThe log at the bottom has the details.")
                    elif val == "cancelled":
                        status.configure(text="Cancelled.", foreground=MUTED)
                    elif state.get("mode") == "dkp":
                        if val == "installer":
                            status.configure(text="Finish the devkitPro installer, then click Install devkitPro again.", foreground="")
                        else:
                            status.configure(text="devkitPro packages installed.", foreground=GOOD)
                        refresh_tools()
                    else:
                        bar.configure(value=1.0)
                        b = state["builder"]
                        where = f"copied to {b.sd}" if b.sd else f"ready in {P.OUT}"
                        status.configure(text=f"Done! The SD card files are {where}.", foreground=GOOD)
                        msg = ("Done! Eject the SD card, put it in your Wii U and launch TowerFall from the Wii U Menu."
                               if b.sd else "Done! Copy everything inside the folder that opens to the root of your SD card.")
                        messagebox.showinfo(TITLE, msg)
                        if not b.sd:
                            P.open_path(P.OUT)
                    update_buttons()
        except queue.Empty:
            pass
        root.after(50, poll)

    def on_close():
        if busy():
            if not messagebox.askyesno(TITLE, "A build is running. Stop it and quit?"):
                return
            state["builder"].cancel()
        root.destroy()
    root.protocol("WM_DELETE_WINDOW", on_close)

    refresh_tools()
    check_game()
    append_log(f"Work folder: {P.APP_HOME}\n")
    poll()
    root.mainloop()


def main():
    args = sys.argv[1:]
    if args[:1] == ["--py"]:          # the bundled app runs the port's Python scripts itself
        P.run_py_script(args[1:])
        return 0
    if args[:1] == ["--cli"]:
        return P.main([sys.argv[0]] + args[1:])
    try:
        run_gui()
    except ImportError as e:
        print(f"Tkinter is not available ({e}). Install it (e.g. 'sudo apt install python3-tk' or "
              "'brew install python-tk'), or run without a window:\n  python3 builder/app.py --cli GAME [SD]")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
