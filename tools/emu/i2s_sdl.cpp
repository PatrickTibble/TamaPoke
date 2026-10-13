#include "ESP_I2S.h"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unistd.h>

static SDL_AudioDeviceID g_dev = 0;
static uint32_t g_rate = 16000;
static bool g_mute = false;
// Leaked: the audio task can still be writing while static destructors run.
static std::mutex &mx() { static std::mutex *m = new std::mutex; return *m; }

void emuAudioMute() { g_mute = true; }

bool I2SClass::begin(int, uint32_t rate, int bits, int slots, int) {
  if (g_mute || bits != 16 || slots != 2) return false;
  // WSLg serves PulseAudio on this socket; not every launch path exports it.
  if (!getenv("PULSE_SERVER") && access("/mnt/wslg/PulseServer", F_OK) == 0)
    setenv("PULSE_SERVER", "unix:/mnt/wslg/PulseServer", 0);
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "emu: no audio (%s)\n", SDL_GetError());
    return false;
  }
  SDL_AudioSpec want = {}, have = {};
  want.freq = (int)rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;
  SDL_AudioDeviceID d = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!d) {
    fprintf(stderr, "emu: no audio device (%s)\n", SDL_GetError());
    return false;
  }
  std::lock_guard<std::mutex> l(mx());
  g_dev = d; g_rate = rate;
  SDL_PauseAudioDevice(g_dev, 0);
  printf("emu: audio on %s at %u Hz\n", SDL_GetCurrentAudioDriver(), (unsigned)rate);
  return true;
}

size_t I2SClass::write(const uint8_t *buf, size_t n) {
  const uint32_t high = g_rate * 4 / 20;   // 50 ms of 16-bit stereo
  for (;;) {
    {
      std::lock_guard<std::mutex> l(mx());
      if (!g_dev) break;
      if (SDL_GetQueuedAudioSize(g_dev) < high) { SDL_QueueAudio(g_dev, buf, (Uint32)n); return n; }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  // closed: keep the task paced rather than spinning
  std::this_thread::sleep_for(std::chrono::microseconds((uint64_t)n * 250000 / g_rate));
  return n;
}

void emuAudioClose() {
  std::lock_guard<std::mutex> l(mx());
  if (g_dev) { SDL_CloseAudioDevice(g_dev); g_dev = 0; }
}
