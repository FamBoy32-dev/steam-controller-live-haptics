#ifdef _WIN32
#ifndef usleep
#define usleep(us) Sleep((DWORD)((us)/1000))
#endif
#endif
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
#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
static void Sleep(unsigned long ms) { usleep(ms * 1000); }
static struct termios g_oldt; static bool g_rawon = false;
#endif
static double g_gain = 2.0, g_bass = 1.0 / 6.0;
static int g_cap = 16, g_dev = -1, g_rate = 8000, g_rate_set = 0;
static std::string g_device; static bool g_list = false;
static bool g_web = false;
static int g_gain_set = 0;
static int32_t g_env = 0;
static double g_smooth = 2.0;
static bool g_bit16 = false;
static bool g_pick = false;
static std::atomic<int> g_lvlL{0}, g_lvlR{0};
static std::atomic<unsigned long long> g_sent{0};

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
      int32_t bl = (int32_t)(bass_l.process(p[idx+2]) * (g_bass * 3.0));
      int32_t br = (int32_t)(bass_r.process(p[idx+3]) * (g_bass * 3.0));
      acc_l += aa_l.process((int16_t)std::clamp((int32_t)p[idx+2] + bl, -32768, 32767));
      acc_r += aa_r.process((int16_t)std::clamp((int32_t)p[idx+3] + br, -32768, 32767));
      if (++cnt >= decim) {
        int32_t xl = (int32_t)(acc_l / decim), xr = (int32_t)(acc_r / decim);
        int32_t m = abs(xl) > abs(xr) ? abs(xl) : abs(xr);
        if (m > g_env) g_env = m; else g_env -= g_env / 256;
        double gt = g_gain;
        if (g_env > 0 && (double)g_env * gt > 26000.0) gt = 26000.0 / g_env;
        if (gt < g_smooth) g_smooth = gt; else g_smooth += (gt - g_smooth) * 0.02;
        int32_t xl2 = (int32_t)(xl * g_smooth); if (xl2 > 26000) xl2 = 26000 + (xl2 - 26000) / 4; else if (xl2 < -26000) xl2 = -26000 + (xl2 + 26000) / 4;
        int16_t sl = (int16_t)std::clamp(xl2, -32767, 32767);
        int32_t xr2 = (int32_t)(xr * g_smooth); if (xr2 > 26000) xr2 = 26000 + (xr2 - 26000) / 4; else if (xr2 < -26000) xr2 = -26000 + (xr2 + 26000) / 4;
        int16_t sr2 = (int16_t)std::clamp(xr2, -32767, 32767);
        if (abs((int)sl) > g_lvlL.load()) g_lvlL.store(abs((int)sl));
        if (abs((int)sr2) > g_lvlR.load()) g_lvlR.store(abs((int)sr2));
        if (g_bit16) {
          temp_accumulator.push_back(sl & 0xFF); temp_accumulator.push_back((sl >> 8) & 0xFF);
          temp_accumulator.push_back(sr2 & 0xFF); temp_accumulator.push_back((sr2 >> 8) & 0xFF);
        } else {
          temp_accumulator.push_back(LinearToMuLawSample(sl));
          temp_accumulator.push_back(LinearToMuLawSample(sr2));
        }
        acc_l = acc_r = 0; cnt = 0;
        if (temp_accumulator.size() >= (g_bit16 ? 60 : 62)) {
          std::lock_guard<std::mutex> lock(queue_mutex);
          queue.push(std::move(temp_accumulator));
          while ((int)queue.size() > (g_cap < 2 ? 2 : g_cap)) queue.pop();
          temp_accumulator.clear();
        }
      }
    }
  }
};
#ifdef _WIN32
#include "wasapi.hpp"
#elif defined(__APPLE__)
#include "capture_mac.hpp"
#else
#include "capture_linux.hpp"
#endif
#ifdef _WIN32
#include <windows.h>
#endif
std::atomic<bool> running{true};
static void pcm_cleanup(int);
static std::deque<std::array<uint8_t,64>> g_wrq;
static std::mutex g_wrq_m;
static double g_per_adj = 1.0;
static long long g_depth_sum = 0, g_depth_n = 0;
static int g_wfail = 0;
static bool g_bt = false;
#include <cstring>
#ifdef _WIN32
static std::string g_def_id;
static std::string wasapi_default_id() {
  std::string id;
  IMMDeviceEnumerator *en = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&en)) && en) {
    IMMDevice *dev = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) && dev) {
      LPWSTR ws = nullptr;
      if (SUCCEEDED(dev->GetId(&ws)) && ws) {
        int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
        std::string t;
        if (n > 0) { t.resize((size_t)n); WideCharToMultiByte(CP_UTF8, 0, ws, -1, &t[0], n, nullptr, nullptr); t.resize((size_t)n - 1); }
        id = t; CoTaskMemFree(ws);
      }
      dev->Release();
    }
    en->Release();
  }
  return id;
}
#endif
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
    send_pcm_mode(d, 0x02, 2, (uint8_t)(g_bit16 ? 0 : (g_rate == 8000 ? 8 : (g_rate == 4000 ? 9 : (g_rate == 2000 ? 10 : 11)))));
    send_pcm_mode(d, 0x02, 5, (uint8_t)(g_bit16 ? 0 : (g_rate == 8000 ? 8 : (g_rate == 4000 ? 9 : (g_rate == 2000 ? 10 : 11)))));
    usleep(200000);  // let firmware process DISABLE before ENABLE
  }
  Sleep(10);
  printf("PCM setup sent: DISABLE 2+5, ENABLE 2+5 @ %s (param %d)\n", g_bit16 ? "16-bit 8kHz" : "8-bit u-law 8kHz", g_bit16 ? 0 : (g_rate == 8000 ? 8 : 9));
}
static std::vector<int> g_pids;
static std::vector<std::string> g_paths;
static std::vector<bool> g_ok;
static hid_device *open_controller() {
  std::vector<std::pair<std::string,int>> cands;
  struct hid_device_info *list = hid_enumerate(0x28de, 0x0000);
  for (auto *e = list; e; e = e->next)
    if ((e->product_id == 0x1304 || e->product_id == 0x1302 || e->product_id == 0x1303) && e->usage_page == 0xFF00) cands.push_back({e->path, e->product_id});
  hid_free_enumeration(list);
  for (auto &c : cands) {
    hid_device *d = hid_open_path(c.first.c_str());
    if (!d) continue;
    uint8_t probe[64] = {0};
    probe[0] = 0x88; probe[1] = 0;
    bool ok = hid_write(d, probe, sizeof(probe)) >= 0;
    g_devs.push_back(d); g_pids.push_back(c.second); g_paths.push_back(c.first); g_ok.push_back(ok);
  }
  for (int p : g_pids) if (p == 0x1302) g_bit16 = true;
  for (int p : g_pids) if (p == 0x1303) g_bt = true;
    for (int p : g_pids) if (p == 0x1303 && !g_rate_set) {
#ifdef _WIN32
    g_rate = 4000; printf("BT: 4kHz mode (Windows native stack)\n");
#else
for (int p : g_pids) if (p == 0x1303 && !g_rate_set) {
#ifdef _WIN32
    g_rate = 4000; printf("BT: 4kHz mode (Windows native stack)\n");
#else
    g_rate = 1000; printf("BT: 1kHz mode (32 reports/s)\n");
#endif
  }
#endif
  }
  // Puck: 8kHz hi-fi default; GUI selector / --rate 4000 = clean mode
    if (!g_gain_set) g_gain = g_bit16 ? 2.0 : (g_rate == 4000 ? 3.0 : (g_rate == 2000 ? 3.0 : (g_rate == 1000 ? 4.0 : 2.0)));  // per-mode loudness normalize
    printf("Found %zu candidate interface(s) [PID %04x] - %s\n", g_devs.size(), g_pids.empty() ? 0 : (unsigned)g_pids[0], g_bit16 ? "WIRED: 16-bit 8kHz" : (g_rate == 4000 ? "BT: u-law 4kHz" : (g_rate == 2000 ? "BT: u-law 2kHz" : (g_rate == 1000 ? "BT: u-law 1kHz" : "PUCK: 8-bit u-law 8kHz"))));
  return g_devs.empty() ? nullptr : g_devs[0];
}
static void ui_handle(int c) {
  if (c == '+' || c == '=') g_gain *= 1.25;
  else if (c == '-' || c == '_') g_gain /= 1.25;
  else if (c == ']') g_bass *= 1.25;
  else if (c == '[') g_bass /= 1.25;
  else if (c == ',') g_cap = g_cap > 2 ? g_cap - 2 : 2;
  else if (c == '.') g_cap += 2;
  else if (c == 'q' || c == 'Q' || c == '\n' || c == '\r') { running = false; return; }
  else return;
  printf("gain %.2f | bass %.3f | cap %d\n", g_gain, g_bass, g_cap);
}
#ifdef _WIN32
#include <conio.h>
static void ui_loop() {
  printf("UI live: +/- gain, [ ] bass, , . latency, q quit\n");
  while (running.load()) { if (_kbhit()) ui_handle(_getch()); else Sleep(30); }
}
#else
#include <termios.h>
static void ui_loop() {
  printf("UI live: +/- gain, [ ] bass, , . latency, q quit\n");
  if (tcgetattr(0, &g_oldt) == 0) {
    struct termios t = g_oldt; t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t); g_rawon = true;
  }
  while (running.load()) {
    fd_set r; FD_ZERO(&r); FD_SET(0, &r);
    struct timeval tv{0, 30000};
    if (select(1, &r, nullptr, nullptr, &tv) > 0) { int ch = getchar(); if (ch > 0) ui_handle(ch); }
  }
  if (g_rawon) tcsetattr(0, TCSANOW, &g_oldt);
}
#endif
static void cmd_poll() {
  while (running.load()) {
    Sleep(100);
    FILE *f = fopen("lh.cmd", "r");
    if (!f) continue;
    char line[64]; double v;
    while (fgets(line, sizeof line, f)) {
      if (sscanf(line, "gain %lf", &v) == 1) g_gain = v;
      else if (sscanf(line, "bass %lf", &v) == 1) g_bass = v;
    else if (strncmp(line, "stop", 4) == 0) { remove("lh.cmd"); pcm_cleanup(0); }
      else if (sscanf(line, "cap %lf", &v) == 1) g_cap = (int)v;
    }
    fclose(f);
    remove("lh.cmd");
  }
}
#ifdef _WIN32
static void dev_watch() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  while (running.load()) {
    Sleep(500);
    std::string id = wasapi_default_id();
    if (!id.empty() && !g_def_id.empty() && id != g_def_id) {
      printf("default audio device changed - capture restart\n");
      g_def_id = id; running = false;
    }
  }
}
#endif
static void hotplug_watch() {
  while (running.load()) {
    Sleep(1000);
    static long long ls = -1; long long cs = g_sent.load();
    if (cs != ls) { ls = cs; continue; }
    int had = 0;
    for (int p : g_pids) had |= (p == 0x1302 ? 1 : p == 0x1304 ? 2 : p == 0x1303 ? 4 : 0);
    int now = 0; bool any = false;
    struct hid_device_info *list = hid_enumerate(0x28de, 0);
    for (auto *e = list; e; e = e->next)
      if ((e->product_id == 0x1302 || e->product_id == 0x1304 || e->product_id == 0x1303) && e->usage_page == 0xFF00) {
        any = true;
        now |= (e->product_id == 0x1302 ? 1 : e->product_id == 0x1304 ? 2 : 4);
      }
    hid_free_enumeration(list);
    if (!any) { printf("controller disconnected - hot-plug restart\n"); running = false; return; }
    if (had != now) { printf("connection changed - hot-plug restart\n"); running = false; return; }
  }
}
static void stat_write() {
  while (running.load()) {
    Sleep(200);
    FILE *f = fopen("lh.stat", "w");
    if (f) {
      fprintf(f, "%d %d %llu %s %04x\n", g_lvlL.load(), g_lvlR.load(), g_sent.load(),
              g_bit16 ? "16bit8k" : (g_rate == 4000 ? "ulaw4k" : "ulaw8k"),
              g_pids.empty() ? 0 : (unsigned)g_pids[0]);
      fclose(f);
    }
    g_lvlL.store(g_lvlL.load() * 8 / 10);
    g_lvlR.store(g_lvlR.load() * 8 / 10);
  }
}
static int16_t MuLawToLinear(uint8_t u) {
  u = ~u;
  int s = (u & 0x80) ? -1 : 1;
  int e = (u >> 4) & 7;
  int m = u & 15;
  int v = ((m << 1) + 33) << e;
  return (int16_t)(s * (v - 33));
}
#ifndef _WIN32
#include "web_linux.hpp"
#endif
#include <csignal>
#include <deque>
#include <array>
#include <cstring>
static void write_thread_fn() {
  while (true) {
    std::array<uint8_t,64> r; bool have = false;
    { std::lock_guard<std::mutex> lk(g_wrq_m);
      if (!g_wrq.empty()) { r = g_wrq.front(); g_wrq.pop_front(); have = true; } }
    if (have) {
      int wr = 0;
      for (auto *d : g_devs) wr = hid_write(d, r.data(), 64);
      if (wr <= 0) { if (++g_wfail > 100) { printf("controller link dead - restart\n"); running = false; } }
      else g_wfail = 0;
    } else {
      if (!running.load()) return;
      Sleep(1);
    }
  }
}

static void pcm_cleanup(int) {
  uint8_t p = (uint8_t)(g_bit16 ? 0 : (g_rate == 8000 ? 8 : (g_rate == 4000 ? 9 : (g_rate == 2000 ? 10 : 11))));
  for (auto *d : g_devs) { send_pcm_mode(d, 0x01, 2, p); send_pcm_mode(d, 0x01, 5, p); }
  usleep(150000);
  for (auto *d : g_devs) { send_pcm_mode(d, 0x01, 2, 0); send_pcm_mode(d, 0x01, 5, 0); }
  usleep(150000);
  _exit(0);
}
int main(int argc, char **argv) {
#ifdef _WIN32
  timeBeginPeriod(1);
#endif
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    double v = (i + 1 < argc) ? atof(argv[i+1]) : 0;
    if (a == "--gain" && i+1 < argc) { g_gain = v; g_gain_set = 1; i++; }
    else if (a == "--bass" && i+1 < argc) { g_bass = v; i++; }
    else if (a == "--cap" && i+1 < argc) { g_cap = (int)v; i++; }
    else if (a == "--dev" && i+1 < argc) { g_dev = (int)v; i++; }
    else if (a == "--rate" && i+1 < argc) { g_rate = ((int)v == 4000) ? 4000 : 8000; g_rate_set = 1; i++; }
    else if (a == "--web") g_web = true;
    else if (a == "--device" && i+1 < argc) { g_device = argv[++i]; }
    else if (a == "--list") { g_list = true; }
    else if (a == "--16bit") { g_bit16 = true; }
    else if (a == "--pick") { g_pick = true; }
    else if (a == "--help") {
      printf("LiveHaptics v1.1\n  --gain X  loudness (default 2)\n  --bass X  bass shelf amount (default 0.17)\n  --cap N   queued chunks = latency (default 16, lower = tighter)\n  --dev N   use interface N only\n  --rate N  bitrate: 4000 or 8000 (default 8000)\n  --list    show audio devices\n  --device S  capture device containing S\n");
      return 0;
    }
  }
#ifdef _WIN32
  if (g_list) { ListRenderDevices(); return 0; }
#else
    printf("--list is Windows-only\n");
#endif
  printf("Steam Controller (2026) live haptics - final build\n");
  if (hid_init() != 0) { printf("hid_init failed\n"); return 1; }
  hid_device *dev = open_controller();
  if (!dev) { printf("No controller found. Plug in Puck/cable.\n"); return 1; }
  #ifndef _WIN32
  if (!g_devs.empty() && g_pids[0] == 0x1304) {
    int live = -1;
    for (size_t i = 0; i < g_devs.size() && live < 0; ++i) {
      unsigned char b[64];
      int r = hid_read_timeout(g_devs[i], b, sizeof b, 400);
      if (r > 0) live = (int)i;
    }
    if (live < 0) {
      // --dev narrowed us to dead interface(s); scan ALL Puck interfaces
      struct hid_device_info *l = hid_enumerate(0x28de, 0x1304);
      for (struct hid_device_info *e = l; e && live < 0; e = e->next) {
        hid_device *h = hid_open_path(e->path);
        if (!h) continue;
        unsigned char b[64];
        int r = hid_read_timeout(h, b, sizeof b, 400);
        if (r > 0) {
          for (auto *d : g_devs) hid_close(d);
          g_devs.clear(); g_pids.clear();
          g_devs.push_back(h); g_pids.push_back(0x1304);
          live = 0;
          printf("Puck: full scan found the live interface\n");
        } else hid_close(h);
      }
      hid_free_enumeration(l);
    }
    if (live >= 0) { g_dev = live; printf("Puck: streaming on interface %d (IN traffic)\n", g_dev); }
    else printf("Puck: no IN traffic anywhere - wake the controller (Steam button) and retry\n");
  }
#endif
  setup_pcm_8k_ulaw();
  if (!g_pids.empty() && g_pids[0] == 0x1303 && g_cap < 64) g_cap = 64;
  if (!g_pids.empty() && g_pids[0] == 0x1304 && g_cap < 24) g_cap = 24;
  int chosen = g_dev;
  if (chosen < 0) { FILE *f = fopen("livehaptics.cfg", "r"); if (f) { int v = -1, vp = 0; if (fscanf(f, "%d %d", &v, &vp) == 2 && v >= 0 && v < (int)g_devs.size() && (int)g_pids[v] == vp) chosen = v; fclose(f); } }
  if ((int)g_devs.size() > 1 && (g_pick || chosen < 0 || chosen >= (int)g_devs.size())) {
    printf("HID interfaces found (pick your controller):\n");
    for (size_t i = 0; i < g_devs.size(); ++i)
      printf("  [%zu] PID %04x%s %s\n", i, (unsigned)g_pids[i], g_ok[i] ? "  <haptics detected>" : "", g_paths[i].c_str());
    printf("Type number + Enter: ");
    int sel = -1;
    if (scanf("%d", &sel) == 1 && sel >= 0 && sel < (int)g_devs.size()) chosen = sel;
    if (chosen < 0) { for (size_t i = 0; i < g_ok.size(); ++i) if (g_ok[i]) { chosen = (int)i; break; } }
    if (chosen >= 0) { FILE *f = fopen("livehaptics.cfg", "w"); if (f) { fprintf(f, "%d %d", chosen, (int)g_pids[chosen]); fclose(f); } printf("Saved - next launch starts straight away (--pick to re-choose).\n"); }
  }
  if (chosen < 0) { for (size_t i = 0; i < g_ok.size(); ++i) if (g_ok[i]) { chosen = (int)i; break; } }
  if (chosen >= 0 && chosen < (int)g_devs.size()) {
    if (g_devs.size() > 1) printf("Using interface %d\n", chosen);
    hid_device *d = g_devs[chosen];
    for (size_t i = 0; i < g_devs.size(); ++i) if ((int)i != chosen) hid_close(g_devs[i]);
    g_devs = {d}; dev = d;
  }
  auto sink = std::make_shared<MockAudioSink>();
  sink->decim = 48000 / g_rate;
  signal(SIGTERM, pcm_cleanup); signal(SIGINT, pcm_cleanup);
  wasapi_thread = std::thread([sink]() { CaptureBackend c(sink); c.run(running); });
#ifdef _WIN32
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  g_def_id = wasapi_default_id();
#endif
  std::thread([]() { ui_loop(); }).detach();
  std::thread([]() { cmd_poll(); }).detach();
  std::thread([]() { hotplug_watch(); }).detach();
  std::thread([]() { stat_write(); }).detach();
  std::thread([]() { write_thread_fn(); }).detach();
#ifdef _WIN32
  std::thread([]() { dev_watch(); }).detach();
#endif
#ifndef _WIN32
  if (g_web) std::thread([]() { web_server(); }).detach();
#endif
  printf("Streaming live audio at 8kHz u-law. Press Enter to quit.\n");
  int err_count = 0, sent = 0;
  auto next_send = std::chrono::steady_clock::now();
  long long wr_max = 0;
  int qmin = 999;
  while (running) {
    std::vector<uint8_t> pkt;
    static bool primed = false;
    static int prime_target = 24, prime_floor = 0;
    if (!primed && !g_pids.empty() && g_pids[0] == 0x1303) { prime_target = 8; prime_floor = 2; }
    { std::lock_guard<std::mutex> lock(sink->queue_mutex);
      if (!primed && sink->queue.size() >= (size_t)prime_target) primed = true;
      if (primed) { if ((int)sink->queue.size() < qmin) qmin = (int)sink->queue.size();
        g_depth_sum += (long long)sink->queue.size(); g_depth_n++;
        if ((int)sink->queue.size() > prime_floor && !sink->queue.empty()) { pkt = std::move(sink->queue.front()); sink->queue.pop(); } } }
    if (pkt.empty() && primed) {
      auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
      while (pkt.empty() && std::chrono::steady_clock::now() < dl) {
        std::lock_guard<std::mutex> lock(sink->queue_mutex);
        if (!sink->queue.empty()) { pkt = std::move(sink->queue.front()); sink->queue.pop(); }
      }
    }
    if (pkt.empty() && primed) {
      for (int w = 0; w < 3 && pkt.empty(); w++) {
        Sleep(1);
        std::lock_guard<std::mutex> lock(sink->queue_mutex);
        if (!sink->queue.empty()) { pkt = std::move(sink->queue.front()); sink->queue.pop(); }
      }
    }
      static int16_t fl = 0, fr = 0;
      static bool was_gap = false;
      if (pkt.empty()) {
        if (g_bit16) {
          for (int i = 0; i < 31; i++) { fl -= fl / 32; fr -= fr / 32;
            pkt.push_back(fl & 0xFF); pkt.push_back((fl >> 8) & 0xFF);
            pkt.push_back(fr & 0xFF); pkt.push_back((fr >> 8) & 0xFF); }
        } else {
          for (int i = 0; i < 31; i++) { fl -= fl / 4; fr -= fr / 4;
            pkt.push_back(LinearToMuLawSample(fl)); pkt.push_back(LinearToMuLawSample(fr)); }
        }
        was_gap = true;
      } else {
        if (g_bit16) { fl = (int16_t)(pkt[56] | (pkt[57] << 8)); fr = (int16_t)(pkt[58] | (pkt[59] << 8)); }
        else { fl = MuLawToLinear(pkt[60]); fr = MuLawToLinear(pkt[61]); }
        if (was_gap) {
          for (int i = 0; i < 8; i++) {
            double t = (i + 1) / 9.0;
            fl -= fl / 16; fr -= fr / 16;
            if (g_bit16) {
              int16_t l = (int16_t)(pkt[i*4] | (pkt[i*4+1] << 8));
              int16_t r = (int16_t)(pkt[i*4+2] | (pkt[i*4+3] << 8));
              l = (int16_t)(l * t + fl * (1 - t)); r = (int16_t)(r * t + fr * (1 - t));
              pkt[i*4] = l & 0xFF; pkt[i*4+1] = (l >> 8) & 0xFF;
              pkt[i*4+2] = r & 0xFF; pkt[i*4+3] = (r >> 8) & 0xFF;
            } else {
              int16_t l = MuLawToLinear(pkt[i*2]); int16_t r = MuLawToLinear(pkt[i*2+1]);
              l = (int16_t)(l * t + fl * (1 - t)); r = (int16_t)(r * t + fr * (1 - t));
              pkt[i*2] = LinearToMuLawSample(l); pkt[i*2+1] = LinearToMuLawSample(r);
            }
          }
          was_gap = false;
        }
      }
    if (!pkt.empty()) {
      uint8_t report[64] = {0};
      report[0] = 0x88;
      if (g_bit16) {
        report[1] = 30;
        for (size_t i = 0; i < 15 && i*4 + 3 < pkt.size(); i++) {
          report[2 + i*2] = pkt[i*4]; report[3 + i*2] = pkt[i*4+1];
          report[33 + i*2] = pkt[i*4+2]; report[34 + i*2] = pkt[i*4+3];
        }
      } else {
        report[1] = (uint8_t)(pkt.size() / 2);
        for (size_t i = 0; i < pkt.size()/2 && i < 31; i++) { report[2+i] = pkt[i*2]; report[33+i] = pkt[i*2+1]; }
      }
      { std::unique_lock<std::mutex> lk(g_wrq_m);
        if (g_wrq.size() > 6) {
          if (g_bt) { auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
            while (g_wrq.size() > 6 && std::chrono::steady_clock::now() < dl) { lk.unlock(); Sleep(1); lk.lock(); } }
          if (g_wrq.size() > 6) g_wrq.pop_front();
        }
        std::array<uint8_t,64> rr; memcpy(rr.data(), report, 64);
        g_wrq.push_back(rr); }
if (++sent % 500 == 0) {
        double avg = g_depth_n ? (double)g_depth_sum / g_depth_n : 8.0;
        g_per_adj -= (avg - 8.0) * 0.0004;
        if (g_per_adj < 0.994) g_per_adj = 0.994;
        if (g_per_adj > 1.006) g_per_adj = 1.006;
        printf("sent %d packets (qmin=%d, avg=%.1f, adj=%.4f)\n", sent, qmin, avg, g_per_adj);
        wr_max = 0; qmin = 999; g_depth_sum = 0; g_depth_n = 0; }
      g_sent.store(g_sent.load() + 1);
next_send += std::chrono::microseconds((long long)((g_bit16 ? 1875.0 : 31000000.0 / g_rate) * g_per_adj));
      {
        auto nowr = std::chrono::steady_clock::now();
        long long per = g_bit16 ? 1875 : 31000000 / g_rate;
        if (nowr > next_send + std::chrono::microseconds(2 * per)) next_send = nowr;
      }
      if (next_send < std::chrono::steady_clock::now()) next_send = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() < next_send) {}
    } else { Sleep(1); next_send = std::chrono::steady_clock::now(); }
  }
  if (wasapi_thread.joinable()) wasapi_thread.join();
  for (auto *d : g_devs) hid_close(d);
  pcm_cleanup(0);
  return 0;
}
