#include "wild.h"
#include "i18n.h"
#include "noart.h"

static uint8_t seen[(DEX_COUNT + 7) / 8];
static Preferences wprefs;

static bool wildPlayable(int16_t d) {
  return d >= 1 && d <= DEX_COUNT && regionAvailable(regionOfDex(d)) && speciesHasArt(d);
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
  { "SALVAJE", "ZONA SALVAJE", "VISTOS %u/%u", "Un %s salvaje aparecio!",
    "El %s salvaje se debilito!", "%s +%u!",
    "Primero debe nacer", "Esta dormido", "Esta muy cansado", "Falta el pack" },
  { "WILD", "WILD AREA", "SEEN %u/%u", "A wild %s appeared!",
    "The wild %s fainted!", "%s +%u!",
    "Hatch it first", "It is asleep", "Too tired", "Needs pack" },
  { "SAUVAGE", "ZONE SAUVAGE", "VUS %u/%u", "Un %s sauvage apparait!",
    "Le %s sauvage est K.O.!", "%s +%u!",
    "Il doit eclore", "Il dort", "Trop fatigue", "Pack manquant" },
  { "WILD", "WILDNIS", "GESEHEN %u/%u", "Ein wildes %s erscheint!",
    "Das wilde %s ist besiegt!", "%s +%u!",
    "Erst ausbrueten", "Es schlaeft", "Zu muede", "Paket fehlt" },
  { "SELVATICO", "ZONA SELVATICA", "VISTI %u/%u", "Appare un %s selvatico!",
    "Il %s selvatico e esausto!", "%s +%u!",
    "Deve schiudersi", "Sta dormendo", "Troppo stanco", "Manca il pack" },
  { "SELVAGEM", "ZONA SELVAGEM", "VISTOS %u/%u", "Um %s selvagem apareceu!",
    "O %s selvagem desmaiou!", "%s +%u!",
    "Deve chocar antes", "Esta dormindo", "Muito cansado", "Falta o pack" },
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
    default: return "";
  }
}
