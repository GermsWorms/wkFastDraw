# wkFastDraw

Makes **Worms Armageddon 3.8.1** draw its map about two to three times faster. The picture stays the same.

It is a WormKit module: one file, `wkFastDraw.dll`, with nothing to set up.

## How much faster

Frames a second on one desktop PC, 3.8.1 with the Direct3D 9 (shader) renderer, vsync off:

| | WA 3.8.1 | with wkFastDraw |
|---|---|---|
| 1920x1080, lowest detail | 830 | 1400 to 1650 |
| 1920x1080, full detail | 480 | 1220 |
| 1920x1080, full detail, a match full of explosions | 320 | 1100 |
| 2560x1440, full detail | 330 | 820 |
| 3840x2160, full detail | 210 | 400 |

Your numbers will differ with your PC, your detail level and the map. With vsync on, the frame rate stays your monitor's and
WA uses less of your CPU instead: at 120 Hz and full detail, WA's main thread went from 33% to 17% of one core.

## How it works

WA 3.8.1 is drawn by the CPU, not the graphics card. Every frame, one thread puts the whole picture together in memory, one
byte a pixel (a number from 0 to 255: the colour's place in the palette). The graphics card only gets the finished picture
and turns those numbers into colours.

Most of that work is copying. The land is kept in tiles, the sky in strips 64 pixels wide, and the back layers in tiles too,
and all of it is copied onto the picture again every frame, about two million pixels at 1080p. One small routine in WA does
all of those copies, and it takes 45 to 75% of that thread's time:

- **Pieces with see-through parts** (land, back layers) are copied one pixel at a time: read a pixel, is it the see-through
  colour, if not write it, next pixel.
- **Solid pieces** (the sky) are copied a row at a time with a function call for each row. The sky's strips are only 64 pixels
  wide, so that is tens of thousands of calls a frame.

wkFastDraw replaces those two loops with ones that use SSE2, processor instructions every PC of the last 20 years has, which
handle 16 pixels at once:

1. Load 16 pixels of the piece.
2. Compare all 16 with the see-through colour in one step. That gives a mask of which to skip.
3. Skip all 16 (an empty part), write all 16 (a solid part), or mix them with what is there already (an edge).
4. Solid rows are simply written 16 pixels at a time, with no call per row.

The result is the same bytes in the same places, only sooner. Everything else is still WA's own code: which pieces are drawn,
where, in what order, and how the picture gets to the screen.

**How it gets in.** When WA loads the module, it checks that this is WA 3.8.1 and that the routine is exactly as WA has it, then
writes two 5-byte jumps over the start of the two loops. They lead into the module and back to WA right after the loops. If
anything is not as expected, it changes nothing. None of WA's files are changed.

## What it doesn't change

- **The game.** Only the picture is drawn faster. Physics, random numbers, turns and what is sent to other players are WA's own,
  so nobody else needs the module.
- **A map drawn by another module.** Zoomed with [wkMapZoom](https://github.com/GermsWorms/wkMapZoom) or wkVista, the map is drawn
  by that module, so wkFastDraw has nothing to speed up. At 100% zoom wkMapZoom lets WA draw (4.3 beta 2 also with its
  speedometer on), and then it helps.
- **Pauses that aren't drawing.** Many crates landing on a large map, for example, is the game's own work.

## Install

1. Download [`release/wkFastDraw.dll`](release/wkFastDraw.dll) and put it in the WA folder, next to `WA.exe`.
2. In WA: Options > Advanced > turn on "Load WormKit modules", and restart WA.

To remove it, delete the file while WA is closed. `install.ps1` (and `install.ps1 -Remove`) does the same.

It works with Steam's and GOG's 3.8.1. `%LOCALAPPDATA%\wkFastDraw\wkFastDraw.log` says whether it is on.

## Other modules

| Works with | |
|---|---|
| Drawing and zoom | wkMapZoom 4.3 beta, wkVista 0.1.4, wkZoom 0.2.0, wkRGBA, wkJellyWorm (3.8.1 v1) |
| Modding | WormForge 0.9.9 |
| Game and maps | wkRealTime 0.0.9.2, wkTerrainSync 1.2.6.1, wkTrackMeBetter 3.8.1, wkWall2Wall, wkPin 1.0.0.8 |
| Controls, sound, menus | wkRemapKeys, wkVolumeControl, wkSuperFrontend |

Not tried: RubberWorm, wkBindKeys, wkWormNAT2. If another module has changed the same routine, wkFastDraw leaves WA alone and the
log says whose change it found.

## Good to know

- With vsync **off** you get more frames a second. WA measures the mouse flick that takes over the camera per drawn frame, so
  at very high frame rates that flick has to be quicker. Holding the left mouse button, Scroll Lock, WA's
  `CameraUnlockMouseSpeed` setting or vsync all get around it.
- As with every WormKit module, WA notes a scrambled checksum of the file in replays and sends it to the other players. WA does
  nothing with it.
- Not tried yet: live online games, exclusive fullscreen, weak laptops, Windows 7/8/10 and Wine. If something looks wrong, please
  [open an issue](https://github.com/GermsWorms/wkFastDraw/issues) with the log.

## Build

`build.ps1` needs 32-bit MinGW gcc. It runs the tests (they compare the new loops with WA's own code from your `WA.exe`), then
makes `wkFastDraw.dll`.

## License

[MIT](LICENSE). Worms Armageddon is Team17's; this project is not affiliated with Team17.
