#include "wild.h"
#include "i18n.h"
#include "noart.h"
#include "wild_areas.h"

static uint8_t seen[(DEX_COUNT + 7) / 8];
static Preferences wprefs;

static bool wildPlayable(int16_t d) {
  return d >= 1 && d <= DEX_COUNT && regionAvailable(regionOfDex(d)) && speciesHasArt(d);
}

// ---- Encounter areas

uint16_t wildAreaFirst(uint8_t r) { return r < REGION_COUNT ? WILD_REGION_AREAS[r].first : 0; }
uint16_t wildAreaCount(uint8_t r) { return r < REGION_COUNT ? WILD_REGION_AREAS[r].count : 0; }
bool wildRegionHasAreas(uint8_t r) { return wildAreaCount(r) > 0; }
const char *wildAreaName(uint16_t a) { return a < WILD_AREA_COUNT ? WILD_AREAS[a].name : ""; }
uint8_t wildAreaTier(uint16_t a) { return a < WILD_AREA_COUNT ? WILD_AREAS[a].tier : 0; }
uint8_t wildAreaRegion(uint16_t a) { return a < WILD_AREA_COUNT ? WILD_AREAS[a].region : REGION_ALL; }
uint16_t wildSlotTotal() { return WILD_SLOT_COUNT; }

const char *wildTierTitle(uint8_t r, uint8_t tier) {
  for (uint16_t i = 0; i < WILD_TIER_COUNT; i++)
    if (WILD_TIERS[i].region == r && WILD_TIERS[i].tier == tier) return WILD_TIERS[i].title;
  return "";
}

// gen_wild.py emits an area's slots contiguously and in area order inside its
// region's span. A slot outside that is invisible here, which is why wild_test
// sums wildSlotCount() against wildSlotTotal().
static const WildSlot *areaSlots(uint16_t a, uint16_t &n) {
  n = 0;
  if (a >= WILD_AREA_COUNT) return WILD_SLOTS;
  const WildSpan &sp = WILD_REGION_SLOTS[WILD_AREAS[a].region];
  uint16_t i = sp.first, end = sp.first + sp.count;
  while (i < end && WILD_SLOTS[i].area != a) i++;
  uint16_t f = i;
  while (i < end && WILD_SLOTS[i].area == a) i++;
  n = i - f;
  return WILD_SLOTS + f;
}

uint16_t wildSlotCount(uint16_t a) { uint16_t n; areaSlots(a, n); return n; }

bool wildSlotAt(uint16_t a, uint16_t i, WildSlotView &o) {
  uint16_t n;
  const WildSlot *s = areaSlots(a, n);
  if (i >= n) return false;
  o = { s[i].dex, s[i].weight, s[i].tier, s[i].lo, s[i].hi, s[i].tmask };
  return true;
}

// Bits 8-12 are the Elite 4 and the Champion, hence the TRAINER_GYMS cap.
static uint8_t badgeRun(uint16_t m) {
  uint8_t n = 0;
  while (n < TRAINER_GYMS && ((m >> n) & 1)) n++;
  return n;
}

uint8_t wildTier(const Pet &p, uint8_t r) {
  uint8_t e = badgeRun(p.badgeMask(r, false)), h = badgeRun(p.badgeMask(r, true));
  return 1 + (e > h ? e : h);
}

// Same source and formula as Pet::isNightHour().
int8_t wildHour(const Pet &p) {
  return p.lastSeenEpoch ? (int8_t)((p.lastSeenEpoch / 3600) % 24) : -1;
}

static_assert(WILD_MORNING_FROM < WILD_DAY_FROM && WILD_DAY_FROM < WILD_NIGHT_FROM,
              "wildTimeMask() assumes only night crosses midnight");

uint8_t wildTimeMask(const Pet &p) {
  int8_t h = wildHour(p);
  if (h < 0) return WILD_TM_ALL;
  if (h >= WILD_MORNING_FROM && h < WILD_DAY_FROM) return WILD_TM_MORNING;
  if (h >= WILD_DAY_FROM && h < WILD_NIGHT_FROM) return WILD_TM_DAY;
  return WILD_TM_NIGHT;
}

// How far one slot gets through the filters, in the order the reasons rank.
enum : uint8_t { GATE_TIER, GATE_TIME, GATE_PACK, GATE_LIVE };

template <class S> static uint8_t slotGate(const S &s, uint8_t tier, uint8_t tm) {
  if (s.tier > tier) return GATE_TIER;
  if (!(s.tmask & tm)) return GATE_TIME;
  if (!wildPlayable(s.dex)) return GATE_PACK;
  return GATE_LIVE;
}

template <class S>
static WildRefusal whyClosed(const S *s, uint16_t n, uint8_t areaTier, uint8_t tier, uint8_t tm) {
  if (areaTier > tier) return WILD_AREA_LOCKED;
  uint8_t best = GATE_TIER;
  for (uint16_t i = 0; i < n && best < GATE_LIVE; i++) {
    uint8_t g = slotGate(s[i], tier, tm);
    if (g > best) best = g;
  }
  switch (best) {
    case GATE_LIVE: return WILD_OK;
    case GATE_PACK: return WILD_LOCKED;
    case GATE_TIME: return WILD_AREA_NOT_NOW;
    default: return WILD_AREA_LOCKED;
  }
}

WildRefusal wildWhyClosedOf(const WildSlotView *s, uint16_t n, uint8_t areaTier, uint8_t tier, uint8_t tm) {
  return whyClosed(s, n, areaTier, tier, tm);
}

WildRefusal wildAreaWhyClosed(const Pet &p, uint16_t a) {
  if (a >= WILD_AREA_COUNT) return WILD_AREA_LOCKED;
  uint16_t n;
  const WildSlot *s = areaSlots(a, n);
  return whyClosed(s, n, WILD_AREAS[a].tier, wildTier(p, WILD_AREAS[a].region), wildTimeMask(p));
}

bool wildAreaOpen(const Pet &p, uint16_t a) { return wildAreaWhyClosed(p, a) == WILD_OK; }

// An open area's slots and the gate they are filtered by; n = 0 when closed.
static const WildSlot *liveSlots(const Pet &p, uint16_t a, uint16_t &n, uint8_t &tier, uint8_t &tm) {
  n = 0;
  if (!wildAreaOpen(p, a)) return WILD_SLOTS;
  tier = wildTier(p, WILD_AREAS[a].region);
  tm = wildTimeMask(p);
  return areaSlots(a, n);
}

int16_t wildRollArea(const Pet &p, uint16_t a, uint8_t &lvl) {
  lvl = 0;
  uint16_t n; uint8_t tier = 0, tm = 0;
  const WildSlot *s = liveSlots(p, a, n, tier, tm);
  uint32_t total = 0;
  for (uint16_t i = 0; i < n; i++) if (slotGate(s[i], tier, tm) == GATE_LIVE) total += s[i].weight;
  if (!total) return -1;
  long k = random((long)total);
  for (uint16_t i = 0; i < n; i++) {
    if (slotGate(s[i], tier, tm) != GATE_LIVE) continue;
    if (k < s[i].weight) {
      lvl = s[i].lo + random(s[i].hi - s[i].lo + 1);
      return s[i].dex;
    }
    k -= s[i].weight;
  }
  return -1;
}

void wildAreaLevels(const Pet &p, uint16_t a, uint8_t &lo, uint8_t &hi) {
  lo = hi = 0;
  uint16_t n; uint8_t tier = 0, tm = 0;
  const WildSlot *s = liveSlots(p, a, n, tier, tm);
  for (uint16_t i = 0; i < n; i++) {
    if (slotGate(s[i], tier, tm) != GATE_LIVE) continue;
    if (!lo || s[i].lo < lo) lo = s[i].lo;
    if (s[i].hi > hi) hi = s[i].hi;
  }
}

static bool inRegion(int16_t d, uint8_t region) {
  return region == REGION_ALL || regionOfDex(d) == region;
}

static bool isWildBase(int16_t d, uint8_t tier) {
  return DEX_TBL[d].rarity == tier && wildPlayable(d);
}

// Count, then pick the k-th: no candidate buffer, so REGION_ALL's few hundred
// commons cost no stack.
static int16_t pickTier(const RegionInfo &rg, uint8_t tier) {
  uint16_t n = 0;
  for (int16_t d = rg.lo; d <= rg.hi && d <= DEX_COUNT; d++) if (isWildBase(d, tier)) n++;
  if (!n) return -1;
  long k = random(n);
  for (int16_t d = rg.lo; d <= rg.hi && d <= DEX_COUNT; d++)
    if (isWildBase(d, tier) && k-- == 0) return d;
  return -1;
}

int16_t wildEvolveFor(int16_t d, uint8_t lvl, uint8_t region) {
  while (d >= 1 && d <= DEX_COUNT && d != DEX_EEVEE) {
    const DexEntry &e = DEX_TBL[d];
    if (!e.evolvesTo || lvl < e.evolveLevel) break;
    int16_t n = (int16_t)e.evolvesTo;
    if (!wildPlayable(n) || !inRegion(n, region) || DEX_TBL[n].rarity == R_LEGENDARIO) break;
    d = n;
  }
  return d;
}

int16_t wildRoll(uint8_t region, uint8_t lvl) {
  if (region >= REGION_COUNT || !regionAvailable(region)) return -1;
  const RegionInfo &rg = REGIONS[region];
  uint8_t tier = random(100) < WILD_COMMON_PCT ? R_COMUN : R_RARO;
  int16_t d = pickTier(rg, tier);
  if (d < 0) d = pickTier(rg, tier == R_COMUN ? R_RARO : R_COMUN);
  return d < 0 ? -1 : wildEvolveFor(d, lvl, region);
}

uint8_t wildLevelFor(uint8_t top) {
  int l = (int)top - (int)random(WILD_LEVEL_SPREAD + 1);
  if (l < WILD_MIN_LEVEL) l = WILD_MIN_LEVEL;
  if (l > MAX_LEVEL) l = MAX_LEVEL;
  return (uint8_t)l;
}

WildRefusal wildCanStart(const Pet &p, uint8_t region) {
  if (p.isEgg()) return WILD_EGG;
  if (p.sleeping) return WILD_ASLEEP;
  if (p.energy < WILD_ENERGY_MIN) return WILD_TIRED;
  if (region >= REGION_COUNT || !regionAvailable(region)) return WILD_LOCKED;
  return WILD_OK;
}

void wildSpend(Pet &p) {
  p.energy = p.energy > WILD_ENERGY_COST ? p.energy - WILD_ENERGY_COST : 0;
  p.saveNow();
}

uint8_t wildReward(Pet &p, bool won, bool petFought, uint8_t &which) {
  which = 0;
  if (!won || !petFought) return 0;
  return p.rewardTraining(WILD_REWARD_MIN + random(WILD_REWARD_SPAN), which);
}

// Tolerates a blob from another build: absent stays zero, a short one fills
// the front, a long one keeps the prefix. getBytes() copies NOTHING when the
// blob is larger than the buffer (see loadBlob in pet.cpp, static there).
void wildLoad() {
  memset(seen, 0, sizeof(seen));
  if (!wprefs.begin("tamapoke", true)) return;
  size_t have = wprefs.getBytesLength(WILD_SEEN_KEY);
  if (have && have <= sizeof(seen)) {
    wprefs.getBytes(WILD_SEEN_KEY, seen, sizeof(seen));
  } else if (have) {
    uint8_t *tmp = (uint8_t *)malloc(have);
    if (tmp && wprefs.getBytes(WILD_SEEN_KEY, tmp, have) == have) memcpy(seen, tmp, sizeof(seen));
    free(tmp);
  }
  wprefs.end();
}

void wildMarkSeen(int16_t d) {
  if (d < 1 || d > DEX_COUNT) return;
  uint8_t bit = (uint8_t)(1u << ((d - 1) & 7));
  if (seen[(d - 1) >> 3] & bit) return;
  seen[(d - 1) >> 3] |= bit;
  wprefs.begin("tamapoke", false);
  wprefs.putBytes(WILD_SEEN_KEY, seen, sizeof(seen));
  wprefs.end();
}

bool wildSeen(const Pet &p, int16_t d) {
  if (d < 1 || d > DEX_COUNT) return false;
  return (seen[(d - 1) >> 3] & (1u << ((d - 1) & 7))) || p.isRegistered(d);
}

uint16_t wildSeenCountIn(const Pet &p, uint8_t region) {
  if (region >= REGION_COUNT) return 0;
  uint16_t n = 0;
  for (int16_t d = REGIONS[region].lo; d <= REGIONS[region].hi && d <= DEX_COUNT; d++)
    if (wildSeen(p, d)) n++;
  return n;
}

// Rows follow the Lang enum: ES, EN, FR, DE, IT, PT.
static const char *const W_STRINGS[LANG_COUNT][W_COUNT] = {
  { "SALVAJE", "ZONA SALVAJE", "Un %s salvaje aparecio!", "%s +%u!",
    "Primero debe nacer", "Esta dormido", "Esta muy cansado", "Falta el pack",
    "Ahora no" },
  { "WILD", "WILD AREA", "A wild %s appeared!", "%s +%u!",
    "Hatch it first", "It is asleep", "Too tired", "Needs pack",
    "Not now" },
  { "SAUVAGE", "ZONE SAUVAGE", "Un %s sauvage apparait!", "%s +%u!",
    "Il doit eclore", "Il dort", "Trop fatigue", "Pack manquant",
    "Pas maintenant" },
  { "WILD", "WILDNIS", "Ein wildes %s erscheint!", "%s +%u!",
    "Erst ausbrueten", "Es schlaeft", "Zu muede", "Paket fehlt",
    "Nicht jetzt" },
  { "SELVATICO", "ZONA SELVATICA", "Appare un %s selvatico!", "%s +%u!",
    "Deve schiudersi", "Sta dormendo", "Troppo stanco", "Manca il pack",
    "Non ora" },
  { "SELVAGEM", "ZONA SELVAGEM", "Um %s selvagem apareceu!", "%s +%u!",
    "Deve chocar antes", "Esta dormindo", "Muito cansado", "Falta o pack",
    "Agora nao" },
};

const char *WT(WildStr s) {
  if (s >= W_COUNT) return "";
  uint8_t l = gLang < LANG_COUNT ? gLang : LANG_EN;
  const char *r = W_STRINGS[l][s];
  return r ? r : "";
}

const char *wildRefusalText(WildRefusal r) {
  switch (r) {
    case WILD_EGG: return WT(W_NO_EGG);
    case WILD_ASLEEP: return WT(W_NO_SLEEP);
    case WILD_TIRED: return WT(W_NO_ENERGY);
    case WILD_LOCKED: return WT(W_NO_PACK);
    case WILD_AREA_NOT_NOW: return WT(W_NOT_NOW);
    default: return "";   // WILD_AREA_LOCKED: the area screen formats its badge number
  }
}
