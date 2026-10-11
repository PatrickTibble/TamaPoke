#pragma once
// Wild battles: the screens and the glue into the battle code. Included ONCE,
// at the very bottom of TamaPoke.ino, so it shares the sketch's globals and
// statics without any change to arduino-cli's or genproto.py's prototypes.
// Everything the hooks above call is declared in wild.h.

// WILD takes the left half of the LAN button's row on the gym chooser.
#define WILDBTN_W LANBTN_W
#define WILDBTN_H LANBTN_H
#define WILDBTN_X (233 - 6 - WILDBTN_W)
#define WILDBTN_Y LANBTN_Y
static_assert(WILDBTN_X + WILDBTN_W < LANBTN_X, "the WILD button overlaps the LAN button");
static_assert(WILDBTN_W >= UI_TAP_MIN && WILDBTN_H >= UI_TAP_MIN, "WILD is smaller than a fingertip");

// The area list: six rows a page under a two-line header, BACK below. Rows
// tile the pitch with no dead pixels; the drawn box is a little shorter so
// neighbours do not touch.
#define WAREA_ROWS 6
#define WAREA_X 63
#define WAREA_W 340
#define WAREA_Y(i) (96 + (i) * WAREA_PITCH)
#define WAREA_PITCH 46
#define WAREA_H 44
#define WAREA_PAD 16
#define WAREA_GAP 16
#define WAREA_RIGHT_W 96
#define WAREA_BACK_Y 380
static_assert(WAREA_PITCH >= UI_TAP_MIN && WAREA_H >= UI_TAP_MIN, "an area row is smaller than a fingertip");
static_assert(WAREA_Y(WAREA_ROWS) <= WAREA_BACK_Y, "the last area row runs into BACK");
constexpr bool wareaOnPanel(int x, int y) { return (x - 233) * (x - 233) + (y - 233) * (y - 233) <= 233 * 233; }
static_assert(wareaOnPanel(WAREA_X, WAREA_Y(0)) && wareaOnPanel(WAREA_X + WAREA_W, WAREA_Y(0)) &&
              wareaOnPanel(WAREA_X, WAREA_Y(WAREA_ROWS)) && wareaOnPanel(WAREA_X + WAREA_W, WAREA_Y(WAREA_ROWS)),
              "an area row's corner falls off the round panel");
static_assert(WILD_ROW_LV_MAX * 12 <= WAREA_RIGHT_W && WILD_ROW_REASON_MAX * 6 <= WAREA_RIGHT_W,
              "the level range or a closed row's reason overflows the right column");
static_assert(WILD_NAME_MAX * 12 + WAREA_GAP + WAREA_RIGHT_W + 2 * WAREA_PAD <= WAREA_W,
              "a full-length area name runs into the right column");

bool btlWild = false;
bool wildPickOpen = false;
bool wildAreasShown = false;
uint8_t wildAreaPage = 0;
uint16_t wildArea = WILD_NO_AREA;
static uint8_t wildRegion = 0;

// Whether the pet can go at all. A locked region is not its business: the
// chooser greys those per row.
static WildRefusal wildPetRefusal() {
  WildRefusal w = wildCanStart(pet, REGION_ALL);
  return w == WILD_LOCKED ? WILD_OK : w;
}

uint8_t wildRpickMode() {
  if (wildAreasShown) return 0xFF;
  return wildPickOpen ? RPICK_FOR_WILD : RPICK_FOR_GYMS;
}

// ---- pages: a tier never shares a page, and a long tier overflows onto more.
// Derived from the region's span of the table every time; nothing is cached.
struct WildPage { uint16_t first; uint8_t n, tier, sub, subs; };

// The page count, and page `want` into `out`.
static uint8_t wildPages(uint8_t r, uint8_t want = 0xFF, WildPage *out = nullptr) {
  uint16_t a = wildAreaFirst(r), end = a + wildAreaCount(r);
  uint8_t p = 0;
  while (a < end) {
    uint8_t t = wildAreaTier(a);
    uint16_t e = a;
    while (e < end && wildAreaTier(e) == t) e++;
    uint8_t subs = (uint8_t)((e - a + WAREA_ROWS - 1) / WAREA_ROWS);
    for (uint8_t s = 0; s < subs; s++, p++) {
      if (p != want || !out) continue;
      uint16_t f = a + s * WAREA_ROWS;
      *out = { f, (uint8_t)(e - f < WAREA_ROWS ? e - f : WAREA_ROWS), t, s, subs };
    }
    a = e;
  }
  return p;
}

// The first page of the highest tier the badges have opened.
static uint8_t wildStartPage(uint8_t r) {
  uint8_t tier = wildTier(pet, r), pages = wildPages(r), best = 0, bestTier = 0;
  for (uint8_t p = 0; p < pages; p++) {
    WildPage pg;
    wildPages(r, p, &pg);
    if (pg.tier <= tier && pg.tier > bestTier) { bestTier = pg.tier; best = p; }
  }
  return best;
}

static uint8_t wildPageOf(uint8_t r, uint16_t a) {
  uint8_t pages = wildPages(r);
  for (uint8_t p = 0; p < pages; p++) {
    WildPage pg;
    wildPages(r, p, &pg);
    if (a >= pg.first && a < pg.first + pg.n) return p;
  }
  return 0;
}

uint16_t wildAreaAtRow(uint8_t row) {
  WildPage pg;
  if (row >= WAREA_ROWS || wildAreaPage >= wildPages(wildRegion, wildAreaPage, &pg) || row >= pg.n)
    return WILD_NO_AREA;
  return pg.first + row;
}

void wildAreaRowRect(uint8_t row, int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  x = WAREA_X; y = WAREA_Y(row); w = WAREA_W; h = WAREA_PITCH;
}

// ---- navigation. Every exit to the gym chooser goes through wildExitToGyms(),
// so a stale wildAreasShown can never reopen the last region's list on WILD.
static void wildShowAreas() { gymOpen = gymPick = wildPickOpen = wildAreasShown = true; }

void wildExitToGyms() {
  wildAreasShown = false;
  wildPickOpen = false;
  gymOpen = gymPick = true;
  rpickPage = 0;
}

// A greyed WILD still says why, inside the button, so it never reads as dead.
void wildDrawPick(uint8_t mode) {
  if (mode != RPICK_FOR_GYMS) return;
  WildRefusal why = wildPetRefusal();
  bool ok = why == WILD_OK;
  int cx = WILDBTN_X + WILDBTN_W / 2;
  gfx->fillRoundRect(WILDBTN_X, WILDBTN_Y, WILDBTN_W, WILDBTN_H, 11, UI_BG_DAY);
  gfx->drawRoundRect(WILDBTN_X, WILDBTN_Y, WILDBTN_W, WILDBTN_H, 11, ok ? UI_INK : UI_TRACK);
  gfx->setTextColor(ok ? UI_INK : UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(cx - (int)strlen(WT(W_BTN)) * 6, WILDBTN_Y + (ok ? 14 : 7));
  gfx->print(WT(W_BTN));
  if (ok) return;
  const char *t = wildRefusalText(why);
  gfx->setTextSize(1);
  gfx->setCursor(cx - (int)strlen(t) * 3, WILDBTN_Y + 29);
  gfx->print(t);
}

bool wildGymTap(int16_t x, int16_t y) {
  if (x < WILDBTN_X || x > WILDBTN_X + WILDBTN_W || y < WILDBTN_Y || y > WILDBTN_Y + WILDBTN_H)
    return false;
  if (wildPetRefusal() != WILD_OK) { sfxPlay(SFX_DENY); return true; }
  wildPickOpen = true;
  wildAreasShown = false;
  rpickPage = 0;
  sfxPlay(SFX_TAP);
  return true;
}

// The region chooser in wild mode: a region opens its area list.
void wildPickTap(int16_t x, int16_t y) {
  int r = regionPickTap(x, y, RPICK_FOR_WILD);
  if (r >= 0) {
    if (!wildRegionHasAreas((uint8_t)r)) {   // unreachable while gen_wild.py requires every document
      sfxPlay(SFX_DENY);
      Serial.printf("wild: %s has no areas\n", REGIONS[r].name);
      return;
    }
    wildRegion = (uint8_t)r;
    wildAreaPage = wildStartPage(wildRegion);
    wildAreasShown = true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (y > 380) wildExitToGyms();   // BACK
}

// Horizontal pages, clamped at both ends; it never closes the list.
bool wildAreaSwipe(int dir) {
  if (!wildAreasShown || uiCurrentScreen() != SCR_GYMPICK) return false;
  int p = (int)wildAreaPage + (dir > 0 ? -1 : 1);
  if (p >= 0 && p < wildPages(wildRegion)) {
    wildAreaPage = (uint8_t)p;
    sfxPlay(SFX_TAP);
  }
  return true;
}

void wildAreaTap(int16_t x, int16_t y) {
  if (y > WAREA_BACK_Y) { wildAreasShown = false; sfxPlay(SFX_TAP); return; }   // the region chooser
  if (x < WAREA_X || x > WAREA_X + WAREA_W || y < WAREA_Y(0)) return;
  uint16_t a = wildAreaAtRow((uint8_t)((y - WAREA_Y(0)) / WAREA_PITCH));
  if (a == WILD_NO_AREA) return;
  // It tired while the list was up: the gym chooser's WILD button says why.
  if (wildPetRefusal() != WILD_OK) { sfxPlay(SFX_DENY); wildExitToGyms(); return; }
  if (!wildAreaOpen(pet, a)) { sfxPlay(SFX_DENY); return; }
  wildArea = a;
  wildAreasShown = wildPickOpen = gymOpen = gymPick = false;
  pickTrainer = PICK_WILD;
  pickHard = false;
  pickPage = 0;
  pickDefault(squadCap(PICK_WILD, false));
  pickOpen = true;
  sfxPlay(SFX_TAP);
}

static void wildCentred(const char *s, int y, uint16_t col) {
  gfx->setTextColor(col);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(s) * 6, y);
  gfx->print(s);
}

void wildRenderAreas() {
  uint8_t pages = wildPages(wildRegion);
  if (pages && wildAreaPage >= pages) wildAreaPage = pages - 1;
  WildPage pg = { 0, 0, 1, 0, 1 };
  wildPages(wildRegion, wildAreaPage, &pg);
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  wildCentred(REGIONS[wildRegion % REGION_COUNT].name, 44, UI_INK);
  char head[32];
  const char *title = wildTierTitle(wildRegion, pg.tier);
  if (pg.subs > 1) snprintf(head, sizeof(head), "%s %u/%u", title, (unsigned)pg.sub + 1, (unsigned)pg.subs);
  else snprintf(head, sizeof(head), "%s", title);
  wildCentred(head, 68, pg.tier <= wildTier(pet, wildRegion) ? UI_INK : UI_TRACK);

  for (uint8_t i = 0; i < pg.n; i++) {
    uint16_t a = pg.first + i;
    WildRefusal why = wildAreaWhyClosed(pet, a);
    bool ok = why == WILD_OK;
    int y = WAREA_Y(i) + (WAREA_PITCH - WAREA_H) / 2;
    gfx->fillRoundRect(WAREA_X, y, WAREA_W, WAREA_H, 10, ok ? UI_WHITE : UI_BG_DAY);
    gfx->drawRoundRect(WAREA_X, y, WAREA_W, WAREA_H, 10, ok ? UI_INK : UI_TRACK);
    gfx->setTextColor(ok ? UI_INK : UI_TRACK);
    gfx->setTextSize(2);
    gfx->setCursor(WAREA_X + WAREA_PAD, y + 14);
    gfx->print(wildAreaName(a));
    char rt[24];
    int right = WAREA_X + WAREA_W - WAREA_PAD;
    gfx->setTextColor(UI_TRACK);
    if (ok) {
      uint8_t lo, hi;
      wildAreaLevels(pet, a, lo, hi);
      snprintf(rt, sizeof(rt), WT(W_LV_FMT), (unsigned)lo, (unsigned)hi);
      char *dash = lo == hi ? strchr(rt, '-') : nullptr;   // "Lv 12", not "Lv 12-12"
      if (dash) *dash = 0;
      gfx->setCursor(right - (int)strlen(rt) * 12, y + 14);
    } else {
      wildRowReason(why, wildAreaTier(a), rt, sizeof(rt));
      gfx->setTextSize(1);
      gfx->setCursor(right - (int)strlen(rt) * 6, y + 18);
    }
    gfx->print(rt);
  }
  wildCentred(T(S_BACK), 392, UI_TRACK);
  gfx->flush();
}

void wildPickHead(char *out, size_t n) { snprintf(out, n, "%s", wildAreaName(wildArea)); }

void wildPickBack() { wildShowAreas(); }

// The one routing rule for a fight that did not start or has ended: if the pet
// can still go, back to the list; if not, to the gym chooser, where WILD says why.
static void wildRoute() {
  if (wildPetRefusal() == WILD_OK) wildShowAreas();
  else wildExitToGyms();
}

void wildFight() {
  if (startWildBattle(wildRegion, wildArea) == WILD_OK) return;
  sfxPlay(SFX_DENY);
  wildRoute();
}

WildRefusal startWildBattle(uint8_t region, uint16_t area) {
  if (battleOpen) return WILD_NO_SQUAD;
  WildRefusal w = wildCanStart(pet, region);
  if (w != WILD_OK) return w;
  if (wildAreaRegion(area) != region) return WILD_AREA_LOCKED;
  if ((w = wildAreaWhyClosed(pet, area)) != WILD_OK) return w;
  uint8_t lvl = 0;
  int16_t dex = wildRollArea(pet, area, lvl);
  if (dex < 1) return WILD_AREA_LOCKED;
  btlWild = true;
  btlTrainer = -1;
  btlHard = false;
  btlLink = false;
  startBattle(dex, lvl);
  if (!battleOpen) { btlWild = false; return WILD_NO_SQUAD; }
  if (region != wildRegion || area != wildArea) {   // the console skips the screens
    wildRegion = region;
    wildArea = area;
    wildAreaPage = wildPageOf(region, area);
  }
  wildSpend(pet);
  wildMarkSeen(dex);
  btlSay(WT(W_APPEARED), DEX_TBL[dex].name);
  return WILD_OK;
}

// The battle box draws only the first WILD_MSG_SHOWN lines, and a final turn
// often fills them -- trainer fights move to the win screen, a wild one does
// not. Drop the OLDEST lines so the result is always on screen.
#define WILD_MSG_SHOWN 4
static void wildMakeRoom(uint8_t need) {
  while (btlMsgCount && btlMsgCount + need > WILD_MSG_SHOWN) {
    memmove(btlMsg[0], btlMsg[1], sizeof(btlMsg[0]) * (btlMsgCount - 1));
    btlMsgCount--;
  }
}

bool wildBattleOver() {
  btlTrainGain = wildReward(pet, btlWon, btlPetIn, btlTrainWhich);
  wildMakeRoom(btlTrainGain ? 2 : 1);
  audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
  if (btlWon) sfxPlay(SFX_VICTORY);
  btlSay("%s", T(btlWon ? S_BTL_WIN : S_BTL_LOSE));
  if (btlTrainGain) {
    static const StrId NAMES[3] = { S_TR_ATK, S_TR_DEF, S_TR_SPE };
    btlSay(WT(W_GAIN), T(NAMES[btlTrainWhich % 3]), btlTrainGain);
  }
  return true;
}

// Win, lose or run: back to the page the fight came from, unless the pet can
// no longer go -- a list whose rows all look open but bounce at FIGHT is worse.
void wildAfterBattle() {
  btlWild = false;
  wildRoute();
}

// WILD [region] -- a random open area of the region (default: the egg region),
// fought with the current squad.
bool wildSerial(const String &line) {
  if (!line.startsWith("WILD")) return false;
  int rg = pet.region;
  sscanf(line.c_str() + 4, "%d", &rg);
  if (battleOpen) {
    Serial.println("already in a battle");
  } else if (rg < 0 || rg >= REGION_COUNT) {
    Serial.printf("usage: WILD [region 0..%u]\n", REGION_COUNT - 1);
  } else {
    WildRefusal w = wildCanStart(pet, (uint8_t)rg);
    uint16_t a = w == WILD_OK ? wildRandomOpenArea(pet, (uint8_t)rg) : WILD_NO_AREA;
    if (w == WILD_OK && a == WILD_NO_AREA) {
      Serial.printf("refused: %s\n", WT(W_NO_AREA));
    } else if (w == WILD_OK && (w = startWildBattle((uint8_t)rg, a)) == WILD_OK) {
      Serial.printf("wild %s Lv.%u in %s, ene=%u\n", btlFoe.name, btlFoe.level,
                    wildAreaName(a), pet.energy);
    } else {
      const char *why = wildRefusalText(w);
      Serial.printf("refused: %s\n", *why ? why : "no squad");
    }
  }
  Serial.println("DONE");
  return true;
}
