#pragma once
// Follower cell on the party grid, and SWITCH on a banked sheet.
// Included once, at the bottom of TamaPoke.ino.
// followDrawCell / followCellTap are declared in follow.h for the hooks above.

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

bool followCellTap(int16_t px, int16_t py) {
  if (!party.slots[PARTY_CAP].empty()) return false;
  int x, y;
  followCellXY(x, y);
  if (px < x || px > x + PARTY_CELL_W || py < y || py > y + PARTY_CELL_H) return false;
  sfxPlay(SFX_TAP);
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
