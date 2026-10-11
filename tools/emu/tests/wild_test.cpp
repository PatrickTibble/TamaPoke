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
#include <cstring>
#include <map>
#include <vector>
uint32_t g_seed=41; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static const uint8_t KANTO=0, JOHTO=1;

static const uint8_t HOENN=2, ALOLA=6;

static void live(Pet &p, int16_t dex){
  p.begin(); p.dbgHatchAs(dex,false);
  p.energy=80; p.sleeping=false;
}

// --- area helpers: everything is looked up by name, never by table index
static const uint32_t DAY0 = 1700000000UL - 1700000000UL % 86400;   // a 00:00
static void at(Pet &p, int h){ p.lastSeenEpoch = h<0 ? 0 : DAY0 + (uint32_t)h*3600UL; }
static void badges(Pet &p, uint8_t r, uint16_t easy, uint16_t hard=0){
  p.badges=p.badgesHard=0;
  for (auto &b : p.badgesX) b=0;
  for (auto &b : p.badgesHardX) b=0;
  if (r==0){ p.badges=easy; p.badgesHard=hard; }
  else if (r<GYM_REGIONS){ p.badgesX[r-1]=easy; p.badgesHardX[r-1]=hard; }
}
static uint16_t areaOf(uint8_t r, const char *n){
  for (uint16_t a=wildAreaFirst(r); a<wildAreaFirst(r)+wildAreaCount(r); a++)
    if (!strcmp(wildAreaName(a),n)) return a;
  printf("FAIL  no area %s in region %u\n",n,r); bad++; return WILD_NO_AREA;
}
static int16_t dexOf(const char *n){
  for (int16_t d=1; d<=DEX_COUNT; d++) if (!strcmp(DEX_TBL[d].name,n)) return d;
  printf("FAIL  no species %s\n",n); bad++; return -1;
}
static std::map<int16_t,int> rolls(const Pet &p, uint16_t a, int n=2000){
  std::map<int16_t,int> m; uint8_t l;
  for (int i=0;i<n;i++) m[wildRollArea(p,a,l)]++;
  return m;
}
static bool yields(const Pet &p, uint16_t a, int16_t d){ return rolls(p,a).count(d)>0; }
static bool asciiFits(const char *s, size_t max){
  if (!s || !*s || strlen(s)>max) return false;
  for (const char *c=s; *c; c++) if (*c<32 || *c>126) return false;
  return true;
}

int main(){
  gRegionArt = 0xFFFF;

  // --- refusals
  {
    Pet p; live(p,4);
    gRegionArt = 0xFFFF & ~(uint16_t)(1u<<JOHTO);
    ck(wildCanStart(p,JOHTO)==WILD_LOCKED, "a region without its pack refuses to start");
    gRegionArt = 0xFFFF;
    ck(wildCanStart(p,JOHTO)==WILD_OK, "(and starts once it is installed)");
    ck(wildCanStart(p,REGION_COUNT)==WILD_LOCKED, "an out-of-range region cannot index the table");
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
    uint8_t w, fl=p.level();
    p.trAtk=p.trDef=p.trSpe=0;
    ck(wildReward(p,false,true,fl,w)==0 && !p.trAtk && !p.trDef && !p.trSpe, "a loss trains nothing");
    ck(wildReward(p,true,false,fl,w)==0 && !p.trAtk && !p.trDef && !p.trSpe, "nor a win it sat out");
    bool bounded=true, sawMin=false, sawMax=false, matches=true;
    for (int i=0;i<200;i++){
      p.trAtk=p.trDef=p.trSpe=0;
      uint8_t g = wildReward(p,true,true,fl,w);
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
      if (wildReward(p,true,true,fl,w)==0 || w!=2) onlySpe=false;
    }
    ck(onlySpe, "with ATK and DEF capped it always goes to SPE");
    p.trSpe=p.trMaxSpe();
    ck(wildReward(p,true,true,fl,w)==0, "and a fully trained creature gains nothing");
  }

  // --- reward gap: a foe more than WILD_REWARD_GAP levels below trains nothing
  {
    Pet p; live(p,4);
    p.ivAtk=p.ivDef=p.ivSpe=31;
    p.ageMinutes=(uint32_t)(50-1)*MINUTES_PER_LEVEL;
    ck(p.level()==50, "(the pet is level 50)");
    const uint8_t edge=50-WILD_REWARD_GAP;
    uint8_t w; bool atEdge=true, below=true;
    for (int i=0;i<50;i++){
      p.trAtk=p.trDef=p.trSpe=0;
      if (wildReward(p,true,true,edge,w)==0) atEdge=false;
      p.trAtk=p.trDef=p.trSpe=0;
      if (wildReward(p,true,true,edge-1,w)!=0 || p.trAtk || p.trDef || p.trSpe) below=false;
    }
    ck(atEdge && !wildTooEasy(p,edge), "a win exactly WILD_REWARD_GAP levels below the pet still trains");
    ck(below && wildTooEasy(p,edge-1), "one level further trains nothing, and is reported too easy");
    ck(!wildTooEasy(p,50) && !wildTooEasy(p,MAX_LEVEL), "(an equal or stronger foe is never too easy)");
    p.ageMinutes=0;
    ck(p.level()==1 && !wildTooEasy(p,1) && wildReward(p,true,true,1,w)>0, "a newborn against a level 1 foe trains");
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

  // ===== encounter areas (wild_areas.h, through the accessors only) =====
  gRegionArt = 0xFFFF;

  // --- coverage: no region is named, every one with data must be playable
  {
    Pet p; live(p,4); at(p,-1); badges(p,0,0);
    bool ok=true;
    for (uint8_t r=0; r<REGION_COUNT; r++){
      if (r==REGION_ALL){ if (wildRegionHasAreas(r)) ok=false; continue; }
      int open=0;
      for (uint16_t a=wildAreaFirst(r); a<wildAreaFirst(r)+wildAreaCount(r); a++) if (wildAreaOpen(p,a)) open++;
      if (!wildRegionHasAreas(r) || !open){ ok=false; printf("      %s: %u areas, %d open with no badges\n",REGIONS[r].name,wildAreaCount(r),open); }
    }
    ck(ok, "every region has areas and some are open with no badges; ALL has none");
  }

  // --- table invariants
  {
    uint32_t sum=0; uint16_t next=0;
    bool tiled=true, own=true, tiers=true, levels=true, weights=true, legal=true, owned=true, names=true;
    for (uint8_t r=0; r<REGION_ALL; r++){
      if (wildAreaFirst(r)!=next) tiled=false;
      next += wildAreaCount(r);
      for (uint16_t a=wildAreaFirst(r); a<wildAreaFirst(r)+wildAreaCount(r); a++){
        if (wildAreaRegion(a)!=r) own=false;
        uint8_t t=wildAreaTier(a);
        if (t<1 || t>TRAINER_GYMS+1) tiers=false;
        if (!asciiFits(wildAreaName(a),WILD_NAME_MAX) || !asciiFits(wildTierTitle(r,t),WILD_NAME_MAX)){
          names=false; printf("      bad name or title: %s / %s\n",wildAreaName(a),wildTierTitle(r,t));
        }
        uint16_t n=wildSlotCount(a); uint8_t minTier=255;
        sum += n;
        for (uint16_t i=0;i<n;i++){
          WildSlotView s; wildSlotAt(a,i,s);
          if (s.tier<1 || s.tier>TRAINER_GYMS+1) tiers=false;
          if (s.lo<1 || s.lo>s.hi || s.hi>MAX_LEVEL) levels=false;
          if (!s.weight) weights=false;
          if (s.dex<1 || s.dex>DEX_COUNT || DEX_TBL[s.dex].rarity==R_LEGENDARIO || !speciesHasArt(s.dex)) legal=false;
          if (s.tier<minTier) minTier=s.tier;
        }
        if (!n || minTier>t){ owned=false; printf("      %s tier %u, earliest slot tier %u\n",wildAreaName(a),t,minTier); }
      }
    }
    WildSlotView v;
    ck(tiled && !*wildAreaName(next) && !wildSlotAt(next,0,v), "the regions' areas tile the table end to end");
    ck(own, "every area sits in its own region's span");
    ck(sum==wildSlotTotal(), "every slot is reachable from an area of its own region");
    ck(tiers, "every area and slot tier is in 1..TRAINER_GYMS+1");
    ck(levels, "every slot has 1 <= lo <= hi <= MAX_LEVEL");
    ck(weights, "no slot has weight 0");
    ck(legal, "no slot is legendary or art-less");
    ck(owned, "every area has a slot at or below its own tier");
    ck(names, "every area name and tier title fits WILD_NAME_MAX and is ASCII");
  }

  // --- tiers: consecutive runs, per difficulty, capped at the gyms
  {
    Pet p; live(p,4); at(p,-1);
    bool exact=true, rule=true;
    for (uint8_t r=0; r<REGION_ALL; r++)
      for (uint8_t b=0; b<=TRAINER_GYMS; b++){
        badges(p,r,(uint16_t)((1u<<b)-1));
        if (wildTier(p,r)!=b+1) exact=false;
        for (uint16_t a=wildAreaFirst(r); a<wildAreaFirst(r)+wildAreaCount(r); a++)
          if (wildAreaOpen(p,a) != (wildAreaTier(a)<=b+1)){ rule=false; printf("      %s %s at %u badges\n",REGIONS[r].name,wildAreaName(a),b); }
      }
    ck(exact, "each consecutive badge raises the tier by exactly one");
    ck(rule, "an area is open exactly when its tier is reached (0 badges: tier 1 only)");
    badges(p,KANTO,0x5);
    ck(wildTier(p,KANTO)==2, "a gap stops the run");
    int regions9=0; bool nine=true;
    for (uint8_t r=0; r<REGION_ALL; r++){
      int n=0;
      for (uint16_t a=wildAreaFirst(r); a<wildAreaFirst(r)+wildAreaCount(r); a++){
        if (wildAreaTier(a)!=TRAINER_GYMS+1) continue;
        n++;
        badges(p,r,0x7F); if (wildAreaOpen(p,a)) nine=false;
        badges(p,r,0xFF); if (!wildAreaOpen(p,a)) nine=false;
      }
      if (n){ regions9++; printf("      %s: %d areas past the eighth gym\n",REGIONS[r].name,n); }
    }
    ck(nine && regions9>0, "the post-gym tier opens with 8 badges and not with 7");
    badges(p,KANTO,0x1FFF);
    ck(wildTier(p,KANTO)==TRAINER_GYMS+1, "Elite 4 and Champion bits do not raise it past the gyms");
    badges(p,KANTO,0xFF);
    ck(wildTier(p,KANTO)==TRAINER_GYMS+1, "(eight gym badges alone reach that same tier)");
    badges(p,KANTO,0x1,0x2);
    ck(wildTier(p,KANTO)==2, "easy gym 1 + hard gym 2 is tier 2: the runs are separate");
    badges(p,KANTO,0x3);
    ck(wildTier(p,KANTO)==3, "(easy gyms 1 and 2 are tier 3)");
    badges(p,KANTO,0x1,0x7);
    ck(wildTier(p,KANTO)==4, "and the longer run wins, whichever difficulty it is");
  }

  // --- rolls: 2000 per area, every region, every pack, no clock
  {
    Pet p; live(p,4); at(p,-1);
    bool open=true, member=true, inRange=true, legal=true; long total=0;
    for (uint8_t r=0; r<REGION_ALL; r++){
      badges(p,r,0xFF);
      for (uint16_t a=wildAreaFirst(r); a<wildAreaFirst(r)+wildAreaCount(r); a++){
        if (!wildAreaOpen(p,a)){ open=false; continue; }
        uint8_t lo,hi; wildAreaLevels(p,a,lo,hi);
        for (int i=0;i<2000;i++){
          uint8_t l; int16_t d=wildRollArea(p,a,l); total++;
          if (d<1){ open=false; continue; }
          bool has=false, fits=false;
          for (uint16_t k=0;k<wildSlotCount(a);k++){
            WildSlotView s; wildSlotAt(a,k,s);
            if (s.dex!=d) continue;
            has=true; if (l>=s.lo && l<=s.hi) fits=true;
          }
          if (!has) member=false;
          if (!fits || l<lo || l>hi){ if (inRange) printf("      %s: %s at %u\n",wildAreaName(a),DEX_TBL[d].name,l); inRange=false; }
          if (DEX_TBL[d].rarity==R_LEGENDARIO || !speciesHasArt(d)) legal=false;
        }
      }
    }
    printf("      (%ld rolls)\n", total);
    ck(open, "with 8 badges every area is open and always rolls");
    ck(member, "every species rolled is one of that area's slots");
    ck(inRange, "every level is inside its slot's range and wildAreaLevels()");
    ck(legal, "and is never legendary or art-less");
    badges(p,JOHTO,0xFF);
    uint16_t lake=areaOf(JOHTO,"LAKE OF RAGE"); int16_t karp=dexOf("MAGIKARP");
    bool sawLo=false, sawHi=false; uint8_t lo,hi; wildAreaLevels(p,lake,lo,hi);
    for (int i=0;i<4000;i++){ uint8_t l; if (wildRollArea(p,lake,l)==karp){ if (l==lo) sawLo=true; if (l==hi) sawHi=true; } }
    ck(sawLo && sawHi, "(Magikarp reaches both ends of Lake of Rage's range)");
    badges(p,KANTO,0); uint8_t l=99;
    ck(wildRollArea(p,areaOf(KANTO,"POWER PLANT"),l)==-1 && l==0, "a closed area rolls nothing");
    wildAreaLevels(p,areaOf(KANTO,"POWER PLANT"),lo,hi);
    ck(lo==0 && hi==0, "and has no level range");
    ck(wildRollArea(p,WILD_NO_AREA,l)==-1 && !wildAreaOpen(p,WILD_NO_AREA) && !wildSlotCount(WILD_NO_AREA),
       "WILD_NO_AREA indexes nothing");
  }

  // --- evolution clamp: an evolved form never below its gate when its base shares the row.
  // The table keeps no row ids; one row gives both halves its tier, top level and an even
  // share of its weight. Tier and top alone also pair separate rows (Hoenn's Wurmple 2-6
  // and Silcoon 3-6), whose levels are the document's by design.
  {
    int pairs=0; bool clamp=true;
    for (uint8_t r=0; r<REGION_ALL; r++)
      for (uint16_t a=wildAreaFirst(r); a<wildAreaFirst(r)+wildAreaCount(r); a++)
        for (uint16_t i=0;i<wildSlotCount(a);i++)
          for (uint16_t j=0;j<wildSlotCount(a);j++){
            WildSlotView b, e; wildSlotAt(a,i,b); wildSlotAt(a,j,e);
            if (DEX_TBL[b.dex].evolvesTo!=(uint16_t)e.dex || b.tier!=e.tier || b.hi!=e.hi || b.weight!=e.weight) continue;
            pairs++;
            if (e.lo<DEX_TBL[b.dex].evolveLevel){ clamp=false; printf("      %s: %s %u-%u under %u\n",wildAreaName(a),DEX_TBL[e.dex].name,e.lo,e.hi,DEX_TBL[b.dex].evolveLevel); }
          }
    printf("      (%d same-row evolution pairs)\n", pairs);
    ck(pairs>0 && clamp, "an evolved form sharing a row with its base starts at its evolution level");
    Pet p; live(p,4); at(p,-1);
    badges(p,JOHTO,0xFF);
    int16_t gyara=dexOf("GYARADOS"); bool seen=false, low=false;
    uint16_t lake=areaOf(JOHTO,"LAKE OF RAGE");
    for (int i=0;i<4000;i++){ uint8_t l; if (wildRollArea(p,lake,l)==gyara){ seen=true; if (l<DEX_TBL[dexOf("MAGIKARP")].evolveLevel) low=true; } }
    ck(seen && !low, "Lake of Rage rolls Gyarados, never below Magikarp's gate");
    ck(yields(p,areaOf(JOHTO,"ROUTE 42"),dexOf("MAREEP")) && yields(p,areaOf(JOHTO,"ROUTE 43"),dexOf("MAREEP")),
       "the base keeps its full range: Routes 42 and 43 still roll Mareep");
    badges(p,KANTO,0xFF);
    ck(yields(p,areaOf(KANTO,"POWER PLANT"),dexOf("VOLTORB")), "and the Power Plant still rolls Voltorb");
  }

  // --- per-slot gating inside an area
  {
    Pet p; live(p,4); at(p,-1);
    uint16_t r22=areaOf(KANTO,"ROUTE 22"), r5=areaOf(KANTO,"ROUTE 5");
    int16_t nf=dexOf("NIDORAN H"), nm=dexOf("NIDORAN M"), mk=dexOf("MANKEY");
    badges(p,KANTO,0);
    auto m=rolls(p,r22,3000);
    ck(wildAreaOpen(p,r22) && !m.count(nf) && !m.count(nm), "Kanto ROUTE 22 yields no Nidoran before the first badge");
    uint8_t lo0,hi0,lo1,hi1; wildAreaLevels(p,r22,lo0,hi0);
    badges(p,KANTO,0x1);
    m=rolls(p,r22,3000);
    ck(m.count(nf) && m.count(nm), "and does after it");
    wildAreaLevels(p,r22,lo1,hi1);
    ck(hi1>hi0, "(its level range follows the slots that went live)");
    bool mankeyLive=false;
    for (uint16_t i=0;i<wildSlotCount(r5);i++){ WildSlotView s; wildSlotAt(r5,i,s); if (s.dex==mk && s.tier<=wildTier(p,KANTO)) mankeyLive=true; }
    ck(mankeyLive && wildAreaWhyClosed(p,r5)==WILD_AREA_LOCKED, "Kanto ROUTE 5 stays shut at tier 2 although its Mankey slot is tier 2");
    badges(p,KANTO,0x3);
    ck(wildAreaOpen(p,r5) && yields(p,r5,mk), "and opens at tier 3, Mankey included");
  }

  // --- time of day
  {
    Pet p; live(p,4);
    const int hours[] = {0,3,4,9,10,19,20,23};
    const uint8_t want[] = {WILD_TM_NIGHT,WILD_TM_NIGHT,WILD_TM_MORNING,WILD_TM_MORNING,WILD_TM_DAY,WILD_TM_DAY,WILD_TM_NIGHT,WILD_TM_NIGHT};
    bool win=true;
    for (int i=0;i<8;i++){ at(p,hours[i]); if (wildHour(p)!=hours[i] || wildTimeMask(p)!=want[i]){ win=false; printf("      %02d:00 -> %u\n",hours[i],wildTimeMask(p)); } }
    ck(win, "morning 04-10, day 10-20, night 20-04, on every boundary");
    at(p,-1);
    ck(wildHour(p)==-1 && wildTimeMask(p)==WILD_TM_ALL, "no clock: every time matches");

    badges(p,JOHTO,0xFF);
    uint16_t r29=areaOf(JOHTO,"ROUTE 29"), r38=areaOf(JOHTO,"ROUTE 38"), a1=areaOf(ALOLA,"ROUTE 1");
    int16_t hoot=dexOf("HOOTHOOT"), sent=dexOf("SENTRET"), meow=dexOf("MEOWTH"), farf=dexOf("FARFETCHD");
    int16_t rat=dexOf("RATTATA"), yung=dexOf("YUNGOOS");
    at(p,23); auto n29=rolls(p,r29), n38=rolls(p,r38), na1=rolls(p,a1);
    at(p,12); auto d29=rolls(p,r29), d38=rolls(p,r38), da1=rolls(p,a1);
    at(p,7);  auto ma1=rolls(p,a1);
    ck(n29.count(hoot) && !n29.count(sent), "Johto ROUTE 29 at 23:00: Hoothoot, never Sentret");
    ck(d29.count(sent) && !d29.count(hoot), "and at 12:00 the reverse");
    ck(n38.count(meow) && !n38.count(farf), "Johto ROUTE 38: Meowth at night only");
    ck(d38.count(farf) && !d38.count(meow), "and Farfetch'd by day only");
    ck(na1.count(rat) && !na1.count(yung), "Alola ROUTE 1 at night: Rattata, never Yungoos");
    ck(ma1.count(yung) && da1.count(yung) && !ma1.count(rat) && !da1.count(rat),
       "and Yungoos at 07:00 AND 12:00 (Alola's Day covers the morning)");
    at(p,-1);
    auto x29=rolls(p,r29), x38=rolls(p,r38), xa1=rolls(p,a1);
    ck(x29.count(hoot) && x29.count(sent) && x38.count(meow) && x38.count(farf) && xa1.count(rat) && xa1.count(yung),
       "with no clock every one of them appears");
  }

  // --- the reasons, in order: tier, then time, then pack. No area in the
  // documents ever reads "Not now", so this drives the rule with made-up slots.
  {
    int16_t hoot=dexOf("HOOTHOOT"), sent=dexOf("SENTRET");
    WildSlotView s[2] = { { hoot, 100, 1, 2, 5, WILD_TM_NIGHT }, { sent, 100, 2, 2, 5, WILD_TM_ALL } };
    ck(wildWhyClosedOf(s,2,1,1,WILD_TM_DAY)==WILD_AREA_NOT_NOW, "tier-live slots that are all night-only read Not now, never Badge");
    ck(wildWhyClosedOf(s,2,1,1,WILD_TM_NIGHT)==WILD_OK, "(the same area is open at night)");
    ck(wildWhyClosedOf(s,2,1,2,WILD_TM_DAY)==WILD_OK, "(and by day once the all-day slot's tier opens)");
    ck(wildWhyClosedOf(s,2,2,1,WILD_TM_NIGHT)==WILD_AREA_LOCKED, "a locked area tier outranks every other reason");
    gRegionArt = 0xFFFF & ~(uint16_t)(1u<<JOHTO);
    ck(wildWhyClosedOf(s,2,1,1,WILD_TM_DAY)==WILD_AREA_NOT_NOW, "time outranks a missing pack");
    ck(wildWhyClosedOf(s,2,1,1,WILD_TM_NIGHT)==WILD_LOCKED, "and a missing pack is the last reason");
    gRegionArt = 0xFFFF;
    Lang was=gLang; bool ok=true;
    for (uint8_t l=0;l<LANG_COUNT;l++){ gLang=(Lang)l; if (!asciiFits(wildRefusalText(WILD_AREA_NOT_NOW),16)) ok=false; }
    gLang=was;
    ck(ok, "Not now has a message of 16 characters or fewer in every language");
    ck(!*wildRefusalText(WILD_AREA_LOCKED), "(the badge reason is formatted by the screen, not here)");
  }

  // --- weights
  {
    Pet p; live(p,4); at(p,-1); badges(p,KANTO,0);
    uint16_t r1=areaOf(KANTO,"ROUTE 1");
    auto m=rolls(p,r1,8000);
    int pg=m[dexOf("PIDGEY")], rt=m[dexOf("RATTATA")];
    printf("      (Kanto ROUTE 1: %d Pidgey, %d Rattata)\n", pg, rt);
    ck(pg>0 && rt>0 && pg*10>=rt*8 && rt*10>=pg*8, "Kanto ROUTE 1 splits Pidgey and Rattata roughly evenly");
  }

  // --- which tier owns an area
  {
    Pet p; live(p,4); at(p,-1);
    uint16_t ilex=areaOf(JOHTO,"ILEX FOREST"), r45=areaOf(JOHTO,"ROUTE 45"), r114=areaOf(HOENN,"ROUTE 114");
    badges(p,JOHTO,0x1);  ck(!wildAreaOpen(p,ilex), "Johto ILEX FOREST is shut with 1 badge");
    badges(p,JOHTO,0x3);  ck(wildAreaOpen(p,ilex), "and open with 2");
    badges(p,JOHTO,0x7F); ck(!wildAreaOpen(p,r45), "Johto ROUTE 45 is shut with 7 badges");
    badges(p,JOHTO,0xFF);
    auto m=rolls(p,r45);
    ck(m.count(dexOf("SKARMORY")) && m.count(dexOf("PHANPY")) && m.count(dexOf("TEDDIURSA")),
       "and with 8 yields Skarmory, Phanpy and Teddiursa");
    badges(p,HOENN,0x3);  ck(!wildAreaOpen(p,r114), "Hoenn ROUTE 114 is shut with 2 badges");
    badges(p,HOENN,0x7);  ck(wildAreaOpen(p,r114), "and open with 3");
  }

  // --- duplicate rows merge
  {
    uint16_t r38=areaOf(JOHTO,"ROUTE 38"); int16_t mag=dexOf("MAGNEMITE"); int n=0;
    for (uint16_t i=0;i<wildSlotCount(r38);i++){ WildSlotView s; wildSlotAt(r38,i,s); if (s.dex==mag) n++; }
    ck(n==1, "Johto ROUTE 38 holds exactly one Magnemite slot");
  }

  // --- packs: an area needs the pack of every species it can hand out
  {
    Pet p; live(p,4); at(p,-1); badges(p,JOHTO,0xFF);
    uint16_t r34=areaOf(JOHTO,"ROUTE 34"); uint8_t l;
    gRegionArt = (uint16_t)(1u<<JOHTO);
    ck(wildAreaWhyClosed(p,r34)==WILD_LOCKED && wildRollArea(p,r34,l)==-1,
       "Johto ROUTE 34 (all Kanto species) needs the Kanto pack");
    gRegionArt |= (uint16_t)(1u<<KANTO);
    ck(wildAreaOpen(p,r34), "and opens once it is installed");
    gRegionArt = 0xFFFF;
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
