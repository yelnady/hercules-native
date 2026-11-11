# Hercules native port: working notes for Claude

A native macOS/SDL2 port of Disney's Hercules (1997 PC). `src/` is the engine, reimplemented from the original
`ENG1.DLL` / `ENG3.DLL` / `TITLE.DLL` / `HERCULES.EXE`. Comments cite the original function as `FUN_1xxxxxxx`.

## Always update the app

**Any change under `src/` must end with `make`.** `make` builds `build/hercules` and copies it into the local
`Hercules.app` (re-signed) when that app exists. The app is what the user actually plays, so a fix that only
reaches `build/` is not a fix. A stale app once kept a fixed bug (the Training rock) in the game for days.

- After `make`, check the output says `updated Hercules.app`.
- `./make_app.sh game` rebuilds the app from scratch if it is missing.

## Layout (local, not in git)

| Path | What it is |
|---|---|
| `game/` | The original game files: `HERCULES.FS`, `ENG1/ENG3/TITLE.DLL`, `HERCULES.EXE`, `DATA/*.ETV` videos, the disc ISO, the password guide. Never commit (see `.gitignore`). |
| `analysis/` | Decompilation of the original binaries (`ENG1/`, `ENG3/`, `HERCULES/`, `TITLE/`). Grep here for `FUN_xxxxxxxx` when porting or checking behaviour. |
| `Hercules.app` | The playable app. Its `Contents/Resources` holds a copy of the game files. |
| `docs/notes/` | Per-area porting notes (in git). Start with `levels_notes.md` and `objects_notes.md`. |

## Debugging by playing headlessly

The binary can play a level with scripted input and save a screenshot, no window needed:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy PZ=128 ./build/hercules --data game/HERCULES.FS \
  --level TRAINING --x 20300 --script "$(python3 -c "print('.'*100+'R'*200, end='')")" \
  --frames 300 --every 25 --shot /tmp/out.bmp
```

- `--script` is one character per tick: `R`/`L` walk, `J` jump, `P` punch, `T` power punch, `S` sword, `U`/`D` up/down,
  `.` nothing. See `main.c` for the rest.
- `--every N` prints the player's x, y, z, state and bank every N ticks. State 20 is lifting, bank 3 is carrying.
- **Set `PZ=128` with `--x`.** `--x` places Hercules at depth z=0 by default, but normal play keeps him at z=128,
  the lane the level's objects use. At z=0 he walks through objects and every test result is wrong.
- `OBJ_DEBUG=1` lists live objects after the run; `OBJ_DUMP=1` lists the level's spawn records.
- Convert screenshots with `sips -s format png in.bmp --out out.png`.
- Down only changes depth lane at doorway spots (level attribute flags 0x10/0x20); elsewhere it crouches.
