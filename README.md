# wkFastDraw

Makes **Worms Armageddon 3.8.1** draw its map about two to three times faster. The picture stays the same.

WA draws everything on the CPU, on one thread. Most of that time goes into one small routine that copies the land, the sky
and the background onto the screen one pixel at a time. wkFastDraw does that copy 16 pixels at a time (SSE2) and changes
nothing else: no game rules, no files, no settings, no graphics card code.

It is a WormKit module: one file, `wkFastDraw.dll`, with nothing to set up.

## How much faster

Frames a second on one desktop PC (Zen 2 CPU), replays in a window, Direct3D 9 (shader), vsync off:

| | WA 3.8.1 | with wkFastDraw |
|---|---|---|
| 1920x1080, lowest detail | 830 | 1400 to 1650 |
| 1920x1080, full detail | 480 | 1220 |
| 1920x1080, full detail, a match full of explosions | 320 | 1100 |
| 2560x1440, full detail | 330 | 820 |
| 3840x2160, full detail | 210 | 400 |

Results vary by about 20% from run to run, and other PCs will differ. With vsync on, the frame rate stays your monitor's
and WA uses less CPU instead: at 120 Hz and 1080p full detail its main thread went from 33% to 17% of one core.

Where it doesn't help:

- A map zoomed by [wkMapZoom](https://github.com/GermsWorms/wkMapZoom) or wkVista: those modules draw the zoomed map
  themselves. Both run fine with wkFastDraw.
- Pauses that aren't drawing: many crates landing on a large map, for example, is the game's own work.
- WA's old 8-bit DirectDraw renderer with hardware rendering (Windows 7 and older), which doesn't use this routine.

## Install

1. Download [`release/wkFastDraw.dll`](release/wkFastDraw.dll) and put it in the WA folder, next to `WA.exe`.
2. In WA: Options > Advanced > turn on "Load WormKit modules", and restart WA.

To remove it, delete the file while WA is closed. `install.ps1` (and `install.ps1 -Remove`) does the same.

It works with Steam's and GOG's 3.8.1. On anything else it does nothing. What it did is in
`%LOCALAPPDATA%\wkFastDraw\wkFastDraw.log`.

## Online games and replays

Only your own picture is drawn faster. The game's rules, its random numbers and what is sent to other players are not
touched, so online games and replays should stay in step and nobody else needs the module. (It has been tested on
replays, not yet in a live online game.) As with every WormKit module, WA notes a scrambled checksum of the file in
replays and sends it to the other players; WA does nothing with it.

## How it was checked

- **Against WA's own code.** The tests run WA's two loops (taken from your `WA.exe` when the tests run) and the module's
  on the same 60,000 made-up pictures and compare every byte, the stack and WA's variables.
- **In the game.** A check build did every 16th copy with plain loops too: 1.1 million copies in two matches, none
  different.
- **Whole pictures.** 968 frames saved with WA's Export Video (eight stretches of seven replays, 640x480 to 3840x2160),
  with and without the module: the map and everything in play are the same. What differs is what WA moves by the real
  clock, not the game's: the chat phone, the energy bars sliding in, and the drifting background stars.
- **Independent review** of the machine code, the loops, the loading and WA's behaviour.

## Other modules

Started together with wkFastDraw, each drawing a replay for about 20 seconds with no crash: wkMapZoom 4.3, WormForge
0.9.9, wkVista 0.1.4, wkRGBA, wkZoom 0.2.0, wkJellyWorm (3.8.1 v1), wkRealTime 0.0.9.2, wkTerrainSync 1.2.6.1,
wkTrackMeBetter 3.8.1, wkWall2Wall, wkPin 1.0.0.8, wkVolumeControl, wkSuperFrontend and wkRemapKeys, and nine of them all
at once. Not tested: wkBindKeys and wkWormNAT2 (they need other files to start).

If another module has changed the same routine, wkFastDraw leaves WA alone and says whose it is in the log.

## Good to know

- With vsync **off** you get more frames a second. WA measures the mouse flick that takes over the camera per drawn
  frame, so at very high frame rates that flick has to be quicker. Holding the left mouse button, Scroll Lock, WA's
  `CameraUnlockMouseSpeed` setting or vsync all get around it.
- Untested so far: real weak laptops, fullscreen, Windows 7/8/10 and Wine.

## Build

`build.ps1` needs 32-bit MinGW gcc. It runs the tests, then makes `wkFastDraw.dll`.

## License

[MIT](LICENSE). Worms Armageddon is Team17's; this project is not affiliated with Team17.
