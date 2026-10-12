// A badge won in a gym must survive a reboot.
//
// buildSquad() caps the live pet's level by fighting with a COPY of it:
//
//     Pet tmp = pet;
//
// That copied the Preferences inside Pet, and an NVS handle is a plain integer.
// When tmp went out of scope, ~Preferences() called end() -> nvs_close() on the
// handle the LIVE pet was still using. Every save after the first battle of a
// session then failed silently on the board: the badge showed, its areas
// unlocked, and both were gone after the next reboot. The emulator's stub had
// no handles at all, so nothing here could see it until it modelled them.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=41; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void FakeESP::restart(){exit(0);}

void setup();
void startTrainerBattle(uint8_t idx, bool hard);
extern Pet pet;
extern bool battleOpen, btlPetIn;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup();
  pet.dbgHatchAs(4, false);             // CHARMANDER
  pet.ageMinutes = 60UL*30;             // level 31: above Brock's cap, so the copy is capped
  pet.saveNow();
  ck(!pet.hasBadge(0,0,false), "starts with no Boulder Badge");

  startTrainerBattle(0, false);         // Brock, through the real buildSquad()
  ck(battleOpen && btlPetIn, "the battle opened with the live pet in the squad (the copy was made)");

  pet.winBadge(0, 0, false);            // exactly what btlResolve() does on the win
  pet.rename("AFTER");
  {
    Pet rebooted; rebooted.begin();
    ck(rebooted.hasBadge(0,0,false), "the badge won after the squad was built survives a reboot");
    ck(!strcmp(rebooted.nick,"AFTER"), "and so does any other save made after the battle");
    ck(rebooted.level()==31, "with the live pet at its own level, not the battle's cap");
  }

  // a copy is a scratch Pet: it must not write over the player either
  {
    Pet tmp = pet;
    tmp.rename("COPY");
  }
  {
    Pet rebooted; rebooted.begin();
    ck(!strcmp(rebooted.nick,"AFTER"), "a copy of the pet cannot write to the save");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
