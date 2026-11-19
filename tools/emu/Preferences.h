// In-memory NVS, persisted to a file so the pet survives between runs.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

typedef std::map<std::string, std::vector<uint8_t>> NvsStore;
inline NvsStore &nvs() { static NvsStore s; return s; }
// Leaked on purpose: global Preferences (the live pet's, the party's) close
// their handles from static destructors, which may run after a local static
// set would already have been destroyed.
inline std::set<int> &nvsHandles() { static std::set<int> *s = new std::set<int>; return *s; }
void nvsLoad(const char *path);
void nvsSave(const char *path);

class Preferences {
  // MATCHES THE HARDWARE: begin() opens a handle, end() closes it, and
  // ~Preferences() calls end(). The handle is a plain integer, so a COPY of a
  // Preferences shares it -- and the copy's destructor closes it under the
  // original, whose every later read and write then fails silently. 0 means
  // never begun, which stays lenient so tests can poke the store directly.
  int h = 0;
  bool live() const { return !h || nvsHandles().count(h); }
public:
  NvsStore &kv = nvs();
  ~Preferences() { end(); }
  bool begin(const char *, bool = false) {
    static int next = 0;
    if (h && nvsHandles().count(h)) return false;
    h = ++next;
    nvsHandles().insert(h);
    return true;
  }
  void end() { if (h) nvsHandles().erase(h); }
  void clear() { if (live()) kv.clear(); }
  bool isKey(const char *k) { return live() && kv.count(k) != 0; }
  // Arduino Preferences::remove. The follower journal deletes f_swap with it.
  bool remove(const char *k) { return live() && kv.erase(k) != 0; }

  template <typename T> void putT(const char *k, T v) {
    if (!live()) return;
    std::vector<uint8_t> b(sizeof(T));
    memcpy(b.data(), &v, sizeof(T));
    kv[k] = b;
  }
  template <typename T> T getT(const char *k, T d) {
    if (!live()) return d;
    auto it = kv.find(k);
    if (it == kv.end() || it->second.size() != sizeof(T)) return d;
    T v; memcpy(&v, it->second.data(), sizeof(T)); return v;
  }
  void putUChar(const char *k, uint8_t v) { putT(k, v); }
  uint8_t getUChar(const char *k, uint8_t d = 0) { return getT(k, d); }
  void putChar(const char *k, int8_t v) { putT(k, v); }
  int8_t getChar(const char *k, int8_t d = 0) { return getT(k, d); }
  void putBool(const char *k, bool v) { putT(k, v); }
  bool getBool(const char *k, bool d = false) { return getT(k, d); }
  void putUInt(const char *k, uint32_t v) { putT(k, v); }
  uint32_t getUInt(const char *k, uint32_t d = 0) { return getT(k, d); }
  void putShort(const char *k, int16_t v) { putT(k, v); }
  int16_t getShort(const char *k, int16_t d = 0) { return getT(k, d); }
  void putUShort(const char *k, uint16_t v) { putT(k, v); }
  uint16_t getUShort(const char *k, uint16_t d = 0) { return getT(k, d); }
  void putBytes(const char *k, const void *p, size_t n) {
    if (!live()) return;
    const uint8_t *b = (const uint8_t *)p;
    kv[k] = std::vector<uint8_t>(b, b + n);
  }
  // Size of a stored blob, 0 if absent. The firmware uses it to tell an
  // old, shorter record layout from the current one (see Party::begin).
  size_t getBytesLength(const char *k) {
    if (!live()) return 0;
    auto it = kv.find(k);
    return it == kv.end() ? 0 : it->second.size();
  }
  // MATCHES THE HARDWARE, which is not the obvious behaviour. Arduino's
  // Preferences::getBytes reads the stored length first and, if it is LARGER
  // than the caller's buffer, logs and returns 0 WITHOUT COPYING ANYTHING:
  //
  //     if (len > maxLen) { log_e("not enough space in buffer"); return 0; }
  //
  // This stub used to truncate instead -- copying the first n bytes -- which is
  // the friendlier behaviour and made a whole class of save loss invisible here.
  // Flashing a build with a SMALLER DEX_COUNT over a newer save leaves dexReg,
  // the badge arrays and eggByRegion at their zero initialiser on a real board,
  // while every test on this stub happily read a sane prefix and passed.
  size_t getBytes(const char *k, void *p, size_t n) {
    if (!live()) return 0;
    auto it = kv.find(k);
    if (it == kv.end()) return 0;
    if (it->second.size() > n) return 0;      // hardware copies nothing here
    memcpy(p, it->second.data(), it->second.size());
    return it->second.size();
  }
  void putString(const char *k, const char *v) {
    if (!live()) return;
    kv[k] = std::vector<uint8_t>(v, v + strlen(v) + 1);
  }
  size_t getString(const char *k, char *out, size_t n) {
    auto it = kv.find(k);
    if (!live() || it == kv.end()) { if (n) out[0] = 0; return 0; }
    size_t c = it->second.size() < n ? it->second.size() : n - 1;
    memcpy(out, it->second.data(), c);
    out[c ? c - 1 : 0] = 0;
    return c;
  }
};
