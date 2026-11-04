# hercules-native

A native **Apple-Silicon (macOS)** re-implementation of the engine behind *Disney's Hercules Action Game* (1997, Windows), written in C11 + SDL2 and built by reverse-engineering the original PC release.

> **You need your own copy of the game.** This repository contains no game data, no original binaries and no decompiled code. The engine loads the scripts, tables and assets from *your* game files at runtime. Not affiliated with or endorsed by Disney. "Hercules" and all game content belong to their owners.

## Status

Playable end to end on a Mac, with all levels ported: Training, Forest, Thebes, Hydra, Medusa, Titans, Vortex and the three "ENG3" levels (Cyclops, Gauntlet, Torment). The original boot flow, title screen, password screen, cutscene videos, save/continue and sound effects are in.

Known gaps:
- **No music.** It is CD-audio on the original disc, not in the game files. Drop your own ripped audio in if you want to extend this.
- Hydra stage lighting, the checkpoint sparkle shower and the pickup glow are not ported.
- Many enemies and objects have been verified against the original code only in headless scripted runs; expect rough edges. Bug reports are welcome (include the build line shown in the menu corner).

## Build and run

Requires macOS, a C compiler and SDL2 (`brew install sdl2`).

1. Put your game files in one folder: `HERCULES.FS`, `ENG1.DLL`, `ENG3.DLL`, `TITLE.DLL`, and (optional, for videos) the `DATA/*.ETV` files. The installer archive on the disc (`DATA32.Z`, InstallShield 3) holds them; the repo does not ship an unpacker for that yet, so use any InstallShield 3 extractor.
2. `make` then `./build/hercules --data /path/to/HERCULES.FS`
   (the DLLs and `DATA/` are looked up next to the `.FS`).
3. Or build a double-clickable app: `./make_app.sh /path/to/game_files`.

Default keys: arrows move, **Z** jump, **X** sword, **C** punch, **D** power, **S** weapon select, **Esc** pause, **F1/H** help, **F2/O** options, **F3/R** restart level, **Alt+Enter** fullscreen. Keys are remappable in the options screen.

Useful flags: `--level NAME`, `--frames N --script RRR.. --shot out.bmp` for headless testing (set `SDL_VIDEODRIVER=dummy`), `--sfxlog`, `--dumpwav DIR`.

## How it works

The game is Eurocom's PlayStation engine ported to PC: it even emulates PSX VRAM and GPU primitives and an ordering table for depth sorting. This project does **not** emulate the PC executable. It re-implements the engine and loads the original per-level scripts, animation tables and state tables out of the original DLLs.

What was reverse-engineered (details in [`docs/notes/`](docs/notes)):
- `HERCULES.FS` archive: hash-based path lookup, and 559 sounds (IMA ADPCM), screens, sprite banks.
- `ANIMPSX.BIN` sprite banks (RLE pieces + PSX BGR555 palettes), `MAP_PSX` level maps (strips, cells, block meshes, lighting, attribute layers) and the per-level palette and shading tables.
- ETV video (RNC method 2 + a 4x4 block codec, with CRC repair of single-bit disc errors) — see `tools/etv.py`.
- The player state machine and script interpreter, object/enemy handlers, boss logic, camera, the frontend flow in `TITLE.DLL`, sound mapping, and the PSX ordering-table depth rule that decides what draws in front of what.

Layout:
- `src/` — the engine (`render.c`, `player.c`, `objects.c`, `enemies*.c`, `bosses.c`, `training.c`, `e3*.c`, `frontend.c`, `sfx.c`, ...)
- `tools/` — Python tools to decode the formats (`herc_fs.py`, `assets.py`, `sprites.py`, `levels.py`, `etv.py`) and a Ghidra script
- `docs/notes/` — research and porting notes

## Contributing

Issues and pull requests welcome, especially: missing handlers, anything that differs from the original game, and music support. Please never attach game files.

## License

MIT for the code in this repository (see `LICENSE`). The game and its assets are not covered.
