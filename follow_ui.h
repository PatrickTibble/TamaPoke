#pragma once
// Follower cell on the party grid, SWITCH on a banked sheet, and the
// follower's own sheet (FAREWELL once the creature has earned it).
// Included once, at the bottom of TamaPoke.ino.
// Drawing and taps are declared in follow.h for the hooks above.

static void followCellXY(int &x, int &y) {
  x = PARTY_GRID_X + (PARTY_CAP % 2) * (PARTY_CELL_W + 10);
  y = PARTY_GRID_Y + (PARTY_CAP / 2) * (PARTY_CELL_H + 8);
}

void followDrawCell() {
  if (!party.slots[PARTY_CAP].empty()) return;
  int x, y;
  followCellXY(x, y);
  gfx->fillRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10, UI_WHITE);
  gfx->drawRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10, UI_INK);
  const uint8_t *th = pet.isEgg() ? nullptr : thumbs.get(pet.speciesId);
  if (th) drawThumb(th, x - 6, y - 3, 1, false);
  const char *nm;
  uint16_t col = UI_INK;
  if (pet.isEgg()) nm = T(S_EGG_HDR);
  else {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    nm = pet.nick[0] ? pet.nick : d.name;
    col = d.accent;
  }
  gfx->setTextColor(col);
  gfx->setTextSize(1);
  gfx->setCursor(x + 62, y + 6);
  gfx->print(nm);
  if (!pet.isEgg()) {
    char lv[12];
    snprintf(lv, sizeof(lv), T(S_LVL_FMT), (unsigned)pet.level());
    gfx->setTextColor(UI_INK);
    gfx->setCursor(x + 62, y + 20);
    gfx->print(lv);
  }
  gfx->setTextColor(UI_BAR_WARN);
  gfx->setCursor(x + 62, y + 36);
  gfx->print(FT(F_FOLLOWING));
}

static bool followSheet = false;

bool followSheetUp() {
  if (followSheet && (!party.slots[PARTY_CAP].empty() || pet.isEgg()))
    followSheet = false;
  return followSheet;
}

bool followSheetBack() {
  if (!followSheetUp()) return false;
  followSheet = false;
  return true;
}

bool followCellTap(int16_t px, int16_t py) {
  if (!party.slots[PARTY_CAP].empty()) return false;
  int x, y;
  followCellXY(x, y);
  if (px < x || px > x + PARTY_CELL_W || py < y || py > y + PARTY_CELL_H) return false;
  sfxPlay(SFX_TAP);
  if (!pet.isEgg()) followSheet = true;   // an egg is swallowed and opens nothing
  return true;
}

void followDrawFarewell() {
  if (!pet.canFarewellNow()) return;
  const char *lbl = FT(F_FAREWELL);
  gfx->fillRoundRect(PDET_L_X, PDET_BTN_Y, PDET_L_W, PDET_BTN_H, 10, UI_BAR_WARN);
  gfx->drawRoundRect(PDET_L_X, PDET_BTN_Y, PDET_L_W, PDET_BTN_H, 10, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(PDET_L_X + PDET_L_W / 2 - (int)strlen(lbl) * 6,
                 PDET_BTN_Y + PDET_BTN_H / 2 - 8);
  gfx->print(lbl);
}

void followRenderSheet() {
  FollowSnap s = followSnapshot(pet);
  const PartyMon &m = s.mon;
  const DexEntry &d = DEX_TBL[m.dex];
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[36];
  snprintf(head, sizeof(head), "%s%s Lv.%u", m.shiny ? "*" : "",
           m.nick[0] ? m.nick : d.name, (unsigned)m.level);
  gfx->setTextColor(d.accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(head) * 6, 40);
  gfx->print(head);
  char ty[24];
  if (d.type2 == T_NONE) snprintf(ty, sizeof(ty), "%s", typeName(d.type1));
  else snprintf(ty, sizeof(ty), "%s/%s", typeName(d.type1), typeName(d.type2));
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(ty) * 3, 64);
  gfx->print(ty);
  for (int i = 0; i < MOVE_SLOTS; i++)
    drawMoveRow(78 + i * 52, m.moves[i], false, m.dex);
  char st[40];
  snprintf(st, sizeof(st), "ATK %u  DEF %u  SPD %u  HP %u",
           party.atkOf(m), party.defOf(m), party.speOf(m), party.vitOf(m));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(st) * 3, 300);
  gfx->print(st);
  followDrawFarewell();
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_BACK)) * 6, 404);
  gfx->print(T(S_BACK));
  gfx->flush();
}

bool followTapFarewell(int16_t x, int16_t y) {
  if (!followSheetUp()) return false;
  bool onBtn = pet.canFarewellNow()
    && x >= PDET_L_X && x <= PDET_L_X + PDET_L_W
    && y >= PDET_BTN_Y && y <= PDET_BTN_Y + PDET_BTN_H;
  followSheet = false;
  if (!onBtn) { sfxPlay(SFX_TAP); return true; }
  partyDetail = 0;
  releaseConfirm = false;
  partyOpen = false;
  pet.startFarewell();
  return true;
}

bool followSheetLeft(uint8_t slot, bool *ok, const char **label, const char **why) {
  if (pet.isEgg()) return false;
  FollowStr reason = F_NOT_NOW;
  bool can = followCanSwitch(slot, &reason);
  if (ok) *ok = can;
  if (label) *label = FT(F_SWITCH);
  if (why) *why = FT(reason);
  return true;
}

bool followTapSwitch(uint8_t slot) {
  if (!followSwitch(slot)) {
    sfxPlay(SFX_DENY);
    return true;
  }
  partyDetail = 0;
  releaseConfirm = false;
  boxSwapFrom = 0;
  partyOpen = false;
  sfxPlay(SFX_HATCH);
  return true;
}
