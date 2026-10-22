// Follower care records and the journalled switch. Nothing here draws a screen.
// Every "never" check has a companion that shows the mechanism engaged.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "follow.h"
#include "save.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
#include <vector>
uint32_t g_seed=41; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
static uint32_t g_ms=0; uint32_t millis(){return g_ms;} void FakeESP::restart(){exit(0);}
extern Pet pet;
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static PartyMon mk(int dex, int lvl, int iv) {
  PartyMon m;
  m.dex = dex; m.level = lvl;
  m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = iv;
  m.moves[0] = 1; m.moves[1] = 4;
  snprintf(m.nick, sizeof(m.nick), "N%d", dex);
  return m;
}

static void wipe() {
  nvs().clear();
  party.begin();
  followLoad();
}

static void boot(int16_t dex) {
  nvs().clear();
  party.begin();
  pet.begin();
  followLoad();
  if (dex > 0) pet.dbgHatchAs(dex, false);
  pet.learnQCount = 0;
  pet.ceremony = CER_NONE;
  pet.sleeping = false;
  pet.frozen = false;
}

static bool hasSwap() {
  Preferences p;
  if (!p.begin("tamapoke", true)) return false;
  bool y = p.isKey(FOLLOW_SWAP_KEY);
  p.end();
  return y;
}

static int copiesOf(int16_t dex) {
  int n = (!pet.isEgg() && pet.speciesId == dex) ? 1 : 0;
  for (int i = 0; i < PARTY_SLOTS; i++) if (party.slots[i].dex == dex) n++;
  for (int i = 0; i < BOX_SLOTS; i++) if (party.box[i].dex == dex) n++;
  return n;
}

static void plantSwap(uint8_t slot, const PartyMon &out) {
  uint8_t buf[1 + sizeof(PartyMon)];
  buf[0] = slot;
  memcpy(buf + 1, &out, sizeof(out));
  Preferences p;
  p.begin("tamapoke", false);
  p.putBytes(FOLLOW_SWAP_KEY, buf, sizeof(buf));
  p.end();
}

// Refusal must not move the follower. The companion call site switches afterwards.
static bool refused(uint8_t slot, FollowStr expect) {
  FollowStr why = F_SWITCH;
  int16_t was = pet.speciesId;
  bool egg = pet.isEgg();
  return !followCanSwitch(slot, &why) && why == expect && !followSwitch(slot)
      && pet.speciesId == was && pet.isEgg() == egg && !hasSwap();
}

static bool switchedTo(uint8_t slot) {
  int16_t want = party.slots[slot].dex;
  if (want < 1 || !followSwitch(slot)) return false;
  return pet.speciesId == want && !pet.frozen && !pet.sleeping && !hasSwap();
}

int main() {
  // --- a boot with nothing stored must not create a key
  {
    NvsStore before = nvs();
    followLoad();
    ck(nvs() == before, "boot with no journal writes nothing");
  }

  // --- the fingerprint ignores moves and still changes when the creature does
  {
    PartyMon m = mk(6, 40, 20);
    uint32_t h = followFingerprint(m);
    ck(h != 0, "a real creature hashes to something other than empty");
    m.moves[0] = 14; m.moves[2] = 9; m.moves[3] = 2;
    ck(followFingerprint(m) == h, "editing moves does not change the fingerprint");
    m.nick[0] = 'Z';
    ck(followFingerprint(m) != h, "a nick change does");
    PartyMon empty;
    ck(followFingerprint(empty) == 0, "an empty slot hashes to 0");
  }

  // --- a missing record is the fresh creature reviveFrom builds
  {
    PartyMon m = mk(25, 40, 16);
    FollowExt got;
    ck(!followFind(followFingerprint(m), got), "nothing stored is a miss");
    FollowExt e = followFallback(m);
    ck(e.fingerprint == followFingerprint(m), "the fallback names that creature");
    ck(e.ageMinutes == 39u * MINUTES_PER_LEVEL, "age comes from the banked level");
    ck(e.lastLearnLevel == 40, "learn gates already passed are not replayed");
    ck(e.fullness == 80 && e.joy == 80 && e.energy == 80 && e.hygiene == 100,
       "care stats are fresh");
    ck(e.bond == 0 && e.careMistakes == 0 && e.poops == 0 && e.weight == 0 &&
       e.goodTicks == 0 && e.farDeclinedAge == 0 && e.evoPen == 0,
       "and the rest starts clear");
    e.bond = 40;
    followStore(e);
    ck(followFind(e.fingerprint, got) && got.bond == 40,
       "a stored record is what comes back, not the fallback");
  }

  // --- prune drops strangers and keeps whoever is still banked
  {
    wipe();
    PartyMon keep = mk(1, 10, 8);
    party.replaceAt(0, keep);
    FollowExt live = followFallback(keep);
    live.bond = 7;
    FollowExt ghost = live;
    ghost.fingerprint = live.fingerprint ^ 0x1111u;
    if (!ghost.fingerprint) ghost.fingerprint = 1;
    followStore(live);
    followStore(ghost);
    followSave();
    FollowExt got;
    ck(followFind(live.fingerprint, got) && got.bond == 7, "a banked creature's record survives the save");
    ck(!followFind(ghost.fingerprint, got), "a record matching nobody is dropped");
    followLoad();
    ck(followFind(live.fingerprint, got) && got.bond == 7, "and the survivor reloads");
    ck(!followFind(ghost.fingerprint, got), "the stranger stays gone");
  }

  // --- the 9th matching record evicts the oldest, not the newest and not a middle one
  {
    wipe();
    uint32_t fp[9];
    for (int i = 0; i < 9; i++) {
      party.box[i] = mk(10 + i, 12, 3 + i);
      FollowExt e = followFallback(party.box[i]);
      e.bond = (uint8_t)(i + 1);
      fp[i] = e.fingerprint;
      followStore(e);
    }
    followSave();
    followLoad();
    FollowExt got;
    ck(!followFind(fp[0], got), "the oldest record is the one evicted");
    ck(followFind(fp[1], got) && got.bond == 2, "the next-oldest stays");
    ck(followFind(fp[8], got) && got.bond == 9, "the newcomer stays");
    ck(followStored() == FOLLOW_EXT_SLOTS, "the table is still 8");
  }

  // --- a full party, a full box and 8 records round-trip through the backup
  {
    wipe();
    Pet p;
    p.begin();
    for (int i = 0; i < PARTY_SLOTS; i++) party.replaceAt(i, mk(30 + i, 20 + i, 10));
    for (int i = 0; i < BOX_SLOTS; i++) party.box[i] = mk(50 + i, 15, 4);
    party.boxSave();
    uint32_t fp[FOLLOW_EXT_SLOTS];
    for (int i = 0; i < FOLLOW_EXT_SLOTS; i++) {
      FollowExt e = followFallback(party.box[i]);
      e.bond = (uint8_t)(20 + i);
      e.ageMinutes = 1000 + i;
      fp[i] = e.fingerprint;
      followStore(e);
    }
    followSave();
    size_t cap = saveExportSize();
    std::vector<uint8_t> buf(cap);
    size_t n = saveExport(buf.data(), cap);
    ck(n > 0 && n <= cap, "export with 8 ext records is not truncated");
    ck(saveImport(buf.data(), n), "and that blob imports");
    followLoad();
    bool back = followStored() == FOLLOW_EXT_SLOTS;
    FollowExt got;
    for (int i = 0; i < FOLLOW_EXT_SLOTS; i++)
      if (!followFind(fp[i], got) || got.bond != 20 + i || got.ageMinutes != 1000u + i) back = false;
    ck(back, "import restores every ext record");
  }

  // --- every fork string is present and ASCII in all six languages
  {
    bool ok = true;
    Lang was = gLang;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      gLang = (Lang)l;
      for (uint8_t id = 0; id < F_COUNT; id++) {
        const char *s = FT((FollowStr)id);
        if (!s || !s[0]) ok = false;
        for (const unsigned char *c = (const unsigned char *)s; s && *c; c++)
          if (*c >= 128) ok = false;
      }
      if (!strcmp(FT(F_SWITCH), FT(F_NOT_NOW))) ok = false;
    }
    gLang = was;
    ck(ok, "every FT() string is non-null and ASCII in all 6 languages");
  }

  // --- round trip: A's care comes back from the ext record, not from its level
  {
    boot(6);
    const uint32_t age = 40u * MINUTES_PER_LEVEL + 17;
    pet.ageMinutes = age;
    pet.bond = 42;
    pet.fullness = 61; pet.joy = 55; pet.energy = 70; pet.hygiene = 90;
    pet.poops = 2; pet.weight = 40; pet.careMistakes = 4; pet.berryKnown = true;
    pet.ivAtk = 21; pet.ivDef = 22; pet.ivSpe = 23; pet.ivHp = 24;
    pet.trAtk = 5; pet.trDef = 6; pet.trSpe = 7;
    pet.moves[0] = 3; pet.moves[1] = 6; pet.moves[2] = 9; pet.moves[3] = 12;
    pet.medals = 0x15;
    pet.lastLearnLevel = 33;
    snprintf(pet.nick, sizeof(pet.nick), "ACE");
    pet.badges = 0x2A;
    PartyMon b = mk(25, 18, 11);
    FollowExt be = followFallback(b);
    be.bond = 4;
    be.ageMinutes = 10u * MINUTES_PER_LEVEL + 3;
    be.lastLearnLevel = 255;
    be.fullness = 77;
    party.replaceAt(0, b);
    followStore(be);
    uint32_t aFp = followFingerprint(followSnapshot(pet).mon);
    ck(followSwitch(0), "switch A out for B");
    ck(pet.speciesId == 25 && pet.bond == 4 && pet.fullness == 77, "B arrives with its ext, not a fresh revive");
    ck(pet.lastLearnLevel == 255 && !pet.frozen && !pet.sleeping, "B is an awake follower");
    ck(party.slots[0].dex == 6 && !strcmp(party.slots[0].nick, "ACE"), "A is banked with its nick");
    ck(party.slots[0].moves[0] == 3 && party.slots[0].ivAtk == 21 && party.slots[0].level == 41,
       "A's banked level is the level it left at");
    FollowExt got;
    ck(followFind(aFp, got) && got.bond == 42 && got.ageMinutes == age && got.lastLearnLevel == 33,
       "A's ext is what was snapshotted, not the fallback");
    uint32_t bAge = pet.ageMinutes;
    pet.dbgTick();
    ck(pet.ageMinutes == bAge + 1, "ticking moves the follower");
    ck(followFind(aFp, got) && got.ageMinutes == age && got.bond == 42, "and leaves the banked ext where it was");
    ck(followSwitch(0), "switch A back");
    bool back = pet.speciesId == 6 && pet.ageMinutes == age && pet.bond == 42
        && pet.fullness == 61 && pet.joy == 55 && pet.energy == 70 && pet.hygiene == 90
        && pet.poops == 2 && pet.weight == 40 && pet.careMistakes == 4 && pet.berryKnown
        && pet.ivAtk == 21 && pet.ivDef == 22 && pet.ivSpe == 23 && pet.ivHp == 24
        && pet.trAtk == 5 && pet.trDef == 6 && pet.trSpe == 7
        && pet.moves[0] == 3 && pet.moves[1] == 6 && pet.moves[2] == 9 && pet.moves[3] == 12
        && !strcmp(pet.nick, "ACE") && pet.medals == 0x15 && pet.lastLearnLevel == 33
        && !pet.frozen && pet.badges == 0x2A && !hasSwap();
    ck(back, "A's age, bond, stats, mistakes, moves, nick and IVs match");
    ck(party.slots[0].dex == 25, "B is the one left in the slot");
  }

  // --- a banked creature does not age while the follower does
  {
    boot(1);
    pet.ageMinutes = 1000;
    pet.lastLearnLevel = 255;
    PartyMon m = mk(16, 22, 8);
    party.replaceAt(0, m);
    FollowExt e = followFallback(m);
    e.ageMinutes = 99999;
    e.bond = 77;
    e.fullness = 13;
    followStore(e);
    followSave();
    FollowExt before;
    ck(followFind(e.fingerprint, before), "the banked record is stored");
    for (int i = 0; i < 600; i++) pet.dbgTick();
    const uint32_t epoch = 1700000000UL;
    pet.dbgSetSeen(epoch);
    pet.syncClock(epoch + 14UL * 24 * 60 * 60);
    followLoad();
    FollowExt after;
    ck(followFind(e.fingerprint, after) && memcmp(&before, &after, sizeof(before)) == 0,
       "600 ticks and two weeks leave the banked ext untouched");
    ck(party.slots[0].level == 22 && party.slots[0].dex == 16, "and the banked level stays put");
    ck(pet.ageMinutes == 1000u + 600u + 14UL * 24 * 60, "the follower's age moves");
  }

  // --- each refusal, then the same slot switching once the reason is gone
  {
    boot(25);
    pet.newEgg();
    party.replaceAt(0, mk(1, 8, 4));
    ck(pet.isEgg() && !pet.awaitingStarter(), "an egg after the first creature is not the starter pick");
    ck(refused(0, F_NOT_NOW), "an egg cannot switch");
    pet.dbgHatchAs(4, false);
    pet.learnQCount = 0;
    ck(switchedTo(0), "the same slot switches once the egg has hatched");
  }
  {
    boot(0);
    pet.speciesId = 25;
    party.replaceAt(0, mk(1, 8, 4));
    ck(!pet.isEgg() && pet.awaitingStarter(), "the starter pick can be the thing refusing");
    ck(refused(0, F_NOT_NOW), "choosing a starter refuses");
    pet.chooseStarter(4);
    pet.learnQCount = 0;
    ck(!pet.awaitingStarter() && switchedTo(0), "once the starter is chosen the same slot switches");
  }
  {
    boot(4);
    party.replaceAt(0, mk(7, 9, 5));
    pet.ceremony = CER_FAREWELL;
    ck(refused(0, F_NOT_NOW), "a ceremony refuses");
    pet.ceremony = CER_NONE;
    ck(switchedTo(0), "the same slot switches once the ceremony is over");
  }
  {
    boot(4);
    party.replaceAt(0, mk(7, 9, 5));
    pet.learnQCount = 1;
    pet.learnQueue[0] = 3;
    ck(refused(0, F_LEARN_FIRST), "a learn offer refuses with its own reason");
    pet.learnQCount = 0;
    ck(switchedTo(0), "the same slot switches once the move is dealt with");
  }
  {
    boot(4);
    ck(party.slots[0].empty() && refused(0, F_NOT_NOW), "an empty slot refuses");
    party.replaceAt(0, mk(7, 9, 5));
    ck(switchedTo(0), "filling that slot lets the switch through");
  }
  {
    boot(4);
    party.replaceAt(0, mk(7, 9, 5));
    ck(refused(9, F_NOT_NOW), "a slot past the party refuses");
    ck(switchedTo(0), "a real slot still switches");
  }
  {
    boot(4);
    pet.ageMinutes = 39u * MINUTES_PER_LEVEL;
    pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
    pet.careMistakes = 0;
    party.replaceAt(0, mk(7, 9, 5));
    int16_t before = pet.speciesId;
    ck(pet.canEvolveNow(), "the evolution refusal has a creature that can evolve");
    g_ms = 0;
    pet.evolve();
    ck(pet.evolving() && pet.speciesId != before, "the animation is actually running");
    ck(refused(0, F_NOT_NOW), "the evolution animation refuses");
    // evolveUntil is private and sticks on this Pet. Later boots must stay
    // past it, or every switch looks refused for the wrong reason.
    g_ms = 100000;
    pet.learnQCount = 0;
    ck(!pet.evolving() && switchedTo(0), "once the animation ends the same slot switches");
  }
  {
    boot(4);
    party.replaceAt(0, mk(7, 9, 5));
    pet.sleeping = true;
    FollowStr why = F_NOT_NOW;
    ck(followCanSwitch(0, &why) && why == F_SWITCH, "asleep is allowed");
    ck(switchedTo(0), "and the creature that comes in is awake");
  }

  // --- the record follows the creature through the box and a move edit
  {
    boot(1);
    PartyMon m = mk(25, 30, 12);
    m.moves[0] = 3;
    party.replaceAt(2, m);
    FollowExt e = followFallback(m);
    e.bond = 55;
    e.ageMinutes = 29u * MINUTES_PER_LEVEL + 9;
    e.lastLearnLevel = 255;
    e.fullness = 61;
    followStore(e);
    uint32_t fp = e.fingerprint;
    party.swapPartyBox(2, 0);
    FollowExt got;
    ck(party.slots[2].empty() && followFind(followFingerprint(party.box[0]), got) && got.bond == 55,
       "a deposit keeps the record");
    party.box[0].moves[0] = 14;
    party.box[0].moves[1] = 8;
    party.boxSave();
    ck(followFingerprint(party.box[0]) == fp && followFind(fp, got) && got.bond == 55,
       "a move edit keeps the record");
    party.swapPartyBox(2, 0);
    ck(followSwitch(2) && pet.speciesId == 25 && pet.bond == 55
       && pet.ageMinutes == 29u * MINUTES_PER_LEVEL + 9 && pet.moves[0] == 14 && pet.fullness == 61,
       "withdraw then switch restores that record");
  }

  // --- a frozen follower comes back as a normal one
  {
    boot(6);
    pet.frozen = true;
    pet.bond = 9;
    pet.ageMinutes = 50u * MINUTES_PER_LEVEL + 3;
    pet.lastLearnLevel = 255;
    party.replaceAt(0, mk(25, 12, 8));
    FollowExt b = followFallback(party.slots[0]);
    b.bond = 4;
    b.lastLearnLevel = 255;
    followStore(b);
    ck(followSwitch(0) && pet.speciesId == 25 && !pet.frozen, "a frozen follower can leave");
    ck(followSwitch(0) && pet.speciesId == 6 && !pet.frozen && pet.bond == 9,
       "switched out and back, frozen is clear and the bond came back");
  }

  // --- both crash points. The journal is planted, which is all the next boot sees.
  {
    boot(4);
    pet.ageMinutes = 20u * MINUTES_PER_LEVEL;
    pet.ivAtk = 11; pet.ivDef = 12; pet.ivSpe = 13; pet.ivHp = 14;
    snprintf(pet.nick, sizeof(pet.nick), "IN");
    party.replaceAt(0, followSnapshot(pet).mon);
    PartyMon outgoing = mk(7, 15, 9);
    plantSwap(0, outgoing);
    ck(copiesOf(4) == 2 && copiesOf(7) == 0, "the crash is after the pet was saved and before replaceAt");
    followLoad();
    ck(pet.speciesId == 4 && party.slots[0].dex == 7, "recovery finishes replaceAt");
    ck(copiesOf(4) == 1 && copiesOf(7) == 1, "no duplicate and no loss");
    ck(!hasSwap(), "and the journal is deleted");
    followLoad();
    ck(pet.speciesId == 4 && party.slots[0].dex == 7, "the next boot leaves them where they landed");
  }
  {
    boot(4);
    snprintf(pet.nick, sizeof(pet.nick), "OUT");
    party.replaceAt(0, mk(7, 15, 9));
    plantSwap(0, followSnapshot(pet).mon);
    ck(copiesOf(4) == 1 && copiesOf(7) == 1, "the crash is before the pet was saved");
    followLoad();
    ck(pet.speciesId == 4 && party.slots[0].dex == 7, "recovery deletes the journal and changes nothing");
    ck(copiesOf(4) == 1 && copiesOf(7) == 1, "no duplicate and no loss");
    ck(!hasSwap(), "the journal is gone");
  }

  // --- FOLLOW lists, and FOLLOW <1-6> is the same switch
  {
    boot(4);
    party.replaceAt(0, mk(7, 10, 5));
    FollowExt e = followFallback(party.slots[0]);
    e.bond = 8;
    e.lastLearnLevel = 255;
    followStore(e);
    ck(followSerial(String("NOPE")) == false, "an unrelated line is not a FOLLOW command");
    ck(followSerial(String("FOLLOW")) && pet.speciesId == 4, "FOLLOW alone does not switch");
    ck(followSerial(String("FOLLOW 2")) && pet.speciesId == 4, "an empty FOLLOW slot refuses");
    ck(followSerial(String("FOLLOW 1")) && pet.speciesId == 7 && pet.bond == 8 && !hasSwap(),
       "FOLLOW 1 switches slot 1");
  }

  // --- cap of 5 banked. Slot 5 is not a free slot. Putting firstFree's loop
  // bound back to PARTY_SLOTS makes add() succeed here, and these checks fail.
  {
    wipe();
    for (int i = 1; i < PARTY_CAP; i++) party.replaceAt(i, mk(10 + i, 20, 5));
    party.slots[PARTY_CAP] = mk(77, 20, 9);
    ck(party.count() == PARTY_CAP && party.firstFree() == 0 && !party.isFull(),
       "a leftover in slot 5 does not fill the party");
    ck(followShownCount() == party.count(), "a stuck slot 5 is not counted a second time");
    wipe();
    for (int i = 0; i < PARTY_CAP; i++) party.replaceAt(i, mk(10 + i, 20, 5));
    ck(party.slots[PARTY_CAP].empty() && party.isFull() && party.firstFree() < 0,
       "5 banked fills the cap");
    ck(followShownCount() == 6, "five banked plus the follower reads 6");
    PartyMon neu = mk(99, 12, 4);
    bool toParty = party.add(neu);
    bool toBox = toParty ? false : party.boxAdd(neu);
    ck(!toParty && toBox && party.box[0].dex == 99 && party.slots[PARTY_CAP].empty(),
       "5 banked: add() falls through to the box");
    party.box[1] = mk(80, 15, 3);
    party.boxSave();
    int free = party.firstFree();
    if (free >= 0) party.swapPartyBox((uint8_t)free, 1);
    ck(free < 0 && party.box[1].dex == 80, "5 banked: box TO PARTY is refused");
    wipe();
    for (int i = 0; i < PARTY_CAP - 1; i++) party.replaceAt(i, mk(10 + i, 20, 5));
    ck(followShownCount() == 5, "four banked plus the follower reads 5");
    neu = mk(99, 12, 4);
    ck(party.add(neu) && party.slots[4].dex == 99 && party.slots[PARTY_CAP].empty(),
       "4 banked: add() succeeds");
    party.releaseAt(4);
    party.box[0] = mk(80, 15, 3);
    party.boxSave();
    free = party.firstFree();
    ck(free == 4, "the free slot is the last banked one");
    if (free >= 0) party.swapPartyBox((uint8_t)free, 0);
    ck(party.slots[4].dex == 80 && party.box[0].empty() && party.slots[PARTY_CAP].empty(),
       "4 banked: box TO PARTY succeeds");
  }

  // --- a legacy sixth member moves once, and a crash between the copy and
  // the clear leaves exactly one. Totals do not change on the move itself.
  {
    auto occupants = []() {
      int n = 0;
      for (int i = 0; i < PARTY_SLOTS; i++) if (!party.slots[i].empty()) n++;
      for (int i = 0; i < BOX_SLOTS; i++) if (!party.box[i].empty()) n++;
      return n;
    };
    auto plant = [](PartyMon *slots, PartyMon *box) {
      nvs().clear();
      Preferences p;
      p.begin("tamapoke", false);
      p.putBytes("party", slots, sizeof(PartyMon) * PARTY_SLOTS);
      if (box) p.putBytes("box", box, sizeof(PartyMon) * BOX_SLOTS);
      p.end();
      party.begin();
    };
    PartyMon slots[PARTY_SLOTS];
    PartyMon box[BOX_SLOTS];
    for (int i = 0; i < PARTY_SLOTS; i++) slots[i] = mk(30 + i, 10 + i, 5);
    slots[2] = PartyMon();
    plant(slots, nullptr);
    int before = occupants();
    int16_t sixth = 35;
    ck(party.slots[5].dex == sixth && party.slots[2].empty(), "the sixth starts in slot 5 with a hole behind it");
    followLoad();
    party.begin();
    ck(occupants() == before && party.slots[2].dex == sixth && party.slots[5].empty()
       && party.slots[0].dex == 30 && party.slots[4].dex == 34,
       "a hole in 0-4 receives the sixth, and the total is unchanged");

    for (int i = 0; i < PARTY_SLOTS; i++) slots[i] = mk(30 + i, 10 + i, 5);
    plant(slots, nullptr);
    before = occupants();
    followLoad();
    party.begin();
    ck(occupants() == before && party.slots[5].empty() && party.box[0].dex == sixth
       && party.slots[4].dex == 34 && party.boxCount() == 1,
       "with no hole it goes to the box, and the total is unchanged");

    for (int i = 0; i < PARTY_SLOTS; i++) slots[i] = mk(30 + i, 10 + i, 5);
    for (int i = 0; i < BOX_SLOTS; i++) box[i] = mk(100 + i, 8, 4);
    plant(slots, box);
    before = occupants();
    followLoad();
    party.begin();
    ck(before == PARTY_SLOTS + BOX_SLOTS && occupants() == before && party.slots[5].dex == sixth,
       "party and box both full: the sixth stays, and the total is unchanged");

    memset(slots, 0, sizeof(slots));
    slots[1] = slots[5] = mk(50, 22, 7);
    plant(slots, nullptr);
    ck(occupants() == 2, "the crash left the copy and slot 5 both occupied");
    followLoad();
    party.begin();
    int n = 0;
    for (int i = 0; i < PARTY_SLOTS; i++) if (party.slots[i].dex == 50) n++;
    for (int i = 0; i < BOX_SLOTS; i++) if (party.box[i].dex == 50) n++;
    ck(n == 1 && party.slots[5].empty() && party.slots[1].dex == 50 && occupants() == 1,
       "a crash between copy and clear leaves exactly one copy");

    memset(slots, 0, sizeof(slots));
    memset(box, 0, sizeof(box));
    slots[5] = box[3] = mk(51, 22, 8);
    plant(slots, box);
    followLoad();
    party.begin();
    n = 0;
    for (int i = 0; i < PARTY_SLOTS; i++) if (party.slots[i].dex == 51) n++;
    for (int i = 0; i < BOX_SLOTS; i++) if (party.box[i].dex == 51) n++;
    ck(n == 1 && party.slots[5].empty() && party.box[3].dex == 51 && party.count() == 0,
       "the same crash after a box copy does not put a second one in the party");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
