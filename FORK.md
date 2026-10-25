# FORK.md -- what this fork adds, and how to keep it mergeable

This is a fork of [DylanPDao/TamaPoke](https://github.com/DylanPDao/TamaPoke)
(`upstream`). It adds **wild battles driven by per-region encounter areas**
and **party-follower switching**, and is built so that pulling upstream stays a
routine merge.
`README.md` and `CLAUDE.md` describe upstream and are deliberately not edited;
this file is where the fork documents itself.

## Fork rules

- **New code goes in new files**, which upstream never touches:
  `wild.h`, `wild.cpp`, `wild_ui.h`, `wild_areas.h` (generated), `tools/gen_wild.py`,
  `docs/*_region_pokemon_encounters.md`, `tools/emu/tests/wild_test.cpp`,
  `tools/emu/tests/wild_ui_test.cpp`, this file.
- **Upstream files only get hooks**: one call or one condition, tagged
  `// FORK(wild)`. `rg "FORK\(wild\)"` is the checklist after every merge.
- **No refactoring of upstream code**, and reuse rather than copy: the wild region
  chooser is upstream's region chooser in a new mode (`RPICK_FOR_WILD`), so it
  inherits every upstream fix to hit areas and paging.
- **Nothing positional is shared with upstream.** Strings live in the fork's own
  `W_STRINGS` table in `wild.cpp`, never in `i18n.cpp`'s positional `STRINGS`.
  The one NVS key is prefixed (`w_seen`). The `SCR_` enum is unchanged.
- **`FW_VERSION` is the fork's own version** (`3.12.0`), no longer derived from
  upstream's. On an upstream merge keep the fork's line (tagged `FORK(wild)`, so the
  conflict is expected) and bump it when the fork releases. `web/manifest.json`
  carries the same number: `tools/build_web.ps1` and `build_web.sh` both copy it
  from `FW_VERSION`, and the installer page shows it. It is display-only (boot log,
  settings, installer): the LAN hello compares `LINK_PROTO` and a table
  fingerprint, never `FW_VERSION`, and the fork adds no moves and no species, so
  LAN battles against upstream 3.11.x boards still work.
- **The README badge and the `web/` binaries are left alone**: they conflict on
  every upstream release. Rebuild `web/firmware/` locally to flash, never commit it.

## Every upstream edit

Tagged `FORK(wild)`:

| file | hook |
|---|---|
| `TamaPoke.ino` | `#include "wild.h"` at the top, `#include "wild_ui.h"` at the very bottom (so it sees every sketch global) |
| `TamaPoke.ino` | `setup()`: `wildLoad();` |
| `TamaPoke.ino` | serial dispatch: `if (wildSerial(line)) return;` |
| `TamaPoke.ino` | `LANBTN_W` / `LANBTN_X`: the LAN button takes the right half of its row, WILD the left |
| `TamaPoke.ino` | `onSwipe()`: `if (wildAreaSwipe(dir)) return;` before `rpickSwipe` |
| `TamaPoke.ino` | tap dispatch: `wildAreasShown` -> `wildAreaTap`, `wildPickOpen` -> `wildPickTap`, `wildGymTap` on the gym chooser |
| `TamaPoke.ino` | render: `wildRenderAreas()` while `wildAreasShown`, else `renderRegionPick(wildRpickMode())` |
| `TamaPoke.ino` | `startBattle()`: squad built from `squadMask` when `btlWild` |
| `TamaPoke.ino` | end of battle: `if (btlWild && wildBattleOver()) return;`; both exits call `wildAfterBattle()` |
| `TamaPoke.ino` | team picker: `PICK_WILD` title, BACK (`wildPickBack`) and FIGHT (`wildFight`) |
| `TamaPoke.ino` | `rpickModeNow()`: `SCR_GYMPICK` maps to `wildRpickMode()`; the region chooser's title and subtitle (`wildSeenCountIn`) for `RPICK_FOR_WILD`; `wildDrawPick(mode)` |
| `tools/emu/tests/run.sh` | `gen_wild.py --check && --selftest` when `python3` exists; `wild_ui_test` joins `needs_sketch` (wrapped, not edited) |

Untagged, because the line is a list entry or a fix rather than a hook:

- `save.cpp`: `#include "wild.h"` and `{ WILD_SEEN_KEY, SK_BYTES }` in `SAVE_FIELDS`.
- `tools/emu/build.sh` and `tools/emu/tests/run.sh` (`CORE`): `wild.cpp` added to the source lists.
- `tools/emu/tests/save_test.cpp`: marks two species seen and checks the bitmap round-trips.
- `tools/emu/README.md`: the list of compiled sources.
- `tools/emu/host_impl.cpp`: sprite path buffer 64 -> 512 (long repo paths lost shinies). Upstreamable as is.
- `TamaPoke.ino`: `btlClose()`, the one way out of a fight (free sprites, stop the music, close). The tap after a wild or LAN result had its own copy that never called `audioMusic(MUS_NONE)`, so the 90 s victory theme played on after the battle. `wild_ui_test` asserts the music is `MUS_NONE` after leaving a won fight, through `emuMusic()` in `host_impl.cpp`. Upstreamable as is.
- `gbsynth.cpp`: master volume is a 2.5 dB-per-step gain table (`MASTER_GAIN`) instead of linear `150 * master`, which left level 1 loud and clipped battle music from level 8 up. `synth_test` checks every step is louder, the steps are even, and two voices never clip. Upstreamable as is.
- `audio.cpp`: stopping the music cuts its two voices, so `busy()` clears instead of spinning the task with the amp on; volume 0 keeps the amp off (its turn-on click is the one sound the volume cannot scale); the amp is held on 1.5 s between sounds so a run of taps does not click each time; the dead `playTone()`, which had its own linear volume rule, is gone. Upstreamable as is; the emulator stubs audio, so only a board can confirm it.
- `.gitattributes`: LF for `*.sh` and `*.py`, which WSL bash needs. Upstreamable as is.

## Syncing from upstream

```bash
git fetch upstream
git checkout main && git merge --ff-only upstream/main
git checkout pt/feat/wild-encounters && git merge main
rg "FORK\(wild\)"                    # every hook still where it should be
rg "FORK\(follow\)"
bash tools/emu/tests/run.sh          # all suites, plus gen_wild.py --check/--selftest
```

Then `arduino-cli compile` (see below). Rebuild `web/` locally if needed, but do
not commit it. If upstream changed `trainers.h`, `dex_data.py` or `noart.h`, run
`python3 tools/gen_wild.py` and commit the regenerated `wild_areas.h`;
`--check` fails until you do.

## Upgrade safety: an install updates in place, never wiped

The fork only ADDS state, and every reader tolerates that state being absent,
short or long. Nothing that already exists is reinterpreted.

- **One new NVS key, `w_seen`** (the seen-in-the-wild bitmap). Absent: zeros.
  Shorter (an older `DEX_COUNT`): fills the front. Longer: keeps the prefix.
  It is written only the first time a species is met, never at boot, so flashing
  the fork changes nothing in NVS until the first wild encounter.
- **No new fields on `Pet` or `PartyMon`**, so the party and box strides cannot
  move. Energy and training go through upstream's public API and write the same
  keys as any battle.
- **Flashing back to upstream is safe**: it never reads `w_seen`, its `EXPORT`
  leaves it out and its `IMPORT` skips unknown keys.
- **Backups:** a fork `EXPORT` restores on upstream minus the seen bitmap; an
  upstream `EXPORT` restores on the fork with the bitmap at zero. `IMPORT` clears
  the namespace first, so restoring an upstream backup resets the bitmap.
- **Same flash layout**: same FQBN, `PartitionScheme=app3M_fat9M_16MB`. Flash
  with `arduino-cli upload` (app only). With the web installer, keep the
  four-part manifest and decline its erase prompt. Never add an erase step.
- **No new SD assets.** Wild battles use the sprite packs already on the card.

Rules for any future fork feature: only add NVS keys, with a fork prefix;
tolerate absent, short and long blobs; never write at boot; never change the
partition scheme.

## Wild battles

**Flow:** swipe left to the gym chooser, tap WILD, pick a region (the wild mode
of the region chooser, subtitled with species seen), pick an area, pick a team,
fight. After the fight the player lands back on the same area page, or on the
gym chooser if the pet can no longer go.

**Numbers** (`wild.h`):

| what | value |
|---|---|
| energy spent by the live pet per battle | `WILD_ENERGY_COST` 8 |
| refused below | `WILD_ENERGY_MIN` 25, also while asleep or still an egg |
| training on a win the live pet fought in | 1-2 points, through `pet.rewardTraining()` |
| no training when the foe is more than | `WILD_REWARD_GAP` 10 levels below the pet; the log says so (`W_TOO_EASY`) |
| team size | uncapped (`PICK_WILD`), any of the party plus the live pet |
| foe species, odds and level | the area's table (below), never the squad |

Losing or running away gives nothing. Energy is the rate limit, as for gym
rematches. `wildTooEasy()` is the single answer to the gap: `wildReward()` and
the battle log both ask it.

**Seen:** a species is marked in `w_seen` when an encounter starts.
`wildSeen(d)` is that bit OR `pet.isRegistered(d)`. It lives outside `Pet`, so
no ending clears it; `WIPE` does.

**Serial console:**

- `WILD AREAS [r]` lists region `r`'s areas: `<index> <name> tier=<t> open Lv a-b`
  or the closed reason. Matched before the numeric forms, so it never starts a fight.
- `WILD <r> <index>` fights in that area (names have spaces; the index is the handle).
- `WILD [r]` fights in a random open area of `r`, default the egg region.
  `REGION_ALL` has no areas, so with the egg region set to ALL it answers
  "No open area"; use `WILD <r>`.

**Breadcrumbs:** the area list reports `SCR_GYMPICK` from `uiCurrentScreen()`, so
a crash there is reported as the gym chooser. This keeps the `SCR_` enum and the
3 s hold guard unchanged (it is not `SCR_MAIN`, so no release is offered).

## Encounter areas: the data

### The documents are the source

`docs/<region>_region_pokemon_encounters.md`, one per `REGIONS` entry in
`tools/dex_data.py`, are the ONLY encounter data. `tools/gen_wild.py` compiles
them into `wild_areas.h`, a flash-resident `static const` table included by
`wild.cpp` alone (included twice, it would be in flash twice). The firmware
never reads the Markdown and loads nothing from SD.

A missing document fails the build, and so does a document that maps to no
region. A new region ships its document together with its ladder; nothing in
the firmware lists regions. The gym ladder (leader, place, which tier) comes from
`trainers.h`'s `TRAINER_SETS`, never from a list in the script.

**After editing any document:**

```bash
python3 tools/gen_wild.py           # regenerate and read the report
python3 tools/gen_wild.py --check   # what run.sh runs: fails if wild_areas.h is stale
python3 tools/gen_wild.py --selftest
```

### The format contract

- A section is `## N. Heading`. `### ...` subheadings inside it (Unova's BW /
  B2W2, Alola's SM / USUM) are ignored; their tables belong to the section.
- An encounter table's header cells start with `Pok`, `Location`, `Level` and
  `Encounter`. The separator row is any mix of `-` and `:`.
- `**Available Areas...:**` lines (optionally tagged, e.g. `(BW)`, trailing `.`
  allowed) list the places a section covers. A section may have several.
- Anything the parser cannot read fails the build. Nothing is silently skipped.

### Sections and tiers

Every section is exactly one of: **gym-named** (its heading names one of the
region's first `TRAINER_GYMS` trainers, by word prefix; tier = earliest such
trainer's index + 1), **`SECTION_TIER`** (explicit tier), **`TIER_TITLES`** (the
pre-Elite 4 tier `TRAINER_GYMS + 1`) or **`SKIP_SECTIONS`** (legendaries,
post-game, gifts, trades). Tiers must strictly increase in document order and
tier 1 must own an area. A gym tier's title is the leader's place from
`trainers.h`.

| region | sections -> tiers | overrides |
|---|---|---|
| Kanto | 1-8 -> 1-8 (Victory Road is in Giovanni's) | skip 9, 10 |
| Johto | 1-8 -> 1-8, 9 -> 9 | title 9 `VICTORY ROAD`; skip 10. Tier 6 owns no area |
| Hoenn | 1-8 -> 1-8, 9 -> 9 | title 9 `VICTORY ROAD`; skip 10, 11 |
| Sinnoh | 1->1, 2->2, 3->3, 4->5, 5->6, 6->7, 7->8, 8->9 | title 8 `VICTORY ROAD`; skip 9, 10. Tier 4 owns no area |
| Unova | 1->1, 2->3, 3->4, 4->5, 5->6, 6->7, 7->9 | `SECTION_TIER` 1->1, 6->7; title 7 `VICTORY ROAD`; skip 8, 9. Tiers 2 and 8 own no area |
| Kalos | 1-8 -> 1-8, 9 -> 9 | title 9 `VICTORY ROAD`; skip 10, 11 |
| Alola | 1->1, 2->2, 3->5, 4->6 | skip 5, 6. No pre-Elite 4 tier |

Tier k opens once gyms 1..k-1 are beaten in order; tier `TRAINER_GYMS + 1` opens
with all eight. `wildTier()` counts the consecutive run from gym 0 on easy and on
hard SEPARATELY and takes the longer, as `gymUnlocked()` does (OR-ing the masks
would build a run neither ladder has), and caps it at `TRAINER_GYMS` because bits
8-12 are the Elite 4 and the Champion.

**Area tier** = max(the tier of the earliest non-skipped section whose Available
Areas lists it, its earliest slot tier). That puts an area where the document
says the player reaches it: a row from an earlier tier that names a later place
(Kanto's Mankey on Routes 5 and 6) adds a slot there without opening it early.
A slot is **live** when its area's tier and its own tier are both unlocked, its
time mask matches the hour, and its species is playable (its region's pack is
installed and it has art). An area is open when it has a live slot. Closed rows
read, in this order: "Badge n" (tier), "Not now" (time), "Needs pack".

**Both game versions are kept.** Unova's BW and B2W2 tables and Alola's SM and
USUM tables are all parsed, like version exclusives: a device has no version.
Unova's ladder is B2W2, so BW-only places (Dreamyard, Pinwheel Forest, Cold
Storage, Route 10) sit beside it. That mix is accepted.

### Rows

1. `ROW_FIX` rewrites a raw row; `RARITY` rewrites a rarity cell.
2. Skip: a rarity naming `Static`, `Story`, `Special`, `Gift`, `Roaming` or
   `Event`, or any legendary species. Skipped rows are never parsed further.
3. Parse names (split at parenthesis depth 0; `NAMES` fixes typos), levels
   (`a-b` or `a`), locations (`ALIASES`, then depth-0 split on `,` and ` / `;
   every location must be in the known set) and time notes.

**Regional forms collapse to their species** ("Rattata (Alolan)" is RATTATA), and
need that species' pack: an Alolan form needs the Kanto pack.

**Art-less species are dropped** at generation, read from `NO_ART` in `noart.h`.
After `tools/check_sprites.py --emit`, re-run `gen_wild.py`; `--check` fails until
you do.

### Weights, the clamp, and levels

- **Weights** are tenths of a percent, floored at 1: the first percentage in the
  rarity (or a range's midpoint); otherwise Very/Extremely Rare 10, Common to
  Uncommon 250, Uncommon 150, Rare 50, Common 400. Species sharing a row split
  its weight evenly unless the rarity says "each".
- **Evolution clamp:** within a row, an evolved form is clamped to
  `max(lo, its evolveLevel)..hi`, and dropped if that is empty. **The base form
  keeps the full range**, because care mistakes really do keep a creature
  unevolved past its level (Voltorb stays in the Power Plant, Mareep on Routes
  42/43). Scope: inside a pair only. A single-species row keeps the document's
  levels as written (Kanto's Metapod 4-6, Hoenn's Claydol 34-38).
- **Levels are the document's after the clamp**, uniform within a slot.

### Time of day

Slots carry a mask from the document's time notes (`Morning`, `Day`, `Daytime`,
`Night`). The windows are HGSS's for every region: morning 04-10, day 10-20,
night 20-04 (`WILD_*_FROM` in `wild.h`). `TIME_WORDS` overrides a word per
region: Alola's `Day` is morning AND day, since Sun/Moon has only day and night.
`PAIRED_TIMES` pairs a two-species row's notes in order (Johto's
"Meowth / Farfetch'd (Night/Day)").

The hour is `(lastSeenEpoch / 3600) % 24`, the same source and formula as
`isNightHour()`; the sketch refreshes `lastSeenEpoch` from the RTC every 30 s.
With no clock (`lastSeenEpoch == 0`: a fresh save, or the emulator) every slot
is live. A board whose RTC was never set reads a wrong but non-zero time, so its
gating is wrong rather than open; set the clock from SETTINGS or `RTCSET`.
Seasons and swarms are not modelled.

No area in the current documents can read "Not now": each has an all-day slot
at its opening tier. The rule is tested through `wildWhyClosedOf()` instead.

### Override tables

All in `OVERRIDES` at the top of `tools/gen_wild.py`, per region. They are the
complete record of every judgement call. **Every entry must match something or
the build fails**, so a stale override cannot linger.

| table | add an entry when |
|---|---|
| `SECTION_TIER` | a heading's named leaders give the wrong tier |
| `TIER_TITLES` | a section is the pre-Elite 4 stretch |
| `SKIP_SECTIONS` | a section is legendaries, post-game, gifts or trades |
| `ALIASES` | a location cell is not an area name (`[]` drops it) |
| `EXTRA_AREAS` | a real place is missing from every Available Areas line (must receive a slot, and must not already be listed) |
| `DISPLAY` | an area name is longer than `WILD_NAME_MAX` (15) |
| `RARITY` | a rarity cell has no percentage and no keyword |
| `ROW_FIX` | a row needs splitting or rewriting in a way no rule expresses |
| `NAMES` | a species name is misspelled in the document (`None` drops it) |
| `TIME_WORDS` | a region's games define a time word differently |
| `PAIRED_TIMES` | a multi-species row's time notes pair with its species in order |

### The random roll is retired

The first version rolled a random common/rare species from the region at the
squad's level. Every region now has a document, so no screen or command can
reach that path, and an untested path rots: `wildRoll()`, `wildEvolveFor()`,
`wildLevelFor()`, `wildSquadTop()`, `inRegion()`, `isWildBase()`, `pickTier()`
and their constants are gone.

**Balance consequence:** area levels come from the documents, not the squad. A
level 73 team on Route 1 wins every time, which is why the reward gap exists.

### Size

245 areas, 1,031 slots, 53 tier titles; about 15.9 KB of flash, no RAM. The
generator fails above 24 KB.

## Building and testing

```bash
bash tools/emu/tests/run.sh          # all suites (WSL: wsl -d Ubuntu)
bash tools/emu/tests/run.sh wild     # wild_test and wild_ui_test
```

On Windows, `run.sh` runs in WSL Ubuntu (the default `docker-desktop` distro has
no bash), which has no `arduino-cli`, so compile the firmware separately with the
CLI bundled in the Arduino IDE. A green emulator build is not proof the firmware
builds:

```powershell
& "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" `
  compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" .
```

To rebuild what `web/index.html` installs (the four firmware parts and
`manifest.json`, checked by `check_installer.py`), the Windows counterpart of
`build_web.sh`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_web.ps1            # firmware only
powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -Sprites   # also repack sprites-<region>.pak
powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -Serve     # build, then serve web\ and open the installer
powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -ServeOnly # just serve web\
```

The server's root must be `web\` (the page fetches `manifest.json` relative to
itself); the script passes it as an absolute `--directory`, so it works from any
console, including an admin one that starts in `System32`.

- `wild_test`: the logic against the real table -- coverage, tiers, rolls, the
  clamp, per-slot and time gating, weights, ownership, dedupe, packs, table
  invariants, the reward gap, refusals, energy, the seen bitmap, strings.
- `wild_ui_test` (needs the sketch): drives the real `onTap`/`onSwipe`/`battleTap`
  through every area of every region, BACK and both exits, the refusal routing,
  the 3 s hold and the console. Set `WILD_SHOT_DIR` to also write screenshots.
- `gen_wild.py --selftest`: each generator guard fails on a broken copy of the
  documents, and the ordering rules pass on the real ones.

## On the board

The emulator cannot judge touch accuracy or a real clock. Checks to run:

1. **Touch.** Open WILD -> a region -> an area list with six rows (Hoenn tier 1
   has seven, so its first page is full). Tap each row, especially the bottom one
   (y 326-372), and BACK (y > 380), with a finger. Rows are drawn 340 x 44 px and
   hit-tested over the full 46 px pitch, so adjacent rows tile with no dead band.
2. **RTC time gating** on Johto `ROUTE 29` (`WILD 1 0`). `EXPORT` first.
   - Set 23:00 with `RTCSET <epoch>`, wait 30 s for the loop to pick it up.
   - `WILD AREAS 1` shows `0 ROUTE 29 tier=1 open Lv 2-5`.
   - Fight a few times (`WILD 1 0`, then run): only HOOTHOOT and RATTATA appear.
   - Set 12:00 and repeat: only PIDGEY, RATTATA and SENTRET, never HOOTHOOT.
   - Put the clock back to the real time before rebooting: a forward `RTCSET`
     followed by a reboot is read as time spent switched off.

Each fight costs 8 energy and is refused below 25, so a rested pet gets about ten.

## Not built / follow-ups

- **Both board checks above have not been run yet.**
- `wild_upgrade_test` (seed NVS with upstream's code, boot the fork, assert every
  upstream key byte-identical) was planned and not written. `save_test` covers
  the `w_seen` round-trip only.
- The seen bitmap shows only as the wild region chooser's subtitle; the Pokedex
  detail does not use `wildSeen()`.

## Party follower switching

Any banked party member can become the live follower, the one that ages and
takes care. The outgoing follower takes that member's slot, with its care
state kept. Level is still age: only the follower ages, and a switch moves no
levels. Battles still pay out training and badges only.

New files, same split as wild: `follow.h` / `follow.cpp` have no sketch
dependency and link into the headless tests. Drawing and taps need the sketch
globals, so they live in `follow_ui.h`, included once at the bottom of
`TamaPoke.ino`. Strings are `FT()` in `follow.cpp` (`F_STRINGS`), never a row
in `i18n.cpp`. Tests: `tools/emu/tests/follow_test.cpp`,
`tools/emu/tests/follow_ui_test.cpp`.

### Switching

`followCanSwitch(slot, &reason)` is the only answer the sheet, the tap and
`FOLLOW` share.

| state | result |
|---|---|
| egg, ceremony, evolution animation, starter still unpicked, empty slot | refused, `NOT NOW` |
| a move-learn offer is pending (`learnQueue` is RAM only and would be lost) | refused, `LEARN THE MOVE FIRST` |
| asleep | allowed; the incoming creature arrives awake |

While an egg is waiting, the party sheet's left button stays upstream's
BRING BACK, so `release_test` is unchanged. Once the follower is hatched, that
button is SWITCH. Success closes the sheet and the party (`partyOpen` clear,
so the screen is `SCR_MAIN`) and plays `SFX_HATCH`. A refusal plays
`SFX_DENY` and leaves the sheet up.

The swap is one-for-one, so it cannot break the cap. Order, so a crash has
only two outcomes:

1. Write `f_swap`: the slot index plus the outgoing `PartyMon`.
2. `pet.saveNow()` of the incoming creature.
3. `party.replaceAt` with the outgoing creature.
4. `followSave()` of `f_ext`.
5. Delete `f_swap`.

Age, bond, care, mistakes, moves, nick and IVs come from the `FollowExt`
record plus the `PartyMon`. `FollowIO` is a friend of `Pet` so those private
care fields move without a second copy of them. A missing record uses the same
fallback as `Pet::reviveFrom()`: age `(level - 1) * MINUTES_PER_LEVEL`,
`lastLearnLevel = level`, fresh care (fullness, joy and energy 80, hygiene
100). `frozen` is still cleared.

At boot, a leftover journal is finished or deleted and nothing else:

- The live pet's fingerprint matches the creature still in that slot, and
  differs from the outgoing record: `saveNow` already ran, so finish
  `replaceAt`.
- Any other case, including a short or odd-sized blob: the swap never took
  effect, so delete the journal.

Records match by content, not by slot. The fingerprint is FNV-1a over dex,
level, medals, IVs, training, shiny and nick. Moves are left out, so editing a
banked moveset still finds the record. An empty mon hashes to 0; a real hash
of 0 is stored as 1. A deposit, a withdraw and a reorder need no hook.

### Cap, and the cell that is not a slot

Storage stays `PARTY_SLOTS` 6. Shrinking it would make `Party::begin()` read a
180-byte blob as shorter records. `PARTY_CAP` is `PARTY_SLOTS - 1`. Both
`firstFree()` and `isFull()` look only at slots `[0, PARTY_CAP)`. `isFull()`
is `firstFree() < 0`, not `count() >= PARTY_CAP`: a leftover occupant of slot
5 would make those two disagree. `count()` still counts all six, which is why
a test that wrote six members and never called `followLoad()` still sees six.

Slot index 5 is the follower's cell on the grid. `followDrawCell()` paints it,
tagged FOLLOWING, after the slot loop, and only while the slot is empty.
`followCellTap()` runs before that loop and swallows the cell, so a box drop
(`boxSel`), the replace picker (`partyPick`) and arming a swap (`boxSwapFrom`)
cannot fill it. `pickCandidates()` still loops all six slots; in normal play
slot 5 is empty.

A save from before the cap can still hold a sixth member in slot 5.
`followLoad()` moves it once, after the journal check: copy to the first hole
in 0-4, else into the box, and clear slot 5 only after that copy is saved. A
fingerprint already in either place means the crash landed between the two
writes, so the leftover is cleared and not copied again. If slots 0-4 and the
box are both full, it stays. While it stays, `followDrawCell` and
`followCellTap` return without drawing or swallowing, and the normal slot loop
keeps the creature visible and depositable. Nothing is hidden or deleted.

### `followShownCount()`

The header is not `party.count()`. `followShownCount()` is the one answer,
used at both `S_PARTY_FMT` sites (the party header and the player card):

- Slot 5 empty: `party.count() + 1`. The live follower counts, and so does an
  egg, the same way the main games count an egg in the party.
- Slot 5 still occupied: `party.count()`, so six banked members do not read
  as `7/6`.

The serial `PARTY` line stays a storage dump and does not call it.

### `followLoad()` is not inside `begin()`

`party.begin()` and `pet.begin()` load NVS. `followLoad()` runs once after
both, from `setup()`. It is not called from `Party::begin()`.
`box_test`, `save_test`, `moves_test` and `touch_test` seed six members by
writing `slots[]` and never call `followLoad()`, so the slot-5 move stays a
boot step and those suites stay green. A boot with no journal and an empty
slot 5 writes nothing.

### `f_ext` and `f_swap`

| key | in `SAVE_FIELDS` | what it is |
|---|---|---|
| `f_ext` | yes, `{ "f_ext", SK_BYTES }` | up to 8 `FollowExt` records, 28 bytes each, 224 bytes, under `MAX_VAL` (768) |
| `f_swap` | no | the swap journal: one slot byte plus the outgoing `PartyMon` |

`f_ext` is additive. Absent reads as no records. A short blob fills the front,
whole records only. A longer blob is truncated to 8. Upstream never reads the
key; its `IMPORT` skips unknown keys, so a fork backup restores there minus
the care records, and an upstream backup restores here with no records. The
party and box strides are unchanged.

`f_swap` is left out of `SAVE_FIELDS` on purpose. A backup must not contain
it. The journal is crash recovery for the device that is mid-swap, not player
state. `EXPORT` walks `SAVE_FIELDS` only, so a backup taken in that window
does not carry the journal, and `IMPORT` cannot replay it: the next
`followLoad()` would otherwise finish `replaceAt` or delete the key against
whatever the restored pet and slot happen to fingerprint as. Crash recovery
stays in NVS, on the device that crashed, and is gone once the swap completes.

### Eviction

`followSave()` drops any record whose fingerprint matches no party or box
member. `followStore()` refreshes a match by moving it to the end. When all 8
slots are full it prunes first, then drops the oldest (index 0). The newcomer
is kept even if it is not in the party yet: the switch banks it on the next
line, and the prune runs only after that. A creature whose record was evicted
comes back through the `reviveFrom` fallback above.

### A legacy frozen companion ages again

`Pet::reviveFrom()` sets `frozen`, which is how a brought-back companion stops
ageing. `followSwitch` always clears `frozen`, sets `sleeping = false` and
clears `learnQCount`. A frozen follower that is switched out and back in
starts ageing. That is the point of the switch: the incoming creature grows.

### Farewell, once

No new field. `declineFarewell()` sets `farDeclinedAge = FAREWELL_NEVER`
(`UINT32_MAX`) instead of `ageMinutes + 1440`. `wantFarewellButton()` is
`canFarewellNow() && ageMinutes >= farDeclinedAge`, so that creature is never
auto-prompted again. The sentinel already rides in `FollowExt`, so a switch
keeps it. Upstream reads it as "not yet" and simply never re-offers. A
creature with no record falls back to 0 and may be prompted once more.

The manual button is only for the live follower. Tapping cell 6 while slot 5
is empty opens a sheet built from `followSnapshot(pet)`, not from a party
slot. `followDrawFarewell()` shows `FT(F_FAREWELL)` ("TO PARTY") only while
`canFarewellNow()` is true (final form, not frozen, not asleep, age at least
`FAREWELL_AGE_MIN`), and the tap calls the existing `startFarewell()`. Banked
party and box sheets do not get it. An egg cell is swallowed and opens no
sheet. A swipe calls `followSheetBack()` and closes that sheet, not the party
under it.

The confirm no longer says goodbye. `followBankCopy()` is the question, the
two buttons and the line under them: "Join the party?", "To the party",
"Keep following", "Switch them back any time". The gold button and the
ceremony line (`followFarewellBtnFmt()`, `followFarewellMsg()`) say the same
thing. An early retire still uses upstream's "Goodbye" and its cost lines,
because that creature is not banked. A free retire, which is a farewell
reached from the menu, uses the party copy.

### Hooks

`rg "FORK\(follow\)"` after every upstream merge:

| file | hook |
|---|---|
| `TamaPoke.ino` | `#include "follow.h"` at the top, `#include "follow_ui.h"` at the very bottom (after `wild_ui.h`) |
| `TamaPoke.ino` | `setup()`: `followLoad();` immediately after `party.begin(); pet.begin();` |
| `TamaPoke.ino` | serial dispatch: `if (followSerial(line)) return;` |
| `TamaPoke.ino` | both party swipes: `if (followSheetBack()) return;` before the sheet or the party closes |
| `TamaPoke.ino` | `renderMonSheet()`: `followSheetLeft()` when `!fromBox && !pet.isEgg()` |
| `TamaPoke.ino` | `partyTap()`: `followTapFarewell()` first; `followTapSwitch()` before the BRING BACK branch; `followCellTap()` before the slot loop |
| `TamaPoke.ino` | render: `followSheetUp()` draws `followRenderSheet()` instead of the party |
| `TamaPoke.ino` | both `S_PARTY_FMT` sites pass `followShownCount()` |
| `TamaPoke.ino` | `renderParty()`: `followDrawCell();` after the slot loop |
| `TamaPoke.ino` | farewell dialog and a free retire: `followBankCopy()`; gold button: `followFarewellBtnFmt()`; banking ceremony: `followFarewellMsg()` |
| `pet.h` | `FAREWELL_NEVER`; `declineFarewell()` assigns it; `friend struct FollowIO` |
| `party.h` | `PARTY_CAP`; `isFull()` is `firstFree() < 0` |
| `party.cpp` | `firstFree()` loops to `PARTY_CAP` |
| `save.cpp` | `{ "f_ext", SK_BYTES }` in `SAVE_FIELDS` |
| `tools/emu/tests/run.sh` | `follow_ui_test` joins `needs_sketch` (wrapped, not edited) |

`tools/emu/sketch.cpp` is generated from the sketch, so it repeats the `TamaPoke.ino` tags. `follow_ui_test.cpp` names the tag in a comment. Neither is a hook.

Untagged, because the line is a list entry rather than a hook:

- `tools/emu/build.sh` and `tools/emu/tests/run.sh` (`CORE`): `follow.cpp` added to the source lists.

```bash
bash tools/emu/tests/run.sh follow   # follow_test and follow_ui_test
```
