#pragma once
#ifdef _WIN32
#include <initguid.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
static const PROPERTYKEY PKEY_DevFriendly = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
static void ListRenderDevices() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator *pEnum = nullptr;
  CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void **)&pEnum);
  if (!pEnum) return;
  IMMDeviceCollection *pColl = nullptr;
  pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pColl);
  UINT n = 0; if (pColl) pColl->GetCount(&n);
  for (UINT i = 0; i < n; i++) {
    IMMDevice *pD = nullptr; if (FAILED(pColl->Item(i, &pD))) continue;
    IPropertyStore *pP = nullptr;
    if (SUCCEEDED(pD->OpenPropertyStore(STGM_READ, &pP))) {
      PROPVARIANT v{};
      if (SUCCEEDED(pP->GetValue(PKEY_DevFriendly, &v)) && v.pwszVal) {
        printf("[%u] %ls\n", i, v.pwszVal);
        CoTaskMemFree(v.pwszVal);
      }
      pP->Release();
    }
    pD->Release();
  }
  if (pColl) pColl->Release();
  pEnum->Release();
}
#include <ksmedia.h>
#include <memory>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include "spdlog/spdlog.h"

class WasapiLoopbackCapture {
public:
  WasapiLoopbackCapture(std::shared_ptr<MockAudioSink> s) : sink_(std::move(s)) {}
  void run(std::atomic<bool> &on) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *en = nullptr;
    IMMDevice *dev = nullptr;
    IAudioClient *cl = nullptr;
    IAudioCaptureClient *cap = nullptr;
    WAVEFORMATEX *fmt = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  IID_IMMDeviceEnumerator, (void **)&en);
    dev = nullptr;
    if (!g_device.empty()) {
      IMMDeviceCollection *pColl = nullptr;
      if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pColl))) {
        UINT count = 0; pColl->GetCount(&count);
        for (UINT di = 0; di < count && !dev; di++) {
          IMMDevice *pCur = nullptr;
          if (FAILED(pColl->Item(di, &pCur))) continue;
          IPropertyStore *pP = nullptr;
          if (SUCCEEDED(pCur->OpenPropertyStore(STGM_READ, &pP))) {
            PROPVARIANT v{};
            if (SUCCEEDED(pP->GetValue(PKEY_DevFriendly, &v)) && v.pwszVal) {
              std::wstring name = v.pwszVal;
              for (auto &c : name) c = towupper(c);
              std::wstring needle;
              for (char c : g_device) needle += (wchar_t)toupper((unsigned char)c);
              if (name.find(needle) != std::wstring::npos) dev = pCur;
            }
            CoTaskMemFree(v.pwszVal);
            pP->Release();
          }
          if (!dev) pCur->Release();
        }
        pColl->Release();
      }
      if (!dev) printf("No device matching '%s' - using default\n", g_device.c_str());
    }
    if (!dev) { hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev); }
    else hr = S_OK;
    if (SUCCEEDED(hr)) hr = dev->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void **)&cl);
    if (SUCCEEDED(hr)) hr = cl->GetMixFormat(&fmt);
    if (SUCCEEDED(hr)) hr = cl->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK, 10000, 0, fmt, nullptr);
    if (SUCCEEDED(hr)) hr = cl->GetService(IID_IAudioCaptureClient, (void **)&cap);
    if (FAILED(hr)) { spdlog::error("WASAPI setup failed: {:#x}", (unsigned)hr); return; }
    bool is_float = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         ((WAVEFORMATEXTENSIBLE *)fmt)->SubFormat.Data1 == 3 && ((WAVEFORMATEXTENSIBLE *)fmt)->SubFormat.Data2 == 0 && ((WAVEFORMATEXTENSIBLE *)fmt)->SubFormat.Data3 == 0x0010);
    uint32_t ch = fmt->nChannels;
    uint32_t target = 48000;
    uint32_t pre = target / target;
    if (pre < 1) pre = 1;
    sink_->decim = (int)(target / g_rate);
    if (sink_->decim < 1) sink_->decim = 1;
    sink_->filter_rl.init(250.0, target);
    sink_->filter_rr.init(250.0, target);
    sink_->filter_fr_high.init(300.0, target);
    sink_->filter_fr_aa.init(1200.0, target);
    spdlog::info("WASAPI loopback haptics started: {} ch, {} Hz", ch, target);
    cl->Start();
    while (on) {
      Sleep(2);
      UINT32 next = 0;
      cap->GetNextPacketSize(&next);
      while (next > 0) {
        BYTE *data = nullptr; UINT32 frames = 0; DWORD flags = 0;
        cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (frames > 0) {
          bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
          std::vector<int16_t> s((size_t)frames * 4);
          for (UINT32 f = 0; f < frames; f += pre) {
            float L = 0, R = 0;
            if (!silent && data) {
              for (UINT32 k = 0; k < pre && f + k < frames; ++k) {
                if (is_float) { float *p = (float *)data + (size_t)(f + k) * ch; L += p[0]; R += ch > 1 ? p[1] : p[0]; }
                else { int16_t *p = (int16_t *)data + (size_t)(f + k) * ch; L += p[0] / 32768.f; R += ch > 1 ? p[1] / 32768.f : L; }
              }
              L /= pre; R /= pre;
            }
            int16_t Li = (int16_t)std::clamp(L * 32767.f, -32768.f, 32767.f);
            int16_t Ri = (int16_t)std::clamp(R * 32767.f, -32768.f, 32767.f);
            s[(size_t)f * 4 + 0] = Li;
            s[(size_t)f * 4 + 1] = (int16_t)(((int32_t)Li + Ri) / 2);
            s[(size_t)f * 4 + 2] = Li;
            s[(size_t)f * 4 + 3] = Ri;
          }
          sink_->write_audio((uint8_t *)s.data(), s.size() * sizeof(int16_t));
        }
        cap->ReleaseBuffer(frames);
        cap->GetNextPacketSize(&next);
      }
    }
    cl->Stop();
  }
private:
  std::shared_ptr<MockAudioSink> sink_;
};
#endif
using CaptureBackend = WasapiLoopbackCapture;
