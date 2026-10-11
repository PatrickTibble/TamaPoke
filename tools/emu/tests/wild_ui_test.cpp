// The wild area screen, driven through the sketch's own dispatchers (onTap,
// onSwipe, battleTap) so the FORK(wild) hooks are tested and not just the
// handlers behind them. A fresh harness: every section sets up the screen it
// needs and asserts it is on it before testing anything.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "wild.h"
#include "battle.h"
#include "i18n.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <chrono>
#include <thread>
uint32_t g_seed=77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
void onTap(int16_t,int16_t); void onSwipe(int);
uint8_t uiCurrentScreen(); uint8_t rpickModeNow();
uint8_t rpickRegions(uint8_t); uint8_t rpickPageCount(uint8_t);
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool gymOpen, gymPick, pickOpen, battleOpen, btlOver;
extern bool cardOpen, galleryOpen, clockOpen, kbOpen, menuOpen, partyOpen, playerOpen, lanOpen;
extern uint8_t rpickPage, btlMsgCount, dimStage, btlTrainGain;
extern bool btlWon, btlPetIn;
extern char btlMsg[6][40];
extern Combatant btlFoe;
extern uint32_t confirmUntil;
extern bool holdFired;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static const char *screen(){ return SCREEN_NAME[uiCurrentScreen()]; }
static bool on(const char *want){
  if (!strcmp(screen(),want)) return true;
  printf("FAIL  expected the '%s' screen, on '%s'\n",want,screen()); bad++; return false;
}

// Sketch geometry with no accessor, copied from the defines it names. The area
// rows come from wildAreaRowRect(), the firmware's own answer.
static const int WILD_BTN_X=77+75, WILD_BTN_Y=336+22;     // WILDBTN_X/Y + half of LANBTN_W/H
static const int RPICK_ROW_Y0=108+31, RPICK_PITCH=72;      // RPICK_Y(i) + RPICK_H/2
static const int BACK_Y=400;                               // every chooser: BACK below y 380
static const int PICK_BACK_CX=71+77, PICK_GO_CX=240+77, PICK_BTN_CY=350+22;
static const int UI_TAP_MIN_T=44;
#define RPICK_FOR_GYMS_T 0

static void reset(){
  cardOpen=galleryOpen=clockOpen=kbOpen=menuOpen=partyOpen=playerOpen=lanOpen=false;
  battleOpen=pickOpen=false; btlWild=false;
  gymOpen=gymPick=true; wildPickOpen=wildAreasShown=false; rpickPage=0;
}
static void allBadges(uint16_t m){
  pet.badges=pet.badgesHard=0;
  for (auto &b : pet.badgesX) b=0;
  for (auto &b : pet.badgesHardX) b=0;
  pet.badges=m; for (auto &b : pet.badgesX) b=m;
}
static void fit(uint8_t e){ pet.energy=e; pet.sleeping=false; }
static void rowTap(uint8_t i){ int16_t x,y,w,h; wildAreaRowRect(i,x,y,w,h); onTap(x+w/2,y+h/2); }
static bool openRegion(uint8_t r){
  reset();
  onTap(WILD_BTN_X,WILD_BTN_Y);
  if (rpickModeNow()!=RPICK_FOR_WILD){ printf("FAIL  WILD did not open the wild region chooser\n"); bad++; return false; }
  uint8_t per=(uint8_t)((rpickRegions(RPICK_FOR_WILD)+rpickPageCount(RPICK_FOR_WILD)-1)/rpickPageCount(RPICK_FOR_WILD));
  rpickPage=(uint8_t)(r/per);
  onTap(233,RPICK_ROW_Y0+(r%per)*RPICK_PITCH);
  if (!wildAreasShown){ printf("FAIL  region %u did not open its area list\n",r); bad++; return false; }
  return true;
}
static void endBattle(){ btlOver=true; btlMsgCount=1; onTap(233,233); }
static uint16_t findArea(const char *name){   // pages until the row is on screen
  for (int i=0;i<64;i++) onSwipe(+1);
  for (int guard=0; guard<64; guard++){
    for (uint8_t i=0;i<6;i++){ uint16_t a=wildAreaAtRow(i); if (a!=WILD_NO_AREA && !strcmp(wildAreaName(a),name)) return i; }
    uint8_t was=wildAreaPage; onSwipe(-1); if (wildAreaPage==was) break;
  }
  printf("FAIL  %s is on no page\n",name); bad++; return WILD_NO_AREA;
}
static void shot(const char *name){
  const char *dir=getenv("WILD_SHOT_DIR"); if (!dir) return;
  render();
  char path[512]; snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
  FILE *f=fopen(path,"wb"); if (!f) return;
  const uint16_t *fb=gfx->buffer();
  fprintf(f,"P6\n466 466\n255\n");
  for (int i=0;i<466*466;i++){ uint16_t c=fb[i];
    fputc(((c>>11)&31)*255/31,f); fputc(((c>>5)&63)*255/63,f); fputc((c&31)*255/31,f); }
  fclose(f); printf("      wrote %s\n",path);
}

// handleTouch() self-gates to 50 Hz off millis(), so real time has to pass.
static void pump(int n){ for(int i=0;i<n;i++){ std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); } }
static void tap(int x,int y){ g_touchX=x; g_touchY=y; g_touchDown=true; emuFireInterrupt(); pump(5); g_touchDown=false; emuFireInterrupt(); pump(3); }
static void hold(int x,int y,int ms){ g_touchX=x; g_touchY=y; g_touchDown=true; emuFireInterrupt(); pump(ms/30); g_touchDown=false; emuFireInterrupt(); pump(3); }

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes=60UL*40;
  while (pet.hasLearnOffer()) pet.declineLearn();
  gRegionArt=0xFFFF; pet.lastSeenEpoch=0; fit(80);
  const uint8_t KANTO=0, JOHTO=1;

  // ---- the screen, and which chooser the firmware says is up
  {
    allBadges(0);
    if (openRegion(KANTO) && on("gympick")){
      ck(rpickModeNow()==0xFF, "rpickModeNow() is 0xFF on the area list: no region chooser is up");
      gfx->frameReady=false; render();
      ck(gfx->frameReady, "the area list flushes");
      shot("areas_kanto_t1");
      onTap(233,BACK_Y);
      ck(on("gympick") && !wildAreasShown && rpickModeNow()==RPICK_FOR_WILD,
         "BACK lands on the wild region chooser, which rpickModeNow() reports as such");
      onTap(233,BACK_Y);
      ck(rpickModeNow()==RPICK_FOR_GYMS_T, "BACK again lands on the gym chooser");
      onTap(WILD_BTN_X,WILD_BTN_Y);
      ck(rpickModeNow()==RPICK_FOR_WILD && !wildAreasShown, "and WILD reopens the region chooser, not a stale area list");
      ck(strcmp(screen(),"main")!=0, "(the area list is not the main screen)");
    }
  }

  // ---- it opens on the first page of the highest unlocked tier
  {
    bool ok=true;
    for (uint16_t m : {0x0, 0x3, 0x1FFF}){
      allBadges(m);
      for (uint8_t r=0;r<REGION_ALL;r++){
        if (!openRegion(r)) { ok=false; continue; }
        uint8_t t=wildTier(pet,r), want=0; uint16_t first=WILD_NO_AREA;
        for (uint16_t a=wildAreaFirst(r); a<wildAreaFirst(r)+wildAreaCount(r); a++)
          if (wildAreaTier(a)<=t && wildAreaTier(a)>want){ want=wildAreaTier(a); first=a; }
        if (wildAreaAtRow(0)!=first){ ok=false; printf("      region %u badges %x: opens on %s, want %s\n",r,m,wildAreaName(wildAreaAtRow(0)),wildAreaName(first)); }
      }
    }
    ck(ok, "every region opens on the first area of its highest unlocked tier, at 0, 2 and all badges");
  }

  // ---- paging through the real onSwipe() reaches every area, and never closes
  {
    allBadges(0x1FFF); fit(80);
    bool stayed=true, clamped=true, all=true, back=true; size_t total=0;
    for (uint8_t r=0;r<REGION_ALL;r++){
      if (!openRegion(r)) { all=false; continue; }
      for (int i=0;i<40;i++) onSwipe(+1);
      if (wildAreaPage!=0 || !wildAreasShown) clamped=false;
      std::set<uint16_t> seen;
      for (int guard=0; guard<64; guard++){
        uint8_t page=wildAreaPage;
        for (uint8_t i=0;i<6;i++){
          rowTap(i);
          if (!pickOpen) continue;
          seen.insert(wildArea);
          onTap(PICK_BACK_CX,PICK_BTN_CY);
          if (!wildAreasShown || wildAreaPage!=page) back=false;
        }
        onSwipe(-1);
        if (!wildAreasShown || strcmp(screen(),"gympick")) { stayed=false; break; }
        if (wildAreaPage==page) break;
      }
      onSwipe(-1);
      if (!wildAreasShown) clamped=false;
      for (uint16_t a : seen) if (wildAreaRegion(a)!=r) all=false;
      if (seen.size()!=wildAreaCount(r)){ all=false; printf("      region %u: %zu of %u areas reachable\n",r,seen.size(),wildAreaCount(r)); }
      total+=seen.size();
    }
    printf("      (%zu areas tapped)\n", total);
    ck(all, "every area of every tier of every region is reachable by swiping and tapping");
    ck(stayed, "a horizontal swipe pages the list and never closes it");
    ck(clamped, "paging clamps at both ends");
    ck(back, "the team picker's BACK returns to the page it came from");
  }

  // ---- a closed row denies, an open one opens the team picker
  {
    allBadges(0); fit(80);
    if (openRegion(KANTO) && on("gympick")){
      rowTap(0);
      ck(pickOpen && wildAreaTier(wildArea)==1, "a tier-1 row opens the team picker with 0 badges");
      onTap(PICK_BACK_CX,PICK_BTN_CY);
      onSwipe(-1);
      uint16_t a=wildAreaAtRow(0);
      ck(a!=WILD_NO_AREA && wildAreaWhyClosed(pet,a)==WILD_AREA_LOCKED, "the next page is a locked tier");
      shot("areas_kanto_locked");
      rowTap(0);
      ck(!pickOpen && wildAreasShown, "and its row denies the tap");
    }
    const uint8_t HOENN=2;
    if (openRegion(HOENN)){
      uint8_t n=0; for (uint8_t i=0;i<6;i++) if (wildAreaAtRow(i)!=WILD_NO_AREA) n++;
      ck(n==6 && wildAreaTier(wildAreaAtRow(5))==1, "Hoenn tier 1 fills its first page");
      shot("areas_hoenn_full");
      onSwipe(-1);
      ck(wildAreaTier(wildAreaAtRow(0))==1 && wildAreaAtRow(1)==WILD_NO_AREA, "and overflows its 7th area onto a second");
    }
    allBadges(0x1FFF);
    gRegionArt=(uint16_t)(1u<<JOHTO);
    if (openRegion(JOHTO)){
      uint16_t row=findArea("ROUTE 34");
      if (row!=WILD_NO_AREA){
        ck(wildAreaWhyClosed(pet,wildAreaAtRow(row))==WILD_LOCKED, "Johto ROUTE 34 reads Needs pack without Kanto's");
        shot("areas_johto_pack");
        rowTap(row);
        ck(!pickOpen && wildAreasShown, "and denies the tap");
        gRegionArt|=(uint16_t)(1u<<KANTO);
        rowTap(row);
        ck(pickOpen, "(installed, the same row opens the team picker)");
      }
    }
    gRegionArt=0xFFFF;
  }

  // ---- a fight returns to the page it came from, unless the pet can no longer go
  {
    allBadges(0x1FFF); fit(80);
    if (openRegion(KANTO)){
      onSwipe(-1); onSwipe(-1);
      uint8_t page=wildAreaPage;
      rowTap(0); onTap(PICK_GO_CX,PICK_BTN_CY);
      ck(battleOpen && btlWild && on("battle"), "FIGHT starts a wild battle in the chosen area");
      endBattle();
      ck(!battleOpen && wildAreasShown && wildAreaPage==page && on("gympick"), "and it ends back on the same page");
      fit(WILD_ENERGY_MIN+WILD_ENERGY_COST-1);
      rowTap(0); onTap(PICK_GO_CX,PICK_BTN_CY);
      ck(battleOpen && pet.energy<WILD_ENERGY_MIN, "a fight that leaves the pet too tired");
      endBattle();
      ck(!wildAreasShown && !wildPickOpen && rpickModeNow()==RPICK_FOR_GYMS_T, "lands on the gym chooser, not the list");
    }
  }

  // ---- a pet refusal on the list exits through wildExitToGyms()
  {
    allBadges(0x1FFF); fit(80);
    if (openRegion(KANTO)){
      fit(WILD_ENERGY_MIN-1);
      rowTap(0);
      ck(!pickOpen && !wildAreasShown && rpickModeNow()==RPICK_FOR_GYMS_T, "tapping a row too tired goes to the gym chooser");
      fit(80);
      onTap(WILD_BTN_X,WILD_BTN_Y);
      ck(rpickModeNow()==RPICK_FOR_WILD && !wildAreasShown, "and WILD then opens the region chooser, not the stale list");
    }
  }

  // ---- rows and their right column
  {
    int16_t x,y,w,h; bool tall=true;
    for (uint8_t i=0;i<6;i++){ wildAreaRowRect(i,x,y,w,h); if (h<UI_TAP_MIN_T) tall=false; }
    ck(tall, "every area row is at least UI_TAP_MIN tall");
    uint8_t maxHi=0;
    for (uint16_t a=0; a<wildAreaFirst(REGION_ALL-1)+wildAreaCount(REGION_ALL-1); a++)
      for (uint16_t i=0;i<wildSlotCount(a);i++){ WildSlotView s; wildSlotAt(a,i,s); if (s.hi>maxHi) maxHi=s.hi; }
    Lang was=gLang; bool fits=true;
    for (uint8_t l=0;l<LANG_COUNT;l++){
      gLang=(Lang)l; char b[40];
      for (WildRefusal r : {WILD_AREA_LOCKED, WILD_AREA_NOT_NOW, WILD_LOCKED}){
        wildRowReason(r,TRAINER_GYMS+1,b,sizeof(b));
        if (!*b || strlen(b)>WILD_ROW_REASON_MAX){ fits=false; printf("      lang %u: '%s'\n",l,b); }
      }
      snprintf(b,sizeof(b),WT(W_LV_FMT),maxHi,maxHi);
      if (strlen(b)>WILD_ROW_LV_MAX){ fits=false; printf("      lang %u: '%s'\n",l,b); }
      if (!*WT(W_NO_AREA)) fits=false;
    }
    gLang=was;
    ck(fits, "every language's row reasons and level range fit their column");
  }

  // ---- the console picks a random open area
  {
    allBadges(0); fit(80); reset();
    wildSerial(String("WILD 0"));
    ck(battleOpen && btlWild && wildAreaRegion(wildArea)==KANTO && wildAreaTier(wildArea)==1,
       "WILD 0 with no badges fights in an open tier-1 Kanto area");
    endBattle();
    bool shown=false; for (uint8_t i=0;i<6;i++) if (wildAreaAtRow(i)==wildArea) shown=true;
    ck(wildAreasShown && shown, "and returns to the page holding that area");
    fit(WILD_ENERGY_MIN-1); reset();
    wildSerial(String("WILD 0"));
    ck(!battleOpen, "a tired pet is refused at the console");
    fit(80);
  }

  // ---- WILD AREAS lists without fighting; WILD <r> <index> fights in exactly that area
  {
    // A misparsed AREAS falls back to the egg region, so that must be one that can fight.
    uint8_t was=pet.region; pet.region=KANTO;
    allBadges(0); fit(80); reset();
    wildSerial(String("WILD"));
    ck(battleOpen, "(bare WILD fights in the egg region, so a misparse would too)");
    endBattle(); reset();
    wildSerial(String("WILD AREAS 1"));
    ck(!battleOpen, "WILD AREAS 1 lists and does not start a battle");
    wildSerial(String("WILD AREAS"));
    ck(!battleOpen, "(nor does WILD AREAS with no region)");
    pet.region=was;
    int open=-1, shut=-1;
    for (uint16_t i=0;i<wildAreaCount(JOHTO);i++){
      bool o=wildAreaOpen(pet,wildAreaFirst(JOHTO)+i);
      if (o && open<0) open=i;
      if (!o && shut<0) shut=i;
    }
    char cmd[32];
    if (open>=0 && shut>=0){
      snprintf(cmd,sizeof(cmd),"WILD 1 %d",shut); wildSerial(String(cmd));
      ck(!battleOpen, "WILD 1 <a closed area> is refused");
      snprintf(cmd,sizeof(cmd),"WILD 1 %d",open); wildSerial(String(cmd));
      ck(battleOpen && btlWild && wildArea==wildAreaFirst(JOHTO)+open, "WILD 1 <an open area> fights in that very area");
      endBattle();
    } else ck(false, "(Johto has an open and a shut area with no badges)");
    reset();
    snprintf(cmd,sizeof(cmd),"WILD 1 %u",(unsigned)wildAreaCount(JOHTO)); wildSerial(String(cmd));
    ck(!battleOpen, "an index past the region's last area is refused");
    wildSerial(String("WILD 1 -1"));
    ck(!battleOpen, "and so is a negative one");
  }

  // ---- a win against a foe far below the pet says why it trained nothing
  {
    allBadges(0); fit(80); reset();
    pet.trAtk=pet.trDef=pet.trSpe=0;
    wildSerial(String("WILD 0 0"));
    if (battleOpen){
      auto says=[](const char *s){ for (uint8_t i=0;i<btlMsgCount;i++) if (!strcmp(btlMsg[i],s)) return true; return false; };
      btlWon=btlPetIn=true; btlMsgCount=0;
      ck(wildTooEasy(pet,btlFoe.level), "(a tier-1 Kanto foe is too easy for the level 41 pet)");
      wildBattleOver();
      ck(btlTrainGain==0 && says(WT(W_TOO_EASY)), "a too-easy win trains nothing and the log says so");
      btlFoe.level=pet.level(); btlMsgCount=0;
      wildBattleOver();
      ck(btlTrainGain>0 && !says(WT(W_TOO_EASY)), "(a level-matched win trains, with no such line)");
      endBattle();
    } else ck(false, "WILD 0 0 started a battle");
  }

  // ---- the 3 s hold is for the main screen only
  {
    allBadges(0x1FFF); fit(80);
    if (openRegion(KANTO)){
      tap(233,60);   // header: wakes the panel, hits nothing
      ck(dimStage==0 && on("gympick") && wildAreasShown, "the panel is awake on the area list");
      confirmUntil=0; holdFired=false;
      hold(233,200,3400);
      ck(confirmUntil==0, "holding an area row does NOT open the release dialog");
    }
    reset(); gymOpen=gymPick=false; pickOpen=false;
    tap(440,440);
    if (on("main")){
      confirmUntil=0; holdFired=false;
      hold(233,200,3400);
      ck(confirmUntil!=0, "(the hold still works on the main screen)");
      confirmUntil=0;
    }
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
