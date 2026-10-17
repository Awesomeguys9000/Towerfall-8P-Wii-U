# TowerFall 8P: Wii U (Aroma) build

Homebrew port for Wii U consoles running the Aroma environment. It builds with devkitPro: devkitPPC, wut, `wiiu-sdl2` and `wiiu-sdl2_mixer`.

## Building

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC

cmake -S port -B port/build/wiiu \
      -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build port/build/wiiu -j8
```

The build produces these files:

- `port/build/wiiu/towerfall.rpx`: the raw executable.
- `port/build/wiiu/towerfall.wuhb`: the Aroma app bundle with its icon and splash screens. This is the file you install.

The packaging images (`icon.png` 128x128, `tv-splash.png` 1280x720, `drc-splash.png` 854x480) are in this folder. `make_assets.py` regenerates them using only the Python standard library:

```sh
python3 port/wiiu/make_assets.py            # uses ../../TF-8P/Content/Atlas if present
python3 port/wiiu/make_assets.py /path/to/Content/Atlas
```

When the game's `menuAtlas.png` is available, the script crops the TowerFall logo from it. Otherwise it draws a plain text logo. The images then contain game artwork, so keep the `.wuhb` for personal use.

## SD card layout

The `.wuhb` does **not** contain the game data (about 300 MB). Copy the data from your own copy of TowerFall (the 8-player PC/Mac version):

```
sd:/wiiu/apps/towerfall.wuhb                 <- the built bundle
sd:/wiiu/apps/towerfall/Content/...          <- from the original game folder
sd:/wiiu/apps/towerfall/DarkWorldContent/... <- from the original game folder
sd:/wiiu/apps/towerfall/save/                <- created automatically (saves/config)
sd:/wiiu/apps/towerfall/towerfall.log        <- created automatically (log of the last run)
```

You can also copy the whole original `TF-8P` folder unchanged, as `sd:/wiiu/apps/towerfall/TF-8P/Content/...`. The game looks there when `sd:/wiiu/apps/towerfall/Content` does not exist. The bundle can also live in its own folder (`sd:/wiiu/apps/towerfall/towerfall.wuhb`), because Aroma finds it there too.

Inside the app, the SD card is `fs:/vol/external01/`.

## Display

The game renders a 1280x720 frame. The image is mirrored on the TV and on the GamePad, and the GPU scales it to the TV mode (480p, 720p or 1080p) and to the 854x480 GamePad screen.

## HOME button

HOME opens the system HOME menu and the game pauses while it is open. "Close software" exits cleanly back to the Wii U Menu. If the game quits from its own menu, it also returns to the Wii U Menu.

## Logs

- The last run's log is written to `sd:/wiiu/apps/towerfall/towerfall.log`.
- With Aroma's logging module, output also goes to the network over UDP port 4405. Read it on a PC with `udplogserver` from wut-tools, or with `nc -ul 4405`.
- Without the module, the game sends UDP logs itself.

## Controllers

There are 8 player slots:

| Slot | Device |
|------|--------|
| 1 | Wii U GamePad |
| 2-8 | Wii Remote channels 1-7: Wii Remote, Wii Remote + Nunchuk, Classic Controller (Pro), Wii U Pro Controller |

Controllers can connect and disconnect at any time. Bloopair controllers (Switch Pro, DualShock 4, DualSense, ...) show up as Wii U Pro Controllers.

In-game actions: **Jump** (also menu confirm), **Shoot** (also menu back), **Dodge**, **Toggle arrows**, **Pause/Start**, and **Move/Aim**.

### Wii Remote alone, held sideways (D-pad on the left)

| Wii Remote | Action | XNA button |
|---|---|---|
| D-pad (rotated: Up=left, Down=right, Left=down, Right=up) | Move / aim | DPad |
| 1 | Jump / confirm | A |
| 2 | Shoot / back | X |
| B (trigger) | Dodge | RightShoulder |
| A, Minus | Toggle arrows | Y |
| Plus | Pause / start | Start |
| HOME | System HOME menu | - |

### Wii Remote + Nunchuk (remote held upright)

| Button | Action | XNA button |
|---|---|---|
| Nunchuk stick | Move / aim | Left stick |
| Wii Remote D-pad | Move / aim | DPad |
| A | Jump / confirm | A |
| B (trigger) | Shoot / back | X |
| Z | Dodge | RightShoulder |
| C | Toggle arrows | Y |
| Plus | Pause / start | Start |
| Minus | Back | Back |

### GamePad, Pro Controller, Classic Controller, Bloopair pads (by button label)

| Button | Action |
|---|---|
| A | Jump / confirm |
| B | Alt shoot / back |
| Y | Shoot (also back in menus) |
| X | Toggle arrows |
| L / ZL | Dodge (menu: left page) |
| R / ZR | Dodge (menu: right page) |
| Plus / Minus | Start / Back |
| Left stick, D-pad | Move / aim |

### Remapping

On first launch the game writes `sd:/wiiu/apps/towerfall/controls.txt` with every mapping
(`pad.*`, `wiimote.*`, `nunchuk.*`). Edit it and restart the game. Actions: `jump`, `shoot`,
`altshoot`, `dodge`, `dodge_l`, `arrows`, `start`, `back`, `none`. (On-screen button icons
show the default layout.)

### Pairing controllers in-game

- Press the console's **SYNC** button, then the red sync button on the controller.
- Or hold **Minus on the GamePad for 2 seconds** to open the system "Sync controllers" screen.

Rumble works on the GamePad and on Wii Remotes. Pro Controllers and Bloopair pads get the same on/off rumble command, but rumble on them has not been tested.
