#!/usr/bin/env python3
"""Apply the port's edits to a fresh ILSpy decompile of TowerFall (8-player build).

usage: port_edits.py <decompiled dir> <output cs dir>

The edits are small: remove desktop-only features (GIF export, editor file dialog, SDL message
boxes, web links), route the save path through Native, add a gameplay-layer for gamepads, add
Wii U controller icon configs and skip the keyboard player on Wii U. Port/Native.cs (ours) is
copied in from tools/port_cs/.
"""
import os
import shutil
import sys

REMOVE = ["XNAFileDialog.cs", "Moments.Encoder", "Properties", "TowerFall.csproj", "app.ico",
          "TowerFall/GifExporter.cs", "TowerFall/GifExportOptions.cs"]


def edit(root, rel, fn):
    p = os.path.join(root, rel)
    s = open(p, encoding="utf-8").read()
    s2 = fn(s)
    open(p, "w", encoding="utf-8").write(s2)


def replace_once(s, old, new, what):
    if old not in s:
        sys.exit(f"port_edits: anchor not found ({what}) - is this the TowerFall 8P build?")
    return s.replace(old, new, 1)


def tfgame(s):
    s = replace_once(s, "using SDL2;\n", "", "TFGame using SDL2")
    s = replace_once(s, "\tpublic static void Log(Exception e, bool whileLoading)\n\t{",
                     "\tpublic static void Log(Exception e, bool whileLoading)\n\t{\n\t\tConsole.WriteLine(\"EXCEPTION: \" + e.ToString());",
                     "TFGame.Log")
    a = s.index("\tpublic static void OpenLog()\n\t{")
    b = s.index("\n\t}", a)
    s = s[:a] + "\tpublic static void OpenLog()\n\t{\n" + s[b:]
    a = s.index("\t\tcatch (NoSuitableGraphicsDeviceException)")
    b = s.index("\t\tcatch (Exception e)", a)
    s = s[:a] + s[b:]
    s = replace_once(s, "\t\tXNAFileDialog.GraphicsDevice = Engine.Instance.GraphicsDevice;\n", "", "XNAFileDialog")
    a = s.index("\tprivate static string GetSavePath()\n\t{")
    b = s.index("\tpublic static bool OpenStoreDarkWorldDLC()")
    s = s[:a] + "\tprivate static string GetSavePath()\n\t{\n\t\treturn Native.GetSavePath();\n\t}\n\n" + s[b:]
    s = replace_once(s, "\t\tProcess.Start(\"http://mattmakesgames.itch.io/towerfall-dark-world\");\n", "", "store link")
    return s


def player_input(s):
    return replace_once(s, "\t\tTFGame.WriteToLoadLog(\"2...\");\n\t\tif (num < 8)\n",
                        "\t\tTFGame.WriteToLoadLog(\"2...\");\n\t\tif (num < 8 && Native.GetPlatform() != \"Wii U\")\n", "AssignInputs")


def round_results(s):
    return replace_once(s, "Scene.Add(new GifExporter(Level.ReplayRecorder.Data, FinishSaveReplay));",
                        "FinishSaveReplay(completed: false);", "GifExporter")


def minput(s):
    s = replace_once(s, "\t\tinternal void Update(bool focus)\n\t\t{\n\t\t\tPreviousState = CurrentState;\n",
                     "\t\tprivate uint gameCurrent;\n\n\t\tprivate uint gamePrevious;\n\n"
                     "\t\tpublic bool GameCheck(Buttons button)\n\t\t{\n\t\t\treturn (gameCurrent & (uint)button) != 0;\n\t\t}\n\n"
                     "\t\tpublic bool GamePressed(Buttons button)\n\t\t{\n\t\t\treturn (gameCurrent & (uint)button) != 0 && (gamePrevious & (uint)button) == 0;\n\t\t}\n\n"
                     "\t\tinternal void Update(bool focus)\n\t\t{\n\t\t\tPreviousState = CurrentState;\n\t\t\tgamePrevious = gameCurrent;\n",
                     "XGamepadData.Update")
    s = replace_once(s, "\t\t\t\tCurrentState = GamePad.GetState(PlayerIndex);\n",
                     "\t\t\t\tCurrentState = GamePad.GetState(PlayerIndex);\n\t\t\t\tgameCurrent = TowerFall.Native.GamePadGameButtons((int)PlayerIndex);\n",
                     "XGamepadData focus")
    s = replace_once(s, "\t\t\telse\n\t\t\t{\n\t\t\t\tCurrentState = default;\n\t\t\t}",
                     "\t\t\telse\n\t\t\t{\n\t\t\t\tCurrentState = default;\n\t\t\t\tgameCurrent = 0u;\n\t\t\t}", "XGamepadData nofocus")
    return s


BUTTONLISTS = '\\t<!-- Wii U: sideways Wii Remote -->\\n\\t<buttonlist>\\n\\t\\t<name>WiiRemote</name>\\n\\t\\t<player>generic/player</player>\\n\\t\\t<move>generic/dpad</move>\\n\\t\\t<jump>generic/1</jump>\\n\\t\\t<shoot>generic/2</shoot>\\n\\t\\t<dodge>generic/shoulder6</dodge>\\n\\t\\t<start>generic/start</start>\\n\\t\\t<confirm>generic/1</confirm>\\n\\t\\t<back>generic/2</back>\\n\\t\\t<alt>generic/shoulder6</alt>\\n\\t\\t<alt2>noButton</alt2>\\n\\t\\t<skip_replay>generic/start</skip_replay>\\n\\t</buttonlist>\\n\\t<!-- Wii U: Wii Remote + Nunchuk -->\\n\\t<buttonlist>\\n\\t\\t<name>Nunchuk</name>\\n\\t\\t<player>generic/player</player>\\n\\t\\t<move>generic/stick</move>\\n\\t\\t<jump>snes/a</jump>\\n\\t\\t<shoot>snes/b</shoot>\\n\\t\\t<dodge>generic/shoulder6</dodge>\\n\\t\\t<start>generic/start</start>\\n\\t\\t<confirm>snes/a</confirm>\\n\\t\\t<back>snes/b</back>\\n\\t\\t<alt>generic/shoulder6</alt>\\n\\t\\t<alt2>snes/select</alt2>\\n\\t\\t<skip_replay>generic/start</skip_replay>\\n\\t</buttonlist>\\n\\t<!-- Wii U: GamePad, Pro Controller, Classic Controller (Nintendo button labels) -->\\n\\t<buttonlist>\\n\\t\\t<name>Nintendo</name>\\n\\t\\t<player>snes/player</player>\\n\\t\\t<move>snes/dpad</move>\\n\\t\\t<jump>snes/b</jump>\\n\\t\\t<shoot>snes/a</shoot>\\n\\t\\t<dodge>snes/r</dodge>\\n\\t\\t<start>snes/start</start>\\n\\t\\t<confirm>snes/a</confirm>\\n\\t\\t<back>snes/b</back>\\n\\t\\t<alt>snes/r</alt>\\n\\t\\t<alt2>snes/l</alt2>\\n\\t\\t<skip_replay>snes/start</skip_replay>\\n\\t</buttonlist>\\n'
CONTROLLERS = '\\t<!-- Wii U controllers -->\\n\\t<controller>\\n\\t\\t<name>Wii U GamePad</name>\\n\\t\\t<guid>wiiu_gamepad</guid>\\n\\t\\t<buttonset>Nintendo</buttonset>\\n\\t</controller>\\n\\t<controller>\\n\\t\\t<name>Pro Controller</name>\\n\\t\\t<guid>procontroller</guid>\\n\\t\\t<buttonset>Nintendo</buttonset>\\n\\t</controller>\\n\\t<controller>\\n\\t\\t<name>Classic Controller</name>\\n\\t\\t<guid>classic</guid>\\n\\t\\t<buttonset>Nintendo</buttonset>\\n\\t</controller>\\n\\t<controller>\\n\\t\\t<name>Wii Remote</name>\\n\\t\\t<guid>wiimote</guid>\\n\\t\\t<buttonset>WiiRemote</buttonset>\\n\\t</controller>\\n\\t<controller>\\n\\t\\t<name>Wii Remote + Nunchuk</name>\\n\\t\\t<guid>nunchuk</guid>\\n\\t\\t<buttonset>Nunchuk</buttonset>\\n\\t</controller>\\n'


def xgamepad(s):
    s = replace_once(s, r"\t<!-- Users can add buttonlists here. -->", BUTTONLISTS + r"\t<!-- Users can add buttonlists here. -->", "buttonlists")
    s = replace_once(s, r"\t<!-- Users can add controllers here. -->", CONTROLLERS + r"\t<!-- Users can add controllers here. -->", "controllers")
    a = s.index("\tpublic override InputState GetState()")
    b = s.index("\tpublic override void Rumble(", a)
    body = s[a:b]
    for t in ("Left", "Right"):
        body = body.replace(f"xGamepad.{t}TriggerCheck(0.1f)", f"xGamepad.GameCheck(Buttons.{t}Trigger)")
        body = body.replace(f"xGamepad.{t}TriggerPressed(0.1f)", f"xGamepad.GamePressed(Buttons.{t}Trigger)")
    body = body.replace("xGamepad.Check(Buttons.", "xGamepad.GameCheck(Buttons.").replace("xGamepad.Pressed(Buttons.", "xGamepad.GamePressed(Buttons.")
    return s[:a] + body + s[b:]


def main():
    src, dst = sys.argv[1], sys.argv[2]
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    for r in REMOVE:
        p = os.path.join(dst, r)
        if os.path.isdir(p):
            shutil.rmtree(p)
        elif os.path.exists(p):
            os.remove(p)
    edit(dst, "TowerFall/TFGame.cs", tfgame)
    edit(dst, "TowerFall/PlayerInput.cs", player_input)
    edit(dst, "TowerFall/VersusRoundResults.cs", round_results)
    edit(dst, "Monocle/MInput.cs", minput)
    edit(dst, "TowerFall/XGamepadInput.cs", xgamepad)
    here = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.join(dst, "Port"), exist_ok=True)
    shutil.copy(os.path.join(here, "port_cs", "Native.cs"), os.path.join(dst, "Port", "Native.cs"))
    print(f"port_edits: wrote {dst}")


if __name__ == "__main__":
    main()
