#pragma once
#include <Arduino.h>
#include "party.h"

// Care state the party record cannot hold, for the follower swap (FORK.md).
// Matched by fingerprint, not by slot, so a move through the box needs no hook.
// follow.cpp has no sketch dependency and links into the headless tests.

#define FOLLOW_EXT_SLOTS 8
#define FOLLOW_EXT_KEY "f_ext"
#define FOLLOW_SWAP_KEY "f_swap"   // journal; deliberately absent from SAVE_FIELDS

enum FollowStr : uint8_t {
  F_SWITCH = 0,
  F_LEARN_FIRST,
  F_NOT_NOW,
  F_FOLLOWING,
  F_FAREWELL,
  F_COUNT
};

// 3 x uint32, then 14 uint8, then one uint16 on a 2-byte boundary: 28, no pad.
struct FollowExt {
  uint32_t fingerprint;
  uint32_t ageMinutes;
  uint32_t farDeclinedAge;
  uint8_t fullness, joy, energy, hygiene;
  uint8_t poops, weight, bond, careMistakes;
  uint8_t mistakeCooldown, neglectTicks, evoPen, evoDeclinedLv;
  uint8_t lastLearnLevel, berryKnown;
  uint16_t goodTicks;
};
static_assert(sizeof(FollowExt) == 28, "FollowExt is 28 bytes");
static_assert(FOLLOW_EXT_SLOTS * sizeof(FollowExt) <= 768, "f_ext exceeds the backup cap");

struct FollowSnap {
  PartyMon mon;
  FollowExt ext;
};

class Pet;
struct FollowIO {
  static void capture(const Pet &p, FollowExt &e);
  static void apply(Pet &p, const PartyMon &m, const FollowExt &e);
};

uint32_t followFingerprint(const PartyMon &m);   // 0 if empty; never 0 for a real mon
FollowSnap followSnapshot(const Pet &p);
FollowExt followFallback(const PartyMon &m);     // what reviveFrom builds when no record
bool followFind(uint32_t fp, FollowExt &out);
uint8_t followStored();
// Insert or refresh. A new record past 8 drops non-matching records first, then
// the oldest. The newcomer is kept even if it is not in the party yet: the
// switch banks it on the next line, and followSave prunes only after that.
void followStore(const FollowExt &e);
void followLoad();    // reads f_ext; finishes or deletes f_swap; then moves a legacy slot 5
// Header total. Slot 5 empty: banked count plus the follower (an egg counts).
// Slot 5 still occupied: party.count(), so six banked never reads as 7/6.
uint8_t followShownCount();
void followDrawCell();                 // cell 5, unless a legacy member still occupies it
bool followCellTap(int16_t x, int16_t y); // swallows that cell unless the member is still there
// Cell 6 of an empty slot 5, hatched follower only. Built from followSnapshot.
// An egg is swallowed by followCellTap and never opens this.
bool followSheetUp();
void followRenderSheet();             // flushes; FAREWELL only while canFarewellNow
void followDrawFarewell();
// Consumes the tap while the sheet is up. The button calls startFarewell().
bool followTapFarewell(int16_t x, int16_t y);
bool followSheetBack();               // a swipe closes the sheet, not the party under it
// Party sheet, hatched follower only. Writes the left button from followCanSwitch
// and FT. An egg returns false and leaves the caller's BRING BACK values alone.
bool followSheetLeft(uint8_t slot, bool *ok, const char **label, const char **why);
// Left button of that sheet. Success closes the sheet and the party. A refusal
// buzzes and leaves the sheet up. Returns true once the tap is handled.
bool followTapSwitch(uint8_t slot);
void followSave();    // drops records that match no party or box member, then writes
// The only answer the sheet, the tap and the serial command share.
// Success reports F_SWITCH. A pending learn offer reports F_LEARN_FIRST.
// Every other refusal reports F_NOT_NOW. Asleep is allowed. reason may be null.
bool followCanSwitch(uint8_t slot, FollowStr *reason);
// f_swap (slot + outgoing), then saveNow of the incoming creature, then
// replaceAt, then f_ext, then delete f_swap. A refusal writes nothing.
bool followSwitch(uint8_t slot);
bool followSerial(const String &line);   // FOLLOW lists records; FOLLOW <1-6> switches
const char *FT(FollowStr id);
