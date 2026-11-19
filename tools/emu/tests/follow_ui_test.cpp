// Party header, follower cell, and the banked sheet's SWITCH button. Driven
// through render() and partyTap() so the FORK(follow) hooks are what gets tested.
// Fresh screen state before each check.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "follow.h"
#include "dex.h"
#include "species.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed = 19;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup();
void loop();
void render();
void partyTap(int16_t, int16_t);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool cardOpen, galleryOpen, clockOpen, kbOpen, menuOpen, partyOpen, partyPick;
extern bool trainOpen, movePickOpen, battleOpen, gymOpen, playerOpen;
extern bool boxOpen, releaseConfirm;
extern uint8_t choiceKind;
extern uint32_t choiceUntil, partyBannerUntil;
extern uint8_t partyDetail, boxDetail, boxSwapFrom, boxSel, playerPage;
extern const uint8_t GLCD_FONT[];

static int bad = 0;
static void ck(bool ok, const char *w) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", w);
  if (!ok) bad++;
}
static bool on(const char *want) {
  const char *got = SCREEN_NAME[uiCurrentScreen()];
  if (!strcmp(got, want)) return true;
  printf("FAIL  expected the '%s' screen, on '%s'\n", want, got);
  bad++;
  return false;
}

static void clearScreens() {
  cardOpen = galleryOpen = clockOpen = kbOpen = menuOpen = partyOpen = partyPick = false;
  trainOpen = movePickOpen = battleOpen = gymOpen = playerOpen = false;
  boxOpen = releaseConfirm = false;
  partyDetail = boxDetail = boxSwapFrom = boxSel = playerPage = 0;
}

static void emptyParty() {
  for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  for (int i = 0; i < BOX_SLOTS; i++) party.boxReleaseAt(i);
  clearScreens();
}

static PartyMon mon(int dex, int lvl, const char *nick) {
  PartyMon m;
  m.dex = dex;
  m.level = lvl;
  m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 12;
  snprintf(m.nick, sizeof(m.nick), "%s", nick);
  return m;
}

#define CELL_X(i) (78 + ((i) % 2) * 160)
#define CELL_Y(i) (88 + ((i) / 2) * 78)
#define CELL_W 150
#define CELL_H 70
static void tapCell(int i) { partyTap(CELL_X(i) + CELL_W / 2, CELL_Y(i) + CELL_H / 2); }
static void leaveFollowerSheet() {
  if (followSheetUp()) partyTap(20, 20);
}

static uint16_t pix(int x, int y) { return gfx->buffer()[y * gfx->_w + x]; }
static bool glyphAt(int x, int y, char ch, int size, uint16_t col) {
  const uint16_t *fb = gfx->buffer();
  int w = gfx->_w, h = gfx->_h;
  const uint8_t *g = GLCD_FONT + (uint8_t)ch * 5;
  for (int i = 0; i < 5; i++) {
    uint8_t line = g[i];
    for (int j = 0; j < 8; j++, line >>= 1) {
      if (!(line & 1)) continue;
      int px = x + i * size, py = y + j * size;
      if (px < 0 || py < 0 || px >= w || py >= h) return false;
      if (fb[py * w + px] != col) return false;
    }
  }
  return true;
}
static bool see(const char *s, int size, uint16_t col, int x0, int y0, int x1, int y1) {
  int n = (int)strlen(s);
  int adv = 6 * size;
  for (int y = y0; y + 8 * size <= y1; y++)
    for (int x = x0; x + n * adv <= x1; x++) {
      bool ok = true;
      for (int i = 0; s[i] && ok; i++)
        if (!glyphAt(x + i * adv, y, s[i], size, col)) ok = false;
      if (ok) return true;
    }
  return false;
}

static void showParty() {
  clearScreens();
  partyOpen = true;
  gfx->frameReady = false;
  render();
}
static void showPlayer() {
  clearScreens();
  playerOpen = true;
  playerPage = 0;
  gfx->frameReady = false;
  render();
}

int main() {
  setup();
  for (int i = 0; i < 4; i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  pet.dbgHatchAs(4, false);
  pet.nick[0] = 0;
  pet.ageMinutes = 60UL * 20;
  while (pet.hasLearnOffer()) pet.declineLearn();
  gLang = LANG_EN;
  emptyParty();

  // two banked, slot 5 empty: the header counts the follower, cell 6 draws it
  party.replaceAt(0, mon(1, 10, "BULBA"));
  party.replaceAt(1, mon(7, 12, "SQUIRT"));
  showParty();
  if (on("party")) {
    ck(gfx->frameReady, "the party screen flushes");
    ck(see("PARTY 3/6", 3, UI_INK, 0, 0, gfx->_w, 120),
       "the header reads 3/6 with the follower counted");
    ck(!see("PARTY 2/6", 3, UI_INK, 0, 0, gfx->_w, 120),
       "it is not the banked count alone");
    int x = CELL_X(5), y = CELL_Y(5);
    ck(see("CHARMANDER", 1, DEX_TBL[4].accent, x, y, x + CELL_W, y + CELL_H),
       "cell 6 draws the follower");
    ck(see("FOLLOWING", 1, UI_BAR_WARN, x, y, x + CELL_W, y + CELL_H),
       "cell 6 tags it FOLLOWING");
    ck(!see("- empty -", 2, 0x8410, x, y, x + CELL_W, y + CELL_H),
       "the empty-slot marker is covered");
  }
  showPlayer();
  if (on("player"))
    ck(see("PARTY 3/6", 2, UI_INK, 0, 280, gfx->_w, gfx->_h),
       "the player card counts the follower the same way");

  // slot 5 still occupied: the cell stands aside and the header does not read 7/6
  emptyParty();
  for (int i = 0; i < PARTY_SLOTS; i++) party.slots[i] = mon(25, 30, "STUCK");
  party.slots[5].nick[0] = 'Z';
  snprintf(party.slots[5].nick, sizeof(party.slots[5].nick), "STUCK");
  showParty();
  if (on("party")) {
    ck(see("PARTY 6/6", 3, UI_INK, 0, 0, gfx->_w, 120), "a stuck sixth reads 6/6");
    ck(!see("PARTY 7/6", 3, UI_INK, 0, 0, gfx->_w, 120), "and not 7/6");
    int x = CELL_X(5), y = CELL_Y(5);
    ck(see("STUCK", 1, DEX_TBL[25].accent, x, y, x + CELL_W, y + CELL_H),
       "cell 6 keeps the banked creature");
    ck(!see("FOLLOWING", 1, UI_BAR_WARN, x, y, x + CELL_W, y + CELL_H),
       "and does not paint the follower over it");
    tapCell(5);
    ck(partyDetail == 6 && boxSwapFrom == 6 && party.slots[5].dex == 25,
       "that cell still opens, so the creature can be deposited");
  }

  // cell 6 cannot be filled. The same three actions still work on cell 1.
  emptyParty();
  tapCell(5);
  ck(party.slots[5].empty() && boxSwapFrom == 0 && followSheetUp(),
     "tapping cell 6 opens the follower sheet and does not arm a swap");
  leaveFollowerSheet();
  ck(!followSheetUp(), "leaving that sheet does not arm a swap either");
  tapCell(0);
  ck(party.slots[0].empty() && boxSwapFrom == 1, "tapping cell 1 does");

  emptyParty();
  party.box[0] = mon(25, 20, "PIKA");
  party.boxSave();
  boxSel = 1;
  partyOpen = true;
  tapCell(5);
  ck(party.slots[5].empty() && party.box[0].dex == 25 && followSheetUp() && boxSel == 1,
     "a box creature dropped on cell 6 stays in the box");
  leaveFollowerSheet();
  ck(!followSheetUp() && boxSel == 1, "leaving the sheet keeps the box creature selected");
  tapCell(0);
  ck(party.slots[0].dex == 25 && party.box[0].empty() && party.slots[5].empty(),
     "the same drop on cell 1 deposits it");

  emptyParty();
  pet.endedMon = mon(143, 40, "NEWS");
  partyPick = true;
  partyOpen = true;
  tapCell(5);
  ck(party.slots[5].empty() && partyPick && followSheetUp(),
     "the replace picker cannot fill cell 6");
  leaveFollowerSheet();
  ck(!followSheetUp() && partyPick, "leaving the sheet keeps the newcomer waiting");
  tapCell(0);
  ck(party.slots[0].dex == 143 && !partyPick && party.slots[5].empty(),
     "and cell 1 still takes the newcomer");

  // Hatched follower: the sheet's left button is SWITCH. An egg keeps BRING BACK.
  emptyParty();
  party.replaceAt(0, mon(1, 10, "BULBA"));
  pet.learnQCount = 1;
  clearScreens();
  partyOpen = true;
  partyDetail = 1;
  gfx->frameReady = false;
  render();
  if (on("party")) {
    ck(gfx->frameReady, "the sheet flushes");
    ck(pix(90, 344) == UI_TRACK, "a refused SWITCH is grey");
    ck(see("SWITCH", 2, 0x8410, 70, 336, 250, 384), "and its label is SWITCH");
    ck(see("LEARN THE MOVE FIRST", 1, UI_BAR_WARN, 0, 308, gfx->_w, 336),
       "with the reason above it");
    ck(!see("BRING BACK", 2, 0x8410, 70, 336, 250, 384),
       "and not BRING BACK");
    ck(!see("TO PARTY", 2, UI_INK, 0, 300, gfx->_w, 420),
       "a banked sheet has no TO PARTY button");
  }
  int16_t live = pet.speciesId;
  partyTap(233, 360);
  ck(pet.speciesId == live && partyDetail == 1 && partyOpen,
     "that tap does not switch, and the sheet stays up");

  pet.learnQCount = 0;
  gfx->frameReady = false;
  render();
  if (on("party")) {
    ck(pix(90, 344) == UI_BAR_OK, "SWITCH lights up once the offer is cleared");
    ck(see("SWITCH", 2, UI_BG_DAY, 70, 336, 250, 384), "in the enabled colour");
  }
  partyTap(320, 360);
  ck(releaseConfirm && pet.speciesId == live && party.slots[0].dex == 1,
     "RELEASE still asks, and does not switch");
  releaseConfirm = false;
  partyTap(233, 360);
  ck(pet.speciesId == 1, "the centre tap switches to the banked creature");
  ck(party.slots[0].dex == live, "and banks the follower in that slot");
  ck(!partyOpen && partyDetail == 0, "the sheet and the party close");
  on("main");

  if (!pet.isEgg()) pet.newEgg();
  clearScreens();
  partyOpen = true;
  tapCell(5);
  ck(!followSheetUp() && partyDetail == 0 && party.slots[5].empty(),
     "an egg cell is swallowed and opens no sheet");
  gfx->frameReady = false;
  render();
  if (on("party"))
    ck(!see("TO PARTY", 2, UI_INK, 0, 0, gfx->_w, gfx->_h),
       "and the grid does not offer TO PARTY");
  partyDetail = 1;
  gfx->frameReady = false;
  render();
  if (on("party")) {
    ck(see("BRING BACK", 2, UI_BG_DAY, 70, 336, 250, 384),
       "an egg still offers BRING BACK");
    ck(!see("SWITCH", 2, UI_BG_DAY, 70, 336, 250, 384) &&
       !see("SWITCH", 2, 0x8410, 70, 336, 250, 384),
       "and not SWITCH");
  }
  partyTap(233, 360);
  ck(pet.speciesId == live && party.slots[0].empty(),
     "BRING BACK still revives from the centre");
  ck(!partyOpen && partyDetail == 0, "and closes the sheet");
  on("main");

  emptyParty();
  party.box[0] = mon(25, 20, "PIKA");
  clearScreens();
  partyOpen = true;
  boxOpen = true;
  boxDetail = 1;
  gfx->frameReady = false;
  render();
  if (on("box")) {
    ck(see("TO PARTY", 2, UI_BG_DAY, 70, 336, 250, 384),
       "a boxed creature still offers TO PARTY");
    ck(!see("SWITCH", 2, UI_BG_DAY, 70, 336, 250, 384) &&
       !see("SWITCH", 2, 0x8410, 70, 336, 250, 384),
       "and not SWITCH");
    ck(!see("TO PARTY", 2, UI_INK, 0, 300, gfx->_w, 420),
       "a boxed creature has no TO PARTY button");
  }

  // The follower's own sheet. Hidden until the farewell is earned, then the
  // button runs the same ceremony the prompt does.
  emptyParty();
  pet.dbgHatchAs(6, false);
  snprintf(pet.nick, sizeof(pet.nick), "ZARD");
  pet.moves[0] = 9;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.frozen = false;
  pet.ceremony = CER_NONE;
  pet.ageMinutes = FAREWELL_AGE_MIN - 1;
  clearScreens();
  partyOpen = true;
  tapCell(5);
  ck(followSheetUp() && party.slots[5].empty(), "cell 6 opens the follower, not a party slot");
  gfx->frameReady = false;
  render();
  if (on("party")) {
    ck(gfx->frameReady, "the follower sheet flushes");
    ck(see("ZARD", 2, DEX_TBL[6].accent, 0, 20, gfx->_w, 80),
       "the sheet is built from the follower");
    ck(!see("TO PARTY", 2, UI_INK, 0, 280, gfx->_w, 420),
       "TO PARTY is hidden one minute short of the requirements");
    ck(pix(90, 344) != UI_BAR_WARN, "and the button is not drawn");
  }
  partyTap(233, 360);
  ck(pet.ceremony == CER_NONE && !followSheetUp(),
     "that tap does not start a farewell");

  pet.ageMinutes = FAREWELL_AGE_MIN;
  partyOpen = true;
  tapCell(5);
  gfx->frameReady = false;
  render();
  if (on("party")) {
    ck(see("TO PARTY", 2, UI_INK, 70, 320, 280, 400),
       "TO PARTY is shown once the requirements are met");
    ck(pix(90, 344) == UI_BAR_WARN, "on the gold button");
  }
  uint8_t lv = pet.level();
  partyTap(233, 360);
  ck(pet.ceremony == CER_FAREWELL && !partyOpen && !followSheetUp(),
     "the tap starts the farewell and leaves the sheet");
  on("main");
  ck(party.count() == 0 && pet.speciesId == 6, "it is not banked until the ceremony ends");
  emuSetTimeScale(100000);
  uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < CEREMONY_MS + 2000) {}
  emuSetTimeScale(1);
  loop();
  ck(pet.isEgg() && pet.ceremony == CER_NONE && pet.lastEnd == CER_FAREWELL,
     "the ceremony ends into a blessed egg");
  ck(party.count() == 1 && party.slots[0].dex == 6 && party.slots[0].level == lv
     && party.slots[0].moves[0] == 9 && !strcmp(party.slots[0].nick, "ZARD")
     && party.slots[5].empty(),
     "and banks the creature the same way the prompt does");

  // An early retire is a real goodbye. An earned farewell banks them.
  clearScreens();
  partyBannerUntil = 0;
  choiceKind = 3;
  choiceUntil = millis() + 60000;
  gfx->frameReady = false;
  render();
  if (on("main")) {
    ck(see("Goodbye", 2, UI_INK, 0, 200, gfx->_w, gfx->_h),
       "an early retire still says Goodbye");
    ck(see("it will not join your party", 1, UI_BAR_BAD, 0, 180, gfx->_w, 230),
       "and still says they will not join the party");
    ck(!see("Join the party?", 2, UI_INK, 0, 0, gfx->_w, gfx->_h),
       "it does not offer the party");
  }
  pet.dbgHatchAs(6, false);
  pet.ageMinutes = FAREWELL_AGE_MIN;
  pet.sleeping = false;
  pet.ceremony = CER_NONE;
  pet.learnQCount = 0;
  clearScreens();
  choiceKind = 2;
  choiceUntil = millis() + 60000;
  gfx->frameReady = false;
  render();
  if (on("main")) {
    ck(see("Join the party?", 2, UI_INK, 0, 140, gfx->_w, 220),
       "the farewell question is about the party");
    ck(see("To the party", 2, UI_INK, 80, 220, 390, 360),
       "the confirm sends them to the party");
    ck(see("Keep following", 2, UI_WHITE, 80, 280, 390, 430),
       "and staying keeps them following");
    ck(see("Switch them back any time", 1, UI_INK, 0, 170, gfx->_w, 230),
       "the line under it says they can come back");
    ck(!see("Goodbye", 2, UI_INK, 0, 0, gfx->_w, gfx->_h),
       "Goodbye is gone from this dialog");
    ck(!see("Say goodbye?", 2, UI_INK, 0, 0, gfx->_w, gfx->_h),
       "and so is Say goodbye?");
  }
  choiceKind = 3;
  gfx->frameReady = false;
  render();
  if (on("main")) {
    ck(see("Join the party?", 2, UI_INK, 0, 140, gfx->_w, 220),
       "a free retire uses the same party question");
    ck(!see("it will not join your party", 1, UI_BAR_BAD, 0, 0, gfx->_w, gfx->_h),
       "and does not claim they are gone");
  }
  choiceKind = 0;
  gfx->frameReady = false;
  render();
  if (on("main"))
    ck(see("can join the party", 2, UI_INK, 0, 160, gfx->_w, 280),
       "the gold button says they can join the party");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
