// Wild encounters. Every check asserts a RULE from wild.h against the real
// functions, and every "never" check has a companion proving the mechanism it
// guards actually engaged -- a negative that holds because nothing happened is
// not a test.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "wild.h"
#include "i18n.h"
#include "noart.h"
#include <cstdio>
#include <vector>
uint32_t g_seed=41; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static const uint8_t KANTO=0, JOHTO=1, UNOVA=4;

static void live(Pet &p, int16_t dex){
  p.begin(); p.dbgHatchAs(dex,false);
  p.energy=80; p.sleeping=false;
}

int main(){
  gRegionArt = 0xFFFF;

  // --- a roll stays inside its region, as a base AND once evolved
  {
    int out=0, none=0;
    for (uint8_t r=0; r<REGION_COUNT; r++)
      for (int i=0;i<2000;i++){
        int16_t d = wildRoll(r, (i&1) ? 100 : 1);
        if (d<0){ none++; continue; }
        if (d<REGIONS[r].lo || d>REGIONS[r].hi) out++;
      }
    ck(none==0, "every region with its pack rolls a species");
    ck(out==0, "and it is always inside that region, at level 1 and 100");
  }

  // --- never legendary, never art-less
  {
    int leg=0, artless=0, unovaRolls=0;
    for (uint8_t r=0; r<REGION_COUNT; r++)
      for (int i=0;i<2000;i++){
        int16_t d = wildRoll(r, (i&1) ? 100 : 30);
        if (d<1) continue;
        if (DEX_TBL[d].rarity==R_LEGENDARIO) leg++;
        if (!speciesHasArt(d)) artless++;
        if (r==UNOVA) unovaRolls++;
      }
    ck(leg==0, "a wild roll is never legendary");
    ck(artless==0, "and never a species with no art");
    int unovaGaps=0;
    for (int16_t d=REGIONS[UNOVA].lo; d<=REGIONS[UNOVA].hi; d++) if (!speciesHasArt(d)) unovaGaps++;
    ck(unovaRolls>0 && unovaGaps>0, "(Unova was rolled and does have art gaps, so that bites)");
  }

  // --- a locked region gives nothing, and the mixed pool skips it
  {
    Pet p; live(p,4);
    gRegionArt = 0xFFFF & ~(uint16_t)(1u<<JOHTO);
    ck(wildRoll(JOHTO,30)==-1, "a region without its pack rolls nothing");
    ck(wildCanStart(p,JOHTO)==WILD_LOCKED, "and refuses to start");
    int johto=0;
    for (int i=0;i<4000;i++){ int16_t d=wildRoll(REGION_ALL,1); if (d>=REGIONS[JOHTO].lo && d<=REGIONS[JOHTO].hi) johto++; }
    ck(johto==0, "ALL never hands out a locked region's species");
    gRegionArt = 0xFFFF;
    int16_t d = wildRoll(JOHTO,30);
    ck(d>=REGIONS[JOHTO].lo && d<=REGIONS[JOHTO].hi, "(unlocked, the same region does roll)");
    ck(wildCanStart(p,JOHTO)==WILD_OK, "(and starts)");
    johto=0;
    for (int i=0;i<4000;i++){ int16_t e=wildRoll(REGION_ALL,1); if (e>=REGIONS[JOHTO].lo && e<=REGIONS[JOHTO].hi) johto++; }
    ck(johto>0, "(and ALL does reach it when it is installed)");
    ck(wildRoll(REGION_COUNT,30)==-1 && wildCanStart(p,REGION_COUNT)==WILD_LOCKED,
       "an out-of-range region cannot index the table");
  }

  // --- rare share is roughly what WILD_COMMON_PCT says
  {
    int comun=0, raro=0;
    for (int i=0;i<8000;i++){
      int16_t d = wildRoll(KANTO,1);
      if (d<1) continue;
      if (DEX_TBL[d].rarity==R_COMUN) comun++;
      else if (DEX_TBL[d].rarity==R_RARO) raro++;
    }
    int pct = (comun+raro) ? 100*raro/(comun+raro) : -1;
    printf("      (Kanto: %d common, %d rare = %d%%)\n", comun, raro, pct);
    int want = 100-WILD_COMMON_PCT;
    ck(pct>=want-5 && pct<=want+5, "rare encounters land near 100 - WILD_COMMON_PCT");
  }

  // --- evolution follows level, stops at Eevee and at the region edge
  {
    const DexEntry &c = DEX_TBL[4], &m = DEX_TBL[c.evolvesTo];
    ck(wildEvolveFor(4,1,KANTO)==4, "a level 1 base stays a base");
    ck(wildEvolveFor(4,c.evolveLevel,KANTO)==c.evolvesTo, "it evolves exactly at the gate");
    ck(wildEvolveFor(4,c.evolveLevel-1,KANTO)==4, "and not one level before");
    ck(wildEvolveFor(4,100,KANTO)==m.evolvesTo, "and walks the whole chain at 100");
    ck(wildEvolveFor(DEX_EEVEE,100,REGION_ALL)==DEX_EEVEE, "EEVEE is never walked past");
    const int16_t ONIX=95; const DexEntry &o = DEX_TBL[ONIX];
    ck(regionOfDex(o.evolvesTo)!=KANTO, "(ONIX's evolution lives outside Kanto)");
    ck(wildEvolveFor(ONIX,100,KANTO)==ONIX, "a Kanto encounter stops at the region edge");
    ck(wildEvolveFor(ONIX,100,REGION_ALL)==o.evolvesTo, "while ALL crosses it");
    gRegionArt = 0xFFFF & ~(uint16_t)(1u<<regionOfDex(o.evolvesTo));
    ck(wildEvolveFor(ONIX,100,REGION_ALL)==ONIX, "but not into a region without its pack");
    gRegionArt = 0xFFFF;
  }

  // --- level: within the spread, clamped at both ends
  {
    const uint8_t tops[] = {0,1,WILD_MIN_LEVEL,3,10,50,99,MAX_LEVEL,255};
    bool inRange=true;
    for (uint8_t t : tops){
      int lo = (int)t-WILD_LEVEL_SPREAD; if (lo<WILD_MIN_LEVEL) lo=WILD_MIN_LEVEL; if (lo>MAX_LEVEL) lo=MAX_LEVEL;
      int hi = t<WILD_MIN_LEVEL ? WILD_MIN_LEVEL : t; if (hi>MAX_LEVEL) hi=MAX_LEVEL;
      for (int i=0;i<300;i++){ int l=wildLevelFor(t); if (l<lo || l>hi){ inRange=false; printf("      top %u gave %d\n",t,l); break; } }
    }
    ck(inRange, "a wild level is never above the squad's best nor below the floor");
    bool sawLo=false, sawHi=false;
    for (int i=0;i<500;i++){ int l=wildLevelFor(50); if (l==50-WILD_LEVEL_SPREAD) sawLo=true; if (l==50) sawHi=true; }
    ck(sawLo && sawHi, "(and the whole spread is actually used)");
  }

  // --- refusals
  {
    Pet p; live(p,4);
    ck(wildCanStart(p,KANTO)==WILD_OK, "an awake, rested creature may go");
    p.energy=WILD_ENERGY_MIN;
    ck(wildCanStart(p,KANTO)==WILD_OK, "including at exactly the energy floor");
    p.energy=WILD_ENERGY_MIN-1;
    ck(wildCanStart(p,KANTO)==WILD_TIRED, "one below it is too tired");
    p.energy=80; p.sleeping=true;
    ck(wildCanStart(p,KANTO)==WILD_ASLEEP, "a sleeping creature cannot");
    p.sleeping=false; p.speciesId=-1;
    ck(wildCanStart(p,KANTO)==WILD_EGG, "nor can an egg");
    for (uint8_t r=WILD_EGG; r<=WILD_LOCKED; r++) if (!*wildRefusalText((WildRefusal)r)) { ck(false,"every refusal has a message"); break; }
    ck(!*wildRefusalText(WILD_OK), "and WILD_OK says nothing");
  }

  // --- energy cost
  {
    Pet p; live(p,4);
    p.energy=80; wildSpend(p);
    ck(p.energy==80-WILD_ENERGY_COST, "a battle costs exactly WILD_ENERGY_COST");
    p.energy=WILD_ENERGY_COST-1; wildSpend(p);
    ck(p.energy==0, "and clamps at zero rather than wrapping");
  }

  // --- reward: only a win the pet fought in, only where there is headroom
  {
    Pet p; live(p,4);
    p.ivAtk=p.ivDef=p.ivSpe=31;
    uint8_t w;
    p.trAtk=p.trDef=p.trSpe=0;
    ck(wildReward(p,false,true,w)==0 && !p.trAtk && !p.trDef && !p.trSpe, "a loss trains nothing");
    ck(wildReward(p,true,false,w)==0 && !p.trAtk && !p.trDef && !p.trSpe, "nor a win it sat out");
    bool bounded=true, sawMin=false, sawMax=false, matches=true;
    for (int i=0;i<200;i++){
      p.trAtk=p.trDef=p.trSpe=0;
      uint8_t g = wildReward(p,true,true,w);
      if (g<WILD_REWARD_MIN || g>WILD_REWARD_MIN+WILD_REWARD_SPAN-1) bounded=false;
      if (g==WILD_REWARD_MIN) sawMin=true;
      if (g==WILD_REWARD_MIN+WILD_REWARD_SPAN-1) sawMax=true;
      uint8_t got[3]={p.trAtk,p.trDef,p.trSpe};
      if (w>2 || got[w]!=g || got[0]+got[1]+got[2]!=g) matches=false;
    }
    ck(bounded, "a win gains MIN .. MIN+SPAN-1");
    ck(sawMin && sawMax, "(and both ends of that range occur)");
    ck(matches, "and lands on exactly the stat it reports");
    bool onlySpe=true;
    for (int i=0;i<100;i++){
      p.trAtk=p.trMaxAtk(); p.trDef=p.trMaxDef(); p.trSpe=0;
      if (wildReward(p,true,true,w)==0 || w!=2) onlySpe=false;
    }
    ck(onlySpe, "with ATK and DEF capped it always goes to SPE");
    p.trSpe=p.trMaxSpe();
    ck(wildReward(p,true,true,w)==0, "and a fully trained creature gains nothing");
  }

  // --- the seen bitmap
  {
    nvs().clear();
    wildLoad();
    ck(nvs().count(WILD_SEEN_KEY)==0, "loading on a fresh save writes nothing");
    Pet p; live(p,4);
    wildLoad();
    int16_t met=10;
    ck(!p.isRegistered(met) && !wildSeen(p,met), "(a species neither raised nor met is unseen)");
    ck(wildSeen(p,4), "a raised species counts as seen without being met");
    uint16_t base = wildSeenCountIn(p,KANTO);
    wildMarkSeen(met);
    ck(nvs().count(WILD_SEEN_KEY)==1, "meeting one writes the key");
    ck(wildSeenCountIn(p,KANTO)==base+1, "and the region count goes up by one");
    wildMarkSeen(met); wildMarkSeen(4);
    ck(wildSeenCountIn(p,KANTO)==base+1, "meeting it again, or meeting a raised one, adds nothing");
    wildLoad();
    ck(wildSeen(p,met), "it survives a reload");
    ck(wildSeenCountIn(p,JOHTO)==p.registeredCountIn(REGIONS[JOHTO].lo,REGIONS[JOHTO].hi),
       "and does not leak into another region");
    ck(!wildSeen(p,0) && !wildSeen(p,DEX_COUNT+1), "out-of-range dex numbers are never seen");

    Preferences q;
    uint8_t one = 0x01; q.putBytes(WILD_SEEN_KEY,&one,1);
    wildLoad();
    ck(wildSeen(p,1) && !wildSeen(p,met), "a short blob from an older build fills the front");
    std::vector<uint8_t> big((DEX_COUNT+7)/8+16, 0); big[0]=0x02;
    q.putBytes(WILD_SEEN_KEY,big.data(),big.size());
    wildLoad();
    ck(wildSeen(p,2) && !wildSeen(p,met), "a longer blob from a newer build keeps its prefix");
  }

  // --- strings: present in every language and ASCII only
  {
    Lang was = gLang; bool ok=true;
    for (uint8_t l=0; l<LANG_COUNT; l++){
      gLang=(Lang)l;
      for (uint8_t s=0; s<W_COUNT; s++){
        const char *t = WT((WildStr)s);
        if (!*t){ ok=false; printf("      lang %u string %u is empty\n",l,s); }
        for (const char *c=t; *c; c++) if (*c<32 || *c>126){ ok=false; printf("      lang %u string %u is not ASCII\n",l,s); break; }
      }
    }
    gLang = was;
    ck(ok, "every wild string exists in all six languages and is plain ASCII");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
