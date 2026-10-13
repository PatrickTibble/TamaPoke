// Silent audio for the headless tests, which must not need a sound device. The
// emulator binary links the real audio.cpp instead (see build.sh).
#include "audio.h"

static uint8_t g_emuVol = 7, g_emuMusic = 0;
void audioBegin() {}
void sfxPlay(uint8_t) {}
void audioMusic(uint8_t id) { g_emuMusic = id; }
uint8_t emuMusic() { return g_emuMusic; }   // what the sketch last asked to play, for tests
void audioSetVolume(uint8_t v) { g_emuVol = v > 10 ? 10 : v; }
uint8_t audioVolume() { return g_emuVol; }
void audioSetEnabled(bool) {}
bool audioEnabled() { return true; }
