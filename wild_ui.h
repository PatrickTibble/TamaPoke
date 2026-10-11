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

bool btlWild = false;
bool wildPickOpen = false;
static uint8_t wildRegion = 0;

// Whether the pet can go at all. A locked region is not its business: the
// chooser greys those per row.
static WildRefusal wildPetRefusal() {
  WildRefusal w = wildCanStart(pet, REGION_ALL);
  return w == WILD_LOCKED ? WILD_OK : w;
}

static uint8_t wildSquadTop(uint16_t mask) {
  uint8_t top = (!pet.isEgg() && (mask & 1)) ? pet.level() : 0;
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (!party.slots[i].empty() && (mask & (1 << (i + 1))) && party.slots[i].level > top)
      top = party.slots[i].level;
  return top;
}

uint8_t wildRpickMode() { return wildPickOpen ? RPICK_FOR_WILD : RPICK_FOR_GYMS; }

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
  rpickPage = 0;
  sfxPlay(SFX_TAP);
  return true;
}

void wildPickTap(int16_t x, int16_t y) {
  int r = regionPickTap(x, y, RPICK_FOR_WILD);
  if (r >= 0) {
    wildRegion = (uint8_t)r;
    wildPickOpen = false;
    gymOpen = false;
    gymPick = false;
    pickTrainer = PICK_WILD;
    pickHard = false;
    pickPage = 0;
    pickDefault(squadCap(PICK_WILD, false));
    pickOpen = true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (y > 380) { wildPickOpen = false; rpickPage = 0; }   // BACK: the gym chooser
}

void wildPickHead(char *out, size_t n) {
  snprintf(out, n, "%s: %s", WT(W_BTN), REGIONS[wildRegion % REGION_COUNT].name);
}

void wildPickBack() { gymOpen = true; gymPick = true; wildPickOpen = true; }

// Refused at FIGHT (it fell asleep or tired while picking): back to the gym
// chooser, where the greyed WILD button says why.
void wildFight() {
  if (startWildBattle(wildRegion)) return;
  sfxPlay(SFX_DENY);
  gymOpen = true;
  gymPick = true;
  wildPickOpen = false;
}

bool startWildBattle(uint8_t region) {
  if (battleOpen || wildCanStart(pet, region) != WILD_OK) return false;
  uint8_t top = wildSquadTop(squadMask);
  if (!top) return false;
  uint8_t lvl = wildLevelFor(top);
  int16_t dex = wildRoll(region, lvl);
  if (dex < 1) return false;
  btlWild = true;
  btlTrainer = -1;
  btlHard = false;
  btlLink = false;
  startBattle(dex, lvl);
  if (!battleOpen) { btlWild = false; return false; }
  wildSpend(pet);
  wildMarkSeen(dex);
  btlSay(WT(W_APPEARED), DEX_TBL[dex].name);
  return true;
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

// Win, lose or run, a wild fight returns to the wild chooser for another go.
void wildAfterBattle() {
  btlWild = false;
  gymOpen = true;
  gymPick = true;
  wildPickOpen = true;
}

// WILD [region] -- defaults to the egg region; uses the current squad.
bool wildSerial(const String &line) {
  if (!line.startsWith("WILD")) return false;
  int rg = pet.region;
  sscanf(line.c_str() + 4, "%d", &rg);
  if (battleOpen) {
    Serial.println("already in a battle");
  } else if (rg < 0 || rg >= REGION_COUNT) {
    Serial.printf("usage: WILD [region 0..%u]\n", REGION_COUNT - 1);
  } else if (startWildBattle((uint8_t)rg)) {
    Serial.printf("wild %s Lv.%u in %s, ene=%u\n", btlFoe.name, btlFoe.level,
                  REGIONS[rg].name, pet.energy);
  } else {
    const char *why = wildRefusalText(wildCanStart(pet, (uint8_t)rg));
    Serial.printf("refused: %s\n", *why ? why : "no squad");
  }
  Serial.println("DONE");
  return true;
}
