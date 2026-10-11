#pragma once
#include <Arduino.h>
#include "pet.h"

// Wild battles (fork-only, see FORK.md). Pure logic: no sketch, SD or display
// dependency, so it links into the tests exactly the way pet.cpp does.

#define WILD_COMMON_PCT 85     // the rest are R_RARO; legendaries never appear
#define WILD_LEVEL_SPREAD 5    // foe level = squad's best minus 0..this
#define WILD_MIN_LEVEL 2
#define WILD_ENERGY_COST 8     // spent by the live pet per battle
#define WILD_ENERGY_MIN 25     // refused below this; matches S_EXHAUSTED
#define WILD_REWARD_MIN 1      // training on a win: MIN .. MIN + SPAN - 1
#define WILD_REWARD_SPAN 2

// Its own key in the shared "tamapoke" namespace, prefixed so an upstream key
// can never collide with it. WIPE clears it and EXPORT/IMPORT carry it.
#define WILD_SEEN_KEY "w_seen"

enum WildRefusal : uint8_t { WILD_OK = 0, WILD_EGG, WILD_ASLEEP, WILD_TIRED, WILD_LOCKED };

// A species for a wild encounter in `region`, or -1 if the region is locked or
// has nothing playable. Never falls back to another region, never legendary.
int16_t wildRoll(uint8_t region, uint8_t lvl);
// Walks `base` forward along evolvesTo while `lvl` has reached each gate. Stops
// at EEVEE (its branch is a player choice), at anything unplayable, and at the
// edge of `region` unless that is REGION_ALL.
int16_t wildEvolveFor(int16_t base, uint8_t lvl, uint8_t region);
uint8_t wildLevelFor(uint8_t squadTop);

WildRefusal wildCanStart(const Pet &p, uint8_t region);
void wildSpend(Pet &p);
// Training for the live pet, only on a win it fought in. Returns what was
// gained (0 = nothing) and writes the stat into `which` (0 ATK, 1 DEF, 2 SPE).
uint8_t wildReward(Pet &p, bool won, bool petFought, uint8_t &which);

// Seen-in-the-wild bitmap. Loading never writes; a bit is written only the
// first time a species is met, so flashing the fork changes nothing in NVS.
void wildLoad();
void wildMarkSeen(int16_t d);
bool wildSeen(const Pet &p, int16_t d);   // met in the wild OR raised
uint16_t wildSeenCountIn(const Pet &p, uint8_t region);

// Fork-owned strings, kept out of upstream's positional STRINGS table so its
// appends can never shift ours. ASCII only, like every firmware string.
enum WildStr : uint8_t {
  W_BTN, W_TITLE, W_SEEN_FMT, W_APPEARED, W_WIN, W_GAIN,
  W_NO_EGG, W_NO_SLEEP, W_NO_ENERGY, W_NO_PACK,
  W_COUNT
};
const char *WT(WildStr s);
const char *wildRefusalText(WildRefusal r);
