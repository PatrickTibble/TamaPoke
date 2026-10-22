// Follower care records. Nothing here switches a creature or draws a screen.
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
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
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

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
