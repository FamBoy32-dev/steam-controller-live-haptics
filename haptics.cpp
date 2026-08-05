#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>
#include <memory>
#include <cstdio>
#include <chrono>
#include <hidapi/hidapi.h>

struct LowpassFilter {
  double prev_out = 0.0, alpha = 0.0;
  void init(double hz, double sr) { double rc = 1.0/(2.0*M_PI*hz); double dt = 1.0/sr; alpha = dt/(rc+dt); }
  int16_t process(int16_t s) {
    prev_out += alpha * ((double)s - prev_out);
    return (int16_t)std::clamp(prev_out, -32768.0, 32767.0);
  }
};
struct HighpassFilter {
  double prev_in = 0.0, prev_out = 0.0, alpha = 0.0;
  void init(double hz, double sr) { double rc = 1.0/(2.0*M_PI*hz); double dt = 1.0/sr; alpha = rc/(rc+dt); }
  int16_t process(int16_t s) {
    double in = s;
    prev_out = alpha * (prev_out + in - prev_in);
    prev_in = in;
    return (int16_t)std::clamp(prev_out, -32768.0, 32767.0);
  }
};
struct BiquadLP {
  double b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
  void init(double fc, double sr) {
    double Q = 0.70710678;
    double w0 = 2*M_PI*fc/sr, cw = cos(w0), sw = sin(w0);
    double al = sw/(2*Q), a0 = 1+al;
    b0 = (1-cw)/2/a0; b1 = (1-cw)/a0; b2 = b0;
    a1 = -2*cw/a0; a2 = (1-al)/a0;
  }
  int16_t process(int16_t s) {
    double x = s, y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
    x2=x1; x1=x; y2=y1; y1=y;
    return (int16_t)std::clamp(y, -32768.0, 32767.0);
  }
};
struct AAFilter {
  BiquadLP s[2];
  void init(double hz, double sr) { for (auto &f : s) f.init(hz, sr); }
  int16_t process(int16_t x) { for (auto &f : s) x = f.process(x); return x; }
};
const int cBias = 0x84;
const int cClip = 32635;
static const uint8_t MuLawCompressTable[256] = {
0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6,
6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
static uint8_t LinearToMuLawSample(int16_t sample) {
  int sign = (sample >> 8) & 0x80;
  if (sign) sample = (int16_t)-sample;
  if (sample > cClip) sample = cClip;
  sample = (int16_t)(sample + cBias);
  int exponent = (int)MuLawCompressTable[(sample >> 7) & 0xFF];
  int mantissa = (sample >> (exponent + 3)) & 0x0F;
  return (uint8_t)~(sign | (exponent << 4) | mantissa);
}
class MockAudioSink {
public:
  std::queue<std::vector<uint8_t>> queue;
  std::mutex queue_mutex;
  std::vector<uint8_t> temp_accumulator;
  LowpassFilter filter_rl, filter_rr;
  HighpassFilter filter_fr_high;
  LowpassFilter filter_fr_aa;
  AAFilter aa_l, aa_r;
  LowpassFilter bass_l, bass_r;
  int decim = 6;
  int32_t acc_l = 0, acc_r = 0; int cnt = 0;
  MockAudioSink() {
    filter_rl.init(250.0, 48000); filter_rr.init(250.0, 48000);
    filter_fr_high.init(300.0, 48000); filter_fr_aa.init(1200.0, 48000);
    aa_l.init(3900.0, 48000); aa_r.init(3900.0, 48000);
    bass_l.init(150.0, 48000); bass_r.init(150.0, 48000);
  }
  void write_audio(const uint8_t *buffer, size_t bytes) {
    uint32_t frames = (uint32_t)(bytes / (4 * sizeof(int16_t)));
    const int16_t *p = (const int16_t *)buffer;
    for (uint32_t f = 0; f < frames; f++) {
      int idx = f * 4;
      int32_t bl = bass_l.process(p[idx+2]) / 6;
      int32_t br = bass_r.process(p[idx+3]) / 6;
      acc_l += aa_l.process((int16_t)std::clamp((int32_t)p[idx+2] + bl, -32768, 32767));
      acc_r += aa_r.process((int16_t)std::clamp((int32_t)p[idx+3] + br, -32768, 32767));
      if (++cnt >= decim) {
        temp_accumulator.push_back(LinearToMuLawSample((int16_t)std::clamp((acc_l / decim) * 2, -32768, 32767)));
        temp_accumulator.push_back(LinearToMuLawSample((int16_t)std::clamp((acc_r / decim) * 2, -32768, 32767)));
        acc_l = acc_r = 0; cnt = 0;
        if (temp_accumulator.size() >= 62) {
          std::lock_guard<std::mutex> lock(queue_mutex);
          queue.push(std::move(temp_accumulator));
          while (queue.size() > 16) queue.pop();
          temp_accumulator.clear();
        }
      }
    }
  }
};
#include "wasapi.hpp"
#include <windows.h>
std::atomic<bool> running{true};
std::thread wasapi_thread;
static std::vector<hid_device*> g_devs;

static void send_pcm_mode(hid_device *d, uint8_t op, uint8_t side, uint8_t param) {
  uint8_t r[64] = {0};
  r[0] = 0x86; r[1] = op; r[2] = side; r[3] = param;
  hid_write(d, r, 4);
}
static void setup_pcm_8k_ulaw() {
  for (auto *d : g_devs) {
    send_pcm_mode(d, 0x01, 2, 0);
    send_pcm_mode(d, 0x01, 5, 0);
    Sleep(10);
    send_pcm_mode(d, 0x02, 2, 0x08);
    send_pcm_mode(d, 0x02, 5, 0x08);
  }
  Sleep(10);
  printf("PCM setup sent: DISABLE 2+5, ENABLE 2+5 @ 8kHz u-law (0x09)\n");
}
static hid_device *open_controller() {
  std::vector<std::string> paths;
  struct hid_device_info *list = hid_enumerate(0x28de, 0x0000);
  for (auto *e = list; e; e = e->next)
    if (e->product_id == 0x1304 || e->product_id == 0x1302) paths.push_back(e->path);
  hid_free_enumeration(list);
  for (auto &path : paths) {
    hid_device *d = hid_open_path(path.c_str());
    if (d) g_devs.push_back(d);
  }
  printf("Opened %zu HID interface(s)\n", g_devs.size());
  return g_devs.empty() ? nullptr : g_devs[0];
}
int main() {
  printf("Steam Controller (2026) live haptics - final build\n");
  if (hid_init() != 0) { printf("hid_init failed\n"); return 1; }
  hid_device *dev = open_controller();
  if (!dev) { printf("No controller found. Plug in Puck/cable.\n"); return 1; }
  setup_pcm_8k_ulaw();
  if (g_devs.size() > 1) {
    std::vector<hid_device*> good;
    for (size_t i = 0; i < g_devs.size(); ++i) {
      printf("Testing interface %zu: buzzing 1s...\n", i);
      auto end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      while (std::chrono::steady_clock::now() < end) {
        uint8_t report[64] = {0};
        report[0] = 0x88; report[1] = 31;
        for (int j = 0; j < 31; ++j) { uint8_t v = (j % 2) ? 0x80 : 0x00; report[2+j] = v; report[33+j] = v; }
        hid_write(g_devs[i], report, sizeof(report));
        Sleep(4);
      }
      printf("Did you feel the buzz? [y/N]: ");
      int c = getchar();
      while (true) { int d = getchar(); if (d == '\n' || d == EOF) break; }
      if (c == 'y' || c == 'Y') { good.push_back(g_devs[i]); break; }
    }
    if (!good.empty()) { g_devs = good; dev = g_devs[0]; }
    printf("Kept %zu interface(s)\n", g_devs.size());
  }
  auto sink = std::make_shared<MockAudioSink>();
  wasapi_thread = std::thread([sink]() { WasapiLoopbackCapture c(sink); c.run(running); });
  fflush(stdin);
  std::thread([]() { getchar(); running = false; }).detach();
  printf("Streaming live audio at 8kHz u-law. Press Enter to quit.\n");
  int err_count = 0, sent = 0;
  auto next_send = std::chrono::steady_clock::now();
  while (running) {
    std::vector<uint8_t> pkt;
    { std::lock_guard<std::mutex> lock(sink->queue_mutex);
      if (!sink->queue.empty()) { pkt = std::move(sink->queue.front()); sink->queue.pop(); } }
    if (!pkt.empty()) {
      uint8_t report[64] = {0};
      report[0] = 0x88;
      report[1] = (uint8_t)(pkt.size() / 2);
      for (size_t i = 0; i < pkt.size()/2 && i < 31; i++) { report[2+i] = pkt[i*2]; report[33+i] = pkt[i*2+1]; }
      int wr = 0;
      for (auto *d : g_devs) wr = hid_write(d, report, sizeof(report));
      if (wr <= 0 && err_count++ < 5) printf("hid_write failed: %d\n", wr);
      if (++sent % 500 == 0) printf("sent %d packets\n", sent);
      next_send += std::chrono::microseconds(3875);
      if (next_send < std::chrono::steady_clock::now()) next_send = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() < next_send) {}
    } else { Sleep(1); next_send = std::chrono::steady_clock::now(); }
  }
  if (wasapi_thread.joinable()) wasapi_thread.join();
  for (auto *d : g_devs) hid_close(d);
  hid_exit();
  return 0;
}
