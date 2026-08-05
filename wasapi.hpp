#pragma once
#ifdef _WIN32
#include <initguid.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
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
    if (SUCCEEDED(hr)) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void **)&cl);
    if (SUCCEEDED(hr)) hr = cl->GetMixFormat(&fmt);
    if (SUCCEEDED(hr)) hr = cl->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK, 300000, 0, fmt, nullptr);
    if (SUCCEEDED(hr)) hr = cl->GetService(IID_IAudioCaptureClient, (void **)&cap);
    if (FAILED(hr)) { spdlog::error("WASAPI setup failed: {:#x}", (unsigned)hr); return; }
    bool is_float = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         ((WAVEFORMATEXTENSIBLE *)fmt)->SubFormat.Data1 == 3 && ((WAVEFORMATEXTENSIBLE *)fmt)->SubFormat.Data2 == 0 && ((WAVEFORMATEXTENSIBLE *)fmt)->SubFormat.Data3 == 0x0010);
    uint32_t ch = fmt->nChannels;
    uint32_t target = 48000;
    uint32_t pre = target / target;
    if (pre < 1) pre = 1;
    sink_->decim = (int)(target / 8000);
    if (sink_->decim < 1) sink_->decim = 1;
    sink_->filter_rl.init(250.0, target);
    sink_->filter_rr.init(250.0, target);
    sink_->filter_fr_high.init(300.0, target);
    sink_->filter_fr_aa.init(1200.0, target);
    spdlog::info("WASAPI loopback haptics started: {} ch, {} Hz", ch, target);
    cl->Start();
    while (on) {
      Sleep(5);
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
