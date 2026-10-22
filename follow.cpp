#include "follow.h"
#include "pet.h"
#include "i18n.h"
#include <stdlib.h>
#include <string.h>

// Headless tests link this file without the sketch, which is where `Pet pet`
// otherwise lives. The sketch's definition is strong and wins on the device.
Pet pet __attribute__((weak));

static FollowExt rec[FOLLOW_EXT_SLOTS];
static uint8_t recN = 0;

static void mix(uint32_t &h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
}

uint32_t followFingerprint(const PartyMon &m) {
  if (m.empty()) return 0;
  uint32_t h = 2166136261u;
  mix(h, &m.dex, sizeof(m.dex));
  mix(h, &m.level, sizeof(m.level));
  mix(h, &m.medals, sizeof(m.medals));
  mix(h, &m.ivAtk, 1); mix(h, &m.ivDef, 1); mix(h, &m.ivSpe, 1); mix(h, &m.ivHp, 1);
  mix(h, &m.trAtk, 1); mix(h, &m.trDef, 1); mix(h, &m.trSpe, 1);
  mix(h, &m.shiny, 1);
  mix(h, m.nick, sizeof(m.nick));
  return h ? h : 1;   // 0 is reserved for "no record"
}

void FollowIO::capture(const Pet &p, FollowExt &e) {
  e.ageMinutes = p.ageMinutes;
  e.farDeclinedAge = p.farDeclinedAge;
  e.fullness = p.fullness;
  e.joy = p.joy;
  e.energy = p.energy;
  e.hygiene = p.hygiene;
  e.poops = p.poops;
  e.weight = p.weight;
  e.bond = p.bond;
  e.careMistakes = p.careMistakes;
  e.mistakeCooldown = p.mistakeCooldown;
  e.neglectTicks = p.neglectTicks;
  e.evoPen = p.evoPen;
  e.evoDeclinedLv = p.evoDeclinedLv;
  e.lastLearnLevel = p.lastLearnLevel;
  e.berryKnown = p.berryKnown ? 1 : 0;
  e.goodTicks = p.goodTicks;
}

FollowSnap followSnapshot(const Pet &p) {
  FollowSnap s;
  memset(&s, 0, sizeof(s));
  if (p.speciesId >= 1) {
    s.mon.dex = p.speciesId;
    s.mon.level = (uint16_t)p.level();
    s.mon.medals = p.medals;
    s.mon.ivAtk = p.ivAtk; s.mon.ivDef = p.ivDef; s.mon.ivSpe = p.ivSpe; s.mon.ivHp = p.ivHp;
    s.mon.trAtk = p.trAtk; s.mon.trDef = p.trDef; s.mon.trSpe = p.trSpe;
    s.mon.shiny = p.shiny ? 1 : 0;
    memcpy(s.mon.moves, p.moves, sizeof(s.mon.moves));
    strncpy(s.mon.nick, p.nick, sizeof(s.mon.nick) - 1);
  }
  FollowIO::capture(p, s.ext);
  s.ext.fingerprint = followFingerprint(s.mon);
  return s;
}

FollowExt followFallback(const PartyMon &m) {
  FollowExt e;
  memset(&e, 0, sizeof(e));
  e.fingerprint = followFingerprint(m);
  e.ageMinutes = (uint32_t)(m.level ? m.level - 1 : 0) * MINUTES_PER_LEVEL;
  e.lastLearnLevel = m.level > 255 ? 255 : (uint8_t)m.level;
  e.fullness = e.joy = e.energy = 80;
  e.hygiene = 100;
  return e;
}

bool followFind(uint32_t fp, FollowExt &out) {
  if (!fp) return false;
  for (uint8_t i = 0; i < recN; i++) {
    if (rec[i].fingerprint != fp) continue;
    out = rec[i];
    return true;
  }
  return false;
}

uint8_t followStored() { return recN; }

static bool memberHas(uint32_t fp) {
  if (!fp) return false;
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (!party.slots[i].empty() && followFingerprint(party.slots[i]) == fp) return true;
  for (int i = 0; i < BOX_SLOTS; i++)
    if (!party.box[i].empty() && followFingerprint(party.box[i]) == fp) return true;
  return false;
}

static void prune() {
  uint8_t w = 0;
  for (uint8_t i = 0; i < recN; i++)
    if (memberHas(rec[i].fingerprint)) rec[w++] = rec[i];
  recN = w;
}

void followStore(const FollowExt &e) {
  if (!e.fingerprint) return;
  for (uint8_t i = 0; i < recN; i++) {
    if (rec[i].fingerprint != e.fingerprint) continue;
    memmove(&rec[i], &rec[i + 1], (recN - i - 1) * sizeof(FollowExt));
    rec[recN - 1] = e;
    return;
  }
  if (recN == FOLLOW_EXT_SLOTS) {
    prune();
    if (recN == FOLLOW_EXT_SLOTS) {
      memmove(&rec[0], &rec[1], (FOLLOW_EXT_SLOTS - 1) * sizeof(FollowExt));
      recN = FOLLOW_EXT_SLOTS - 1;
    }
  }
  rec[recN++] = e;
}

static void loadExt(Preferences &p) {
  recN = 0;
  size_t n = p.getBytesLength(FOLLOW_EXT_KEY);
  if (!n) return;
  uint8_t *tmp = (uint8_t *)malloc(n);
  if (tmp && p.getBytes(FOLLOW_EXT_KEY, tmp, n) == n) {
    size_t keep = n > sizeof(rec) ? sizeof(rec) : n;
    keep -= keep % sizeof(FollowExt);
    memcpy(rec, tmp, keep);
    recN = (uint8_t)(keep / sizeof(FollowExt));
    uint8_t w = 0;
    for (uint8_t i = 0; i < recN; i++)
      if (rec[i].fingerprint) rec[w++] = rec[i];
    recN = w;
  }
  free(tmp);
}

// f_swap is one byte of slot index followed by the outgoing PartyMon, no pad.
// The incoming creature is whoever still sits in that slot. If the live pet's
// fingerprint matches it, saveNow already ran and replaceAt did not.
static void recoverSwap(Preferences &p) {
  const size_t need = 1 + sizeof(PartyMon);
  size_t n = p.getBytesLength(FOLLOW_SWAP_KEY);
  uint8_t buf[1 + sizeof(PartyMon)];
  bool finish = false;
  uint8_t slot = 0;
  PartyMon outgoing;
  if (n == need && p.getBytes(FOLLOW_SWAP_KEY, buf, need) == need) {
    slot = buf[0];
    memcpy(&outgoing, buf + 1, sizeof(outgoing));
    if (slot < PARTY_SLOTS && !outgoing.empty() && !party.slots[slot].empty()) {
      uint32_t petFp = followFingerprint(followSnapshot(pet).mon);
      uint32_t slotFp = followFingerprint(party.slots[slot]);
      uint32_t outFp = followFingerprint(outgoing);
      if (petFp && petFp == slotFp && petFp != outFp) finish = true;
    }
  }
  if (finish) party.replaceAt(slot, outgoing);
  p.remove(FOLLOW_SWAP_KEY);
}

void followLoad() {
  memset(rec, 0, sizeof(rec));
  recN = 0;
  bool swap = false;
  {
    Preferences p;
    if (!p.begin("tamapoke", true)) return;
    loadExt(p);
    swap = p.isKey(FOLLOW_SWAP_KEY);
    p.end();
  }
  if (!swap) return;
  Preferences p;
  if (!p.begin("tamapoke", false)) return;
  recoverSwap(p);
  p.end();
}

void followSave() {
  prune();
  Preferences p;
  if (!p.begin("tamapoke", false)) return;
  if (!recN) {
    if (p.isKey(FOLLOW_EXT_KEY)) p.remove(FOLLOW_EXT_KEY);
  } else {
    p.putBytes(FOLLOW_EXT_KEY, rec, recN * sizeof(FollowExt));
  }
  p.end();
}

// Rows follow the Lang enum: ES, EN, FR, DE, IT, PT. ASCII only.
static const char *const F_STRINGS[LANG_COUNT][F_COUNT] = {
  { "CAMBIAR", "APRENDE EL MOVIMIENTO PRIMERO", "AHORA NO" },
  { "SWITCH", "LEARN THE MOVE FIRST", "NOT NOW" },
  { "CHANGER", "APPRENDS LE MOUVEMENT", "PAS MAINTENANT" },
  { "WECHSELN", "LERNE ZUERST DIE ATTACKE", "NICHT JETZT" },
  { "CAMBIA", "IMPARA PRIMA LA MOSSA", "NON ORA" },
  { "TROCAR", "APRENDE O GOLPE ANTES", "AGORA NAO" },
};

const char *FT(FollowStr id) {
  if ((unsigned)id >= F_COUNT) return "";
  uint8_t l = gLang < LANG_COUNT ? (uint8_t)gLang : (uint8_t)LANG_EN;
  const char *s = F_STRINGS[l][id];
  return s ? s : "";
}
