// LiveHaptics GUI v1.2 - UAI/WinUI3-inspired shell (nav rail + cards + dark titlebar)
// build: g++ -O2 -std=gnu++23 gui.cpp -o LiveHapticsGUI.exe -mwindows -lcomctl32 -lhidapi -lole32 -luuid -ldwmapi -lgdi32
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <hidapi/hidapi.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define IDC_NAV0 90
#define IDC_NAV1 91
#define IDC_NAV2 92
#define IDC_LIST 101
#define IDC_ADD 102
#define IDC_SAVE 103
#define IDC_DEL 104
#define IDC_CAP 105
#define IDC_CH 106
#define IDC_RATE 107
#define IDC_GAIN 108
#define IDC_BASS 109
#define IDC_CAPS 110
#define IDC_M16 111
#define IDC_START 112
#define IDC_CONN 113
#define IDC_BAR 114
#define IDC_INFO 115
#define IDC_ML 116
#define IDC_MR 117
#define IDC_MODE 118
#define IDC_VER 119
#define IDC_VG 120
#define IDC_VB 121
#define IDC_VC 122
static HWND g_win, g_list, g_cap, g_ch, g_rate, g_gain, g_bass, g_capS, g_add, g_save, g_del;
static HWND g_m16, g_start, g_conn, g_bar, g_info, g_ml, g_mr, g_mode;
static HWND g_vg, g_vb, g_vc, g_nav[3], g_card[3], g_ph;
static std::vector<HWND> g_pages[3];
static int g_page = 0;
static HANDLE g_proc = nullptr;
static bool g_want = false;
static int g_lvlL = 0, g_lvlR = 0;
static HBRUSH g_brWin, g_brCard, g_brCtl;
static char g_dir[MAX_PATH];
struct Preset { std::string n; double g, b; int c; };
static std::vector<Preset> g_presets{ {"Music",2,0.17,16}, {"Game",2.5,0.25,10}, {"Movie",3,0.3,14} };
static double SldGain() { return SendMessage(g_gain, TBM_GETPOS, 0, 0) / 10.0; }
static double SldBass() { return SendMessage(g_bass, TBM_GETPOS, 0, 0) / 100.0; }
static int SldCap() { return (int)SendMessage(g_capS, TBM_GETPOS, 0, 0); }
static void UpdateSldLabels() {
  char b[32];
  sprintf(b, "%.1f", SldGain()); SetWindowTextA(g_vg, b);
  sprintf(b, "%.2f", SldBass()); SetWindowTextA(g_vb, b);
  sprintf(b, "%d", SldCap()); SetWindowTextA(g_vc, b);
}
static void LoadPresets() {
  char p[MAX_PATH]; sprintf(p, "%s\\lh_presets.txt", g_dir);
  FILE *f = fopen(p, "r"); if (!f) return;
  char n[128]; double g, b; int c;
  while (fscanf(f, "%127[^\t]\t%lf\t%lf\t%d\n", n, &g, &b, &c) == 4) g_presets.push_back({ n,g,b,c });
  fclose(f);
}
static void SavePresets() {
  char p[MAX_PATH]; sprintf(p, "%s\\lh_presets.txt", g_dir);
  FILE *f = fopen(p, "w"); if (!f) return;
  for (auto &q : g_presets) fprintf(f, "%s\t%.2f\t%.2f\t%d\n", q.n.c_str(), q.g, q.b, q.c);
  fclose(f);
}
static void RefreshList() {
  SendMessage(g_list, LB_RESETCONTENT, 0, 0);
  for (auto &q : g_presets) SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM)q.n.c_str());
  SendMessage(g_list, LB_SETCURSEL, 0, 0);
}
static void PopulateCap() {
  SendMessage(g_cap, CB_RESETCONTENT, 0, 0);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator *en = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&en)) && en) {
    IMMDeviceCollection *col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
      UINT n = 0; col->GetCount(&n);
      for (UINT i = 0; i < n; i++) {
        IMMDevice *dev = nullptr;
        if (SUCCEEDED(col->Item(i, &dev)) && dev) {
          IPropertyStore *ps = nullptr;
          if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
            PROPVARIANT pv; PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.pwszVal) {
              char buf[256]; WideCharToMultiByte(CP_UTF8, 0, pv.pwszVal, -1, buf, 256, nullptr, nullptr);
              SendMessageA(g_cap, CB_ADDSTRING, 0, (LPARAM)buf);
            }
            PropVariantClear(&pv); ps->Release();
          }
          dev->Release();
        }
      }
      col->Release();
    }
    en->Release();
  }
  SendMessage(g_cap, CB_SETCURSEL, 0, 0);
}
static void PopulateHid() {
  char keep[96] = {0}; int ki = (int)SendMessage(g_ch, CB_GETCURSEL, 0, 0);
  if (ki >= 0) SendMessage(g_ch, CB_GETLBTEXT, ki, (LPARAM)keep);
  SendMessage(g_ch, CB_RESETCONTENT, 0, 0);
  hid_init();
  struct hid_device_info *l = hid_enumerate(0x28de, 0);
  int idx = 0, first_ok = -1;
  for (auto *e = l; e; e = e->next)
    if ((e->product_id == 0x1302 || e->product_id == 0x1304 || e->product_id == 0x1303) && e->usage_page == 0xFF00) {
      bool ok = false; hid_device *d = hid_open_path(e->path);
      if (d) { uint8_t pr[64] = {0}; pr[0] = 0x88; ok = hid_write(d, pr, 64) >= 0; hid_close(d); }
      if (ok && first_ok < 0) first_ok = idx;
      char buf[96]; sprintf(buf, "Controller PID %04x #%d%s", e->product_id, idx++, ok ? "  <haptics>" : "");
      SendMessageA(g_ch, CB_ADDSTRING, 0, (LPARAM)buf);
    }
  hid_free_enumeration(l);
  int w = keep[0] ? (int)SendMessage(g_ch, CB_FINDSTRINGEXACT, -1, (LPARAM)keep) : -1;
  SendMessage(g_ch, CB_SETCURSEL, w >= 0 ? w : (first_ok >= 0 ? first_ok : 0), 0);
}
static void StopCore() {
  g_want = false;
  if (g_proc) {
    char p[MAX_PATH]; sprintf(p, "%s\\lh.cmd", g_dir);
    FILE *f = fopen(p, "w"); if (f) { fputs("stop\n", f); fclose(f); }
    if (WaitForSingleObject(g_proc, 2000) != WAIT_OBJECT_0) TerminateProcess(g_proc, 0);
    CloseHandle(g_proc); g_proc = nullptr;
  }
  SetWindowTextA(g_start, "Start");
}
static void StartCore() {
  if (g_proc) return;
  std::string c = "\""; c += g_dir; c += "\\core.exe\"";
  char num[96];
  sprintf(num, " --gain %.1f", SldGain()); c += num;
  sprintf(num, " --bass %.2f", SldBass()); c += num;
  sprintf(num, " --cap %d", SldCap()); c += num;
  if (SendMessage(g_m16, BM_GETCHECK, 0, 0)) c += " --16bit";
  int ci = (int)SendMessage(g_ch, CB_GETCURSEL, 0, 0);
  char txt[96] = {0};
  if (ci >= 0) { SendMessage(g_ch, CB_GETLBTEXT, ci, (LPARAM)txt); sprintf(num, " --dev %d", ci); c += num; }
  bool wired = strstr(txt, "1302") != nullptr;
  int ri = (int)SendMessage(g_rate, CB_GETCURSEL, 0, 0);
  if (!wired && ri == 1) c += " --rate 4000";
  if (!wired && ri == 2) c += " --rate 8000";
  int cai = (int)SendMessage(g_cap, CB_GETCURSEL, 0, 0);
  if (cai >= 0) {
    char dn[256]; SendMessage(g_cap, CB_GETLBTEXT, cai, (LPARAM)dn);
    c += " --device \""; c += dn; c += "\"";
  }
  { char p[MAX_PATH]; sprintf(p, "%s\\lh.cmd", g_dir); remove(p); }
  STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
  std::vector<char> buf(c.begin(), c.end()); buf.push_back(0);
  if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, g_dir, &si, &pi)) {
    CloseHandle(pi.hThread); g_proc = pi.hProcess; g_want = true;
    SetWindowTextA(g_start, "Pause");
  }
}
static void PushSliders() {
  UpdateSldLabels();
  if (!g_proc) return;
  char p[MAX_PATH]; sprintf(p, "%s\\lh.cmd", g_dir);
  FILE *f = fopen(p, "w");
  if (f) { fprintf(f, "gain %.2f\nbass %.2f\ncap %d\n", SldGain(), SldBass(), SldCap()); fclose(f); }
}
static void ApplyPreset(const Preset &q) {
  SendMessage(g_gain, TBM_SETPOS, TRUE, (LPARAM)(int)(q.g * 10));
  SendMessage(g_bass, TBM_SETPOS, TRUE, (LPARAM)(int)(q.b * 100));
  SendMessage(g_capS, TBM_SETPOS, TRUE, (LPARAM)q.c);
  PushSliders();
}
static void ShowPage(int i) {
  g_page = i;
  for (int p = 0; p < 3; p++) {
    ShowWindow(g_card[p], p == i ? SW_SHOW : SW_HIDE);
    for (HWND w : g_pages[p]) ShowWindow(w, p == i ? SW_SHOW : SW_HIDE);
  }
  InvalidateRect(g_win, nullptr, TRUE);
}
static void Tick() {
  if (g_want && g_proc && WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) {
    CloseHandle(g_proc); g_proc = nullptr;
    SetWindowTextA(g_conn, "o Reconnecting");
    PopulateHid(); StartCore();
  }
  char p[MAX_PATH]; sprintf(p, "%s\\lh.stat", g_dir);
  FILE *f = fopen(p, "r");
  if (f) {
    int l = 0, r = 0; unsigned long long s = 0; char mode[16] = {0}; unsigned pid = 0;
    if (fscanf(f, "%d %d %llu %15s %x", &l, &r, &s, mode, &pid) == 5) {
      g_lvlL = l; g_lvlR = r;
      SetWindowTextA(g_conn, "* Connected");
      SetWindowTextA(g_mode, mode);
      char b[160];
      sprintf(b, "PID %04x  -  %llu packets", pid, s); SetWindowTextA(g_info, b);
      sprintf(b, "Streaming active - %s - PID %04x", mode, pid); SetWindowTextA(g_bar, b);
      InvalidateRect(g_ml, 0, 0); InvalidateRect(g_mr, 0, 0);
    }
    fclose(f);
  } else if (!g_want) SetWindowTextA(g_conn, "o Disconnected");
}
static HFONT Font(int h, int w = FW_NORMAL) { return CreateFontA(h, 0, 0, 0, w, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI"); }
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK CardProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_COMMAND || m == WM_HSCROLL) return WndProc(g_win, m, w, l);
  if (m == WM_COMMAND || m == WM_HSCROLL) return WndProc(g_win, m, w, l);
  if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORBTN || m == WM_CTLCOLORLISTBOX) {
    HDC dc = (HDC)w;
    SetTextColor(dc, RGB(225, 225, 225));
    SetBkColor(dc, RGB(45, 45, 45));
    return (LRESULT)g_brCard;
  }
  if (m == WM_DRAWITEM) {
    DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT*)l;
    if (d->CtlID == IDC_ML || d->CtlID == IDC_MR) {
      int lvl = d->CtlID == IDC_ML ? g_lvlL : g_lvlR;
      RECT r = d->rcItem;
      HBRUSH bg = CreateSolidBrush(RGB(58, 58, 64)); FillRect(d->hDC, &r, bg); DeleteObject(bg);
      int w2 = (int)((double)lvl / 32768 * (r.right - r.left));
      if (w2 > 0) { RECT f = r; f.right = f.left + w2; HBRUSH gg = CreateSolidBrush(RGB(76, 194, 255)); FillRect(d->hDC, &f, gg); DeleteObject(gg); }
      FrameRect(d->hDC, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
      return (LRESULT)TRUE;
    }
  }
  if (m == WM_PAINT) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT r; GetClientRect(h, &r);
    HBRUSH b = CreateSolidBrush(RGB(45, 45, 45)); HPEN pn = CreatePen(PS_SOLID, 1, RGB(62, 62, 62));
    HGDIOBJ ob = SelectObject(dc, pn); HGDIOBJ ob2 = SelectObject(dc, b);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 14, 14);
    SelectObject(dc, ob); SelectObject(dc, ob2); DeleteObject(pn); DeleteObject(b);
    char t[64]; GetWindowTextA(h, t, 64);
    if (t[0]) {
      SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(190, 190, 190));
      HFONT f = Font(-14, FW_SEMIBOLD); HGDIOBJ of = SelectObject(dc, f);
      RECT tr = { 16, 10, r.right, 30 }; DrawTextA(dc, t, -1, &tr, DT_LEFT | DT_SINGLELINE);
      SelectObject(dc, of); DeleteObject(f);
    }
    EndPaint(h, &ps); return 0;
  }
  if (m == WM_ERASEBKGND) return 1;
  return DefWindowProc(h, m, w, l);
}
static HWND Mk(int page, const char *cls, const char *txt, DWORD st, int x, int y, int w, int hh, int id) {
  HWND h = CreateWindowA(cls, txt, st, x, y, w, hh, g_card[page], (HMENU)(intptr_t)id, 0, 0);
  g_pages[page].push_back(h); return h;
}
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
  case WM_CREATE: {
    g_win = h;
    for (int p = 0; p < 3; p++)
      g_card[p] = CreateWindowA("LHCard", p == 0 ? "Haptics Settings" : p == 1 ? "Haptics Tester" : "Presets", WS_CHILD, 186, 60, 796, 470, h, 0, 0, 0);
    g_nav[0] = CreateWindowA("BUTTON", "Haptics", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 8, 70, 158, 40, h, (HMENU)IDC_NAV0, 0, 0);
    g_nav[1] = CreateWindowA("BUTTON", "Tester", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 8, 116, 158, 40, h, (HMENU)IDC_NAV1, 0, 0);
    g_nav[2] = CreateWindowA("BUTTON", "Presets", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 8, 162, 158, 40, h, (HMENU)IDC_NAV2, 0, 0);
    g_mode = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE, 560, 16, 150, 20, h, (HMENU)IDC_MODE, 0, 0);
    g_conn = CreateWindowA("STATIC", "o Disconnected", WS_CHILD | WS_VISIBLE, 720, 16, 170, 20, h, (HMENU)IDC_CONN, 0, 0);
    g_start = CreateWindowA("BUTTON", "Start", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 900, 10, 82, 30, h, (HMENU)IDC_START, 0, 0);
    g_bar = CreateWindowA("STATIC", "Stopped - press Start", WS_CHILD | WS_VISIBLE, 186, 538, 700, 20, h, (HMENU)IDC_BAR, 0, 0);
    CreateWindowA("STATIC", "v1.2-gui", WS_CHILD | WS_VISIBLE, 900, 538, 82, 20, h, (HMENU)IDC_VER, 0, 0);
    // page 0: haptics
    Mk(0, "STATIC", "Capture device", WS_CHILD | WS_VISIBLE, 24, 48, 140, 18, 0);
    g_cap = Mk(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 170, 44, 380, 300, IDC_CAP);
    Mk(0, "STATIC", "Controller", WS_CHILD | WS_VISIBLE, 24, 82, 140, 18, 0);
    g_ch = Mk(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 170, 78, 380, 300, IDC_CH);
    Mk(0, "STATIC", "Wireless rate", WS_CHILD | WS_VISIBLE, 24, 116, 140, 18, 0);
    g_rate = Mk(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 170, 112, 380, 300, IDC_RATE);
    SendMessageA(g_rate, CB_ADDSTRING, 0, (LPARAM)"Auto (recommended)");
    SendMessageA(g_rate, CB_ADDSTRING, 0, (LPARAM)"4 kHz - clean wireless");
    SendMessageA(g_rate, CB_ADDSTRING, 0, (LPARAM)"8 kHz - hi-fi (may pop)");
    SendMessage(g_rate, CB_SETCURSEL, 0, 0);
    Mk(0, "STATIC", "Gain", WS_CHILD | WS_VISIBLE, 24, 156, 140, 18, 0);
    g_gain = Mk(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 170, 150, 330, 30, IDC_GAIN);
    SendMessage(g_gain, TBM_SETRANGE, TRUE, MAKELPARAM(0, 50)); SendMessage(g_gain, TBM_SETPOS, TRUE, 20);
    g_vg = Mk(0, "STATIC", "2.0", WS_CHILD | WS_VISIBLE, 510, 156, 50, 18, IDC_VG);
    Mk(0, "STATIC", "Bass", WS_CHILD | WS_VISIBLE, 24, 190, 140, 18, 0);
    g_bass = Mk(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 170, 184, 330, 30, IDC_BASS);
    SendMessage(g_bass, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100)); SendMessage(g_bass, TBM_SETPOS, TRUE, 17);
    g_vb = Mk(0, "STATIC", "0.17", WS_CHILD | WS_VISIBLE, 510, 190, 50, 18, IDC_VB);
    Mk(0, "STATIC", "Latency cap", WS_CHILD | WS_VISIBLE, 24, 224, 140, 18, 0);
    g_capS = Mk(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 170, 218, 330, 30, IDC_CAPS);
    SendMessage(g_capS, TBM_SETRANGE, TRUE, MAKELPARAM(0, 32)); SendMessage(g_capS, TBM_SETPOS, TRUE, 16);
    g_vc = Mk(0, "STATIC", "16", WS_CHILD | WS_VISIBLE, 510, 224, 50, 18, IDC_VC);
    g_m16 = Mk(0, "BUTTON", "Force 16-bit (wired)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 258, 220, 22, IDC_M16);
    // page 1: tester
    Mk(1, "STATIC", "L", WS_CHILD | WS_VISIBLE, 24, 52, 14, 18, 0);
    g_ml = Mk(1, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 44, 50, 520, 22, IDC_ML);
    Mk(1, "STATIC", "R", WS_CHILD | WS_VISIBLE, 24, 82, 14, 18, 0);
    g_mr = Mk(1, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 44, 80, 520, 22, IDC_MR);
    g_info = Mk(1, "STATIC", "", WS_CHILD | WS_VISIBLE, 24, 116, 400, 20, IDC_INFO);
    g_ph = Mk(1, "STATIC", "[ controller image goes here ]", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_ETCHEDFRAME, 200, 160, 380, 240, 0);
    // page 2: presets
    g_list = Mk(2, "LISTBOX", "", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, 24, 48, 300, 330, IDC_LIST);
    g_add = Mk(2, "BUTTON", "Add Preset", WS_CHILD | WS_VISIBLE, 24, 388, 300, 28, IDC_ADD);
    g_save = Mk(2, "BUTTON", "Save Selected", WS_CHILD | WS_VISIBLE, 24, 420, 300, 28, IDC_SAVE);
    g_del = Mk(2, "BUTTON", "Delete", WS_CHILD | WS_VISIBLE, 340, 388, 140, 28, IDC_DEL);
    LoadPresets(); RefreshList(); PopulateCap(); PopulateHid(); UpdateSldLabels();
    if (!g_presets.empty()) ApplyPreset(g_presets[0]);
    ShowPage(0);
    SetTimer(h, 1, 200, nullptr);
    HFONT f = Font(-14);
    EnumChildWindows(h, [](HWND c, LPARAM lp) { SendMessage(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)f);
    return 0;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT r; GetClientRect(h, &r);
    HBRUSH rb = CreateSolidBrush(RGB(26, 26, 26)); RECT rail = { 0, 0, 174, r.bottom }; FillRect(dc, &rail, rb); DeleteObject(rb);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    HFONT hf = Font(-24, FW_SEMIBOLD); HGDIOBJ of = SelectObject(dc, hf);
    RECT hr = { 186, 12, 640, 46 }; DrawTextA(dc, "LiveHaptics", -1, &hr, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, of); DeleteObject(hf);
    EndPaint(h, &ps); return 0;
  }
  case WM_ERASEBKGND: { RECT r; GetClientRect(h, &r); FillRect((HDC)w, &r, g_brWin); return 1; }
  case WM_TIMER: Tick(); return 0;
  case WM_HSCROLL: PushSliders(); return 0;
  case WM_DRAWITEM: {
    DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT*)l;
    if (d->CtlID == IDC_START) {
      RECT r = d->rcItem;
      HBRUSH b = CreateSolidBrush((d->itemState & ODS_SELECTED) ? RGB(0, 90, 160) : RGB(0, 120, 212));
      FillRect(d->hDC, &r, b); DeleteObject(b);
      SetBkMode(d->hDC, TRANSPARENT); SetTextColor(d->hDC, RGB(255, 255, 255));
      char t[16]; GetWindowTextA(d->hwndItem, t, 16);
      DrawTextA(d->hDC, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return (LRESULT)TRUE;
    }
    if (d->CtlID >= IDC_NAV0 && d->CtlID <= IDC_NAV2) {
      int i = d->CtlID - IDC_NAV0;
      RECT r = d->rcItem;
      HBRUSH b = CreateSolidBrush(g_page == i ? RGB(58, 58, 58) : RGB(26, 26, 26));
      FillRect(d->hDC, &r, b); DeleteObject(b);
      if (g_page == i) { RECT a = { r.left, r.top + 8, r.left + 3, r.bottom - 8 }; HBRUSH ab = CreateSolidBrush(RGB(76, 194, 255)); FillRect(d->hDC, &a, ab); DeleteObject(ab); }
      SetBkMode(d->hDC, TRANSPARENT); SetTextColor(d->hDC, g_page == i ? RGB(255, 255, 255) : RGB(170, 170, 170));
      char t[32]; GetWindowTextA(d->hwndItem, t, 32);
      RECT tr = { r.left + 16, r.top, r.right, r.bottom }; DrawTextA(d->hDC, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      return (LRESULT)TRUE;
    }
    if (d->CtlID == IDC_ML || d->CtlID == IDC_MR) {
      int lvl = d->CtlID == IDC_ML ? g_lvlL : g_lvlR;
      RECT r = d->rcItem;
      HBRUSH bg = CreateSolidBrush(RGB(58, 58, 64)); FillRect(d->hDC, &r, bg); DeleteObject(bg);
      int w2 = (int)((double)lvl / 32768 * (r.right - r.left));
      if (w2 > 0) { RECT f = r; f.right = f.left + w2; HBRUSH gg = CreateSolidBrush(RGB(76, 194, 255)); FillRect(d->hDC, &f, gg); DeleteObject(gg); }
      FrameRect(d->hDC, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
      return (LRESULT)TRUE;
    }
    break;
  }
  case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN: case WM_CTLCOLORLISTBOX: {
    HDC dc = (HDC)w;
    SetTextColor(dc, RGB(225, 225, 225));
    if ((HWND)l == g_conn) SetTextColor(dc, RGB(108, 203, 95));
    if ((HWND)l == g_mode) SetTextColor(dc, RGB(76, 194, 255));
    bool oncard = false;
    for (int p = 0; p < 3; p++) if (GetParent((HWND)l) == g_card[p]) oncard = true;
    SetBkColor(dc, oncard ? RGB(45, 45, 45) : RGB(31, 31, 31));
    return (LRESULT)(oncard ? g_brCard : ((m == WM_CTLCOLORLISTBOX) ? g_brCtl : g_brWin));
  }
  case WM_COMMAND: {
    if (HIWORD(w) == CBN_SELCHANGE && (HWND)l == g_cap && g_proc) { StopCore(); StartCore(); return 0; }
    if (HIWORD(w) == LBN_SELCHANGE && (HWND)l == g_list) { int r = (int)SendMessage(g_list, LB_GETCURSEL, 0, 0); if (r >= 0 && r < (int)g_presets.size()) ApplyPreset(g_presets[r]); return 0; }
    switch (LOWORD(w)) {
    case IDC_NAV0: ShowPage(0); break;
    case IDC_NAV1: ShowPage(1); break;
    case IDC_NAV2: ShowPage(2); break;
    case IDC_START: if (g_proc) { StopCore(); SetWindowTextA(g_conn, "o Disconnected"); SetWindowTextA(g_bar, "Stopped"); } else { PopulateHid(); StartCore(); } break;
    case IDC_ADD: { char n[32]; sprintf(n, "Custom %zu", g_presets.size() + 1); g_presets.push_back({ n, SldGain(), SldBass(), SldCap() }); SavePresets(); RefreshList(); SendMessage(g_list, LB_SETCURSEL, g_presets.size() - 1, 0); } break;
    case IDC_SAVE: { int r = (int)SendMessage(g_list, LB_GETCURSEL, 0, 0); if (r >= 0) { g_presets[r] = { g_presets[r].n, SldGain(), SldBass(), SldCap() }; SavePresets(); } } break;
    case IDC_DEL: { int r = (int)SendMessage(g_list, LB_GETCURSEL, 0, 0); if (r >= 0 && g_presets.size() > 1) { g_presets.erase(g_presets.begin() + r); SavePresets(); RefreshList(); } } break;
    }
    return 0;
  }
  case WM_CLOSE: StopCore(); DestroyWindow(h); return 0;
  case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProc(h, m, w, l);
}
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
  GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
  char *sl = strrchr(g_dir, '\\'); if (sl) *sl = 0;
  g_brWin = CreateSolidBrush(RGB(31, 31, 31)); g_brCard = CreateSolidBrush(RGB(45, 45, 45)); g_brCtl = CreateSolidBrush(RGB(40, 40, 44));
  INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS };
  InitCommonControlsEx(&icc);
  WNDCLASSA wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = "LHGui";
  wc.hbrBackground = g_brWin; wc.hCursor = LoadCursor(0, IDC_ARROW);
  RegisterClassA(&wc);
  WNDCLASSA cc = {}; cc.lpfnWndProc = CardProc; cc.hInstance = hi; cc.lpszClassName = "LHCard";
  cc.hCursor = LoadCursor(0, IDC_ARROW);
  RegisterClassA(&cc);
  HWND h = CreateWindowA("LHGui", "LiveHaptics", WS_OVERLAPPEDWINDOW, 100, 100, 1010, 610, 0, 0, hi, 0);
  BOOL dk = TRUE; DwmSetWindowAttribute(h, 20, &dk, sizeof(dk));
  HMODULE ux = LoadLibraryA("uxtheme.dll");
  if (ux) {
    typedef int (WINAPI *SPAM)(int); typedef BOOL (WINAPI *ADFW)(HWND, BOOL); typedef void (WINAPI *RICS)();
    SPAM sp = (SPAM)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    ADFW aw = (ADFW)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    RICS rf = (RICS)GetProcAddress(ux, MAKEINTRESOURCEA(104));
    if (sp) sp(1);
    if (aw) aw(h, TRUE);
    if (rf) rf();
    EnumChildWindows(h, [](HWND c, LPARAM) { SendMessage(c, WM_THEMECHANGED, 0, 0); return TRUE; }, 0);
  }
  ShowWindow(h, show); UpdateWindow(h);
  MSG msg; while (GetMessage(&msg, 0, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
  StopCore();
  return 0;
}
