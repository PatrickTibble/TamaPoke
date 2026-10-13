// I2S for the emulator: the firmware's 16-bit stereo stream goes to the host's
// speakers through SDL (i2s_sdl.cpp). Only the emulator binary links it; the
// headless tests link audio_stub.cpp instead of audio.cpp.
#pragma once
#include <cstddef>
#include <cstdint>

enum { I2S_MODE_STD = 0 };
enum { I2S_DATA_BIT_WIDTH_16BIT = 16 };
enum { I2S_SLOT_MODE_STEREO = 2 };
enum { I2S_STD_SLOT_BOTH = 3 };

class I2SClass {
 public:
  void setPins(int, int, int, int, int) {}
  bool begin(int mode, uint32_t rate, int bits, int slots, int mask = -1);
  // Blocks while ~50 ms is queued, the way a full DMA ring paces the board.
  size_t write(const uint8_t *buf, size_t n);
};

void emuAudioMute();    // before setup(): begin() then fails, as with no codec
void emuAudioClose();   // before SDL_Quit(): the audio task may be mid-write
