# FXI: direction input arrives late / "mixes" actions

Tree: work/fx_input (start snapshot work/base36). All edits tagged `FXI`.

## Root cause (found in the original, not guessed)

The player state machine in `src/player.c` is a faithful port: every handler I compared against the
ENG1 decompiles (FUN_10016840/16b40/16da0/16f40/177a0/17a90/18240/18830/17c30/17c70/17cd0/18ed0,
air FUN_100171b0/17400/174e0, try_walk 10016990, turn_check 10017ae0, try_air_attack 10017290, go_jump*/go_fall/go_land,
physics 10019a10/19650) matches, including which states accept left/right (turning is only accepted where the
original accepts it: idle/walk/land/hurt/crouch/sword/punch via turn_check/try_walk; in the air only air_move flips
facing; turn state ignores input until its script ends, as in the original).

The bug is in the input layer. The PC host builds the pad word in HERCULES.EXE FUN_00408040 (keyboard FUN_00407f30 | joystick
FUN_00407e90) and then does:

    if (pad & UP)   pad &= ~DOWN;
    if (pad & LEFT) pad &= ~RIGHT;      /* UP beats DOWN, LEFT beats RIGHT; also pad = 0 while the window is inactive */

so ENG1 never sees L+R or U+D together (a PSX d-pad can't). The port skipped this. On a keyboard, reversing direction
means pressing the new arrow while the old one is still down for a few ticks, so:
* on the ground (FUN_10016b40): the old forward key keeps the walk going; the new key is ignored until the old key is released = "late";
* in the air (FUN_100174e0): facing flips every tick while both are down (facing right + L held flips left, next tick facing left + R held flips back), so the
  character jitters in place and the turn only sticks after the old key is released = "mixed actions".
Headless before/after (script `R*40 B*8 L*20`, `B` = LEFT+RIGHT): old kept running right for 8 ticks; new turns on the first tick (state 6) exactly
like the original. Air (`R*40 r*6 b*7 l*20`): old facing alternated 00/ff every tick with x oscillating 1288/1296; new faces left at once.

Also in FUN_00407f30: the whole keyboard reads 0 while Alt is held (Alt+Enter toggles the window); ported.

## Changes

* `src/input.c`, `src/input.h`
  * `input_socd()` (host filter above) applied in `input_game_bits` and the menu pad word.
  * Alt held -> keyboard ignored (FUN_00407f30).
  * Key-down latch (`input_note_key`, `input_game_tick`, `input_latch_clear`): the loop polled the keyboard once per 1/30 s tick, so a tap that began
    and ended between two samples was lost entirely (sometimes "does nothing", worst for quick turn-arounds / jump taps). A key-down is now
    remembered until the next tick consumes it. A held key is unchanged. (The original's GetAsyncKeyState poll can't lose a tap on Windows because
    key-down outlasts a tick; this just makes the same true on a 60 Hz event loop.)
* `src/main.c` (input / loop timing only)
  * input sampled once per TICK inside the tick loop (`in = input_game_tick(...)`); before, one sample per frame was reused for every catch-up tick.
  * 2 ms slack on the tick test (`acc >= tick - 0.002`). At 60 Hz, 2 frames = 33.33 ms ~ 1 tick, so timer jitter made ticks alternate between 2- and 3-frame
    spacing (50 ms hiccups, extra input latency). Average rate unchanged because `acc -= tick` keeps the debt.
  * after a level load, `acc`/`last` are reset (the blocking load was replayed as a burst of up to 7 ticks at spawn).
  * `SDL_StopTextInput()` (nothing reads SDL_TEXTINPUT; keeps Cocoa's text-input / press-and-hold path away from Z/X/C/S/D).
  * focus gain clears the latch; taps made in menus/pause don't leak into the level.
  * headless `--script`: input also goes through `input_socd` (set `FXI_RAW=1` to bypass); new chars `B` (L+R) and `b` (L+R+jump).
* `src/player.c`: `FXI_LOG=1` prints one line per tick (pad, pressed, state, facing, vx/target/acc, air speed, vy, x, y, frame, jump kind).

## Tick rate
The port keeps 30 Hz. ENG1's main loop (FUN_10020300) waits for vsyncs through the host function table (FUN_1002cc30, indirect), so the rate is not
readable from the decompiles; the animation timers, the 15 fps FMV and earlier fixes all fit 30 Hz. Not changed. No evidence of lag growing over time:
3000 ticks take ~0.07 s headless, one frame draw ~8 ms (software renderer, Forest, 14.8k faces) so there is headroom at 33 ms per tick.

## Verified headless
* before/after state logs for ground reversal, air reversal (above); ordinary run/jump/left-in-air, run-jump-sword, ground sword turn still behave as before.
* unit test of the input layer with a fake key array (L+R -> LEFT, U+D -> UP, tap latched one tick then consumed, held key unchanged, Alt gating,
  latch clear, menu pad SOCD): all pass.
* interactive loop starts and runs under the dummy video driver without a crash.

## Not verified (needs real hardware)
* real keyboard feel / frame pacing on the Mac display, the 2 ms slack and the latch under real event timing.
* Mac keyboard ghosting: some MacBook key matrices drop a third key (e.g. arrow + Z + X). That is hardware; software can't recover it. Remap in F2 Properties if a
  combo is blocked. No alternate bindings added (the original has none).
