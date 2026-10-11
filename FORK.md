# FORK.md -- what this fork adds, and how to keep it mergeable

This is a fork of [DylanPDao/TamaPoke](https://github.com/DylanPDao/TamaPoke)
(`upstream`). It adds one feature, **wild battles driven by per-region encounter
areas**, and is built so that pulling upstream stays a routine merge.
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
- **`FW_VERSION` is upstream's version plus a fork patch number** (`3.11.1` is
  upstream 3.11 with this fork). On each upstream merge, take upstream's value and
  re-append the patch number; the line is tagged `FORK(wild)` so the conflict is
  expected. It is display-only (boot log, settings): the LAN hello compares
  `LINK_PROTO` and a table fingerprint, never `FW_VERSION`, and the fork adds no
  moves and no species, so LAN battles against upstream boards still work.
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
- `.gitattributes`: LF for `*.sh` and `*.py`, which WSL bash needs. Upstreamable as is.

## Syncing from upstream

```bash
git fetch upstream
git checkout main && git merge --ff-only upstream/main
git checkout pt/feat/wild-encounters && git merge main
rg "FORK\(wild\)"                    # every hook still where it should be
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
