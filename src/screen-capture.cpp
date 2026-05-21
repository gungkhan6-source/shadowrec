// ═══════════════════════════════════════════════════════════════════════
// ShadowRec Screen Capture v3 - DXGI Desktop + Game Capture (Shared Memory)
// 
// Bu modul iki yöntem sunar:
//   1) DXGI Output Duplication → masaüstü, menüler, browser, borderless oyunlar
//   2) Game Capture (DLL inject + shared memory) → fullscreen exclusive oyunlar
// ═══════════════════════════════════════════════════════════════════════

#include <napi.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#define _CRT_SECURE_NO_WARNINGS
#include <TlHelp32.h>
#include <Psapi.h>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <set>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "psapi.lib")

// ═══════════════════════════════════════════════════════════════════════
// SCREEN CAPTURER SINIFI (DXGI - Phase 1, hiç dokunulmadı)
// ═══════════════════════════════════════════════════════════════════════
class ScreenCapturer {
public:
    ScreenCapturer() :
        device(nullptr),
        context(nullptr),
        duplication(nullptr),
        cpuTexture(nullptr),
        width(0),
        height(0),
        initialized(false) {}

    ~ScreenCapturer() {
        Cleanup();
    }

    bool Initialize(std::string& errorMsg) {
        if (initialized) return true;

        HRESULT hr;
        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &device, &featureLevel, &context
        );
        if (FAILED(hr)) {
            errorMsg = "D3D11Device olusturulamadi";
            return false;
        }

        IDXGIDevice* dxgiDevice = nullptr;
        hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        if (FAILED(hr)) { errorMsg = "DXGIDevice alinamadi"; return false; }

        IDXGIAdapter* adapter = nullptr;
        hr = dxgiDevice->GetAdapter(&adapter);
        dxgiDevice->Release();
        if (FAILED(hr)) { errorMsg = "Adapter alinamadi"; return false; }

        IDXGIOutput* output = nullptr;
        hr = adapter->EnumOutputs(0, &output);
        adapter->Release();
        if (FAILED(hr)) { errorMsg = "Ekran bulunamadi"; return false; }

        IDXGIOutput1* output1 = nullptr;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
        output->Release();
        if (FAILED(hr)) { errorMsg = "Output1 alinamadi"; return false; }

        hr = output1->DuplicateOutput(device, &duplication);
        output1->Release();
        if (FAILED(hr)) { errorMsg = "Duplication baslatilamadi"; return false; }

        DXGI_OUTDUPL_DESC duplDesc;
        duplication->GetDesc(&duplDesc);
        width = duplDesc.ModeDesc.Width;
        height = duplDesc.ModeDesc.Height;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = device->CreateTexture2D(&desc, nullptr, &cpuTexture);
        if (FAILED(hr)) { errorMsg = "CPU texture olusturulamadi"; return false; }

        initialized = true;
        return true;
    }

    bool CaptureFrame(std::vector<uint8_t>& outPixels, std::string& errorMsg) {
        if (!initialized) { errorMsg = "Initialize edilmedi"; return false; }

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        IDXGIResource* resource = nullptr;
        HRESULT hr;

        bool gotRealFrame = false;
        for (int attempt = 0; attempt < 5; attempt++) {
            if (resource) {
                resource->Release();
                resource = nullptr;
                duplication->ReleaseFrame();
            }

            hr = duplication->AcquireNextFrame(200, &frameInfo, &resource);

            if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
            if (FAILED(hr)) { errorMsg = "AcquireNextFrame failed"; return false; }

            if (frameInfo.LastPresentTime.QuadPart != 0) {
                gotRealFrame = true;
                break;
            }
        }

        if (!gotRealFrame || !resource) {
            if (resource) resource->Release();
            errorMsg = "timeout";
            return false;
        }

        ID3D11Texture2D* gpuTexture = nullptr;
        hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&gpuTexture);
        resource->Release();
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            errorMsg = "GPU texture alinamadi";
            return false;
        }

        context->CopyResource(cpuTexture, gpuTexture);
        gpuTexture->Release();

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = context->Map(cpuTexture, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            errorMsg = "Map failed";
            return false;
        }

        outPixels.resize(width * height * 4);
        uint8_t* src = (uint8_t*)mapped.pData;
        uint8_t* dst = outPixels.data();
        for (int y = 0; y < height; y++) {
            memcpy(dst + y * width * 4, src + y * mapped.RowPitch, width * 4);
        }

        context->Unmap(cpuTexture, 0);
        duplication->ReleaseFrame();
        return true;
    }

    void Cleanup() {
        if (cpuTexture) { cpuTexture->Release(); cpuTexture = nullptr; }
        if (duplication) { duplication->Release(); duplication = nullptr; }
        if (context) { context->Release(); context = nullptr; }
        if (device) { device->Release(); device = nullptr; }
        initialized = false;
    }

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }

private:
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGIOutputDuplication* duplication;
    ID3D11Texture2D* cpuTexture;
    int width, height;
    bool initialized;
};

static ScreenCapturer* g_capturer = nullptr;

bool WriteBMP(const std::string& filename, const uint8_t* pixels, int width, int height) {
    BITMAPFILEHEADER bfh = {};
    BITMAPINFOHEADER bih = {};
    int rowSize = width * 4;
    int imageSize = rowSize * height;

    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + imageSize;

    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = imageSize;

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    file.write((char*)&bfh, sizeof(bfh));
    file.write((char*)&bih, sizeof(bih));
    file.write((char*)pixels, imageSize);
    file.close();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// GAME CAPTURE (Phase 4 - YENI)
// DLL inject + Shared memory'den frame okur
// ═══════════════════════════════════════════════════════════════════════

#define SHM_NAME L"ShadowRecFrameBuffer"
#define EVENT_NAME L"ShadowRecFrameEvent"
#define GC_MAX_WIDTH 3840
#define GC_MAX_HEIGHT 2160
#define GC_MAX_FRAME_SIZE (GC_MAX_WIDTH * GC_MAX_HEIGHT * 4)

struct SharedFrameHeader {
    UINT32 width;
    UINT32 height;
    UINT32 frameNumber;
    UINT32 pixelFormat;
    UINT64 timestamp;
    UINT32 dataSize;
    UINT32 reserved;
};

#define SHM_SIZE (sizeof(SharedFrameHeader) + GC_MAX_FRAME_SIZE)

class GameCapture {
public:
    GameCapture() : hMap(NULL), hEvent(NULL), header(NULL), pixels(NULL),
                    lastFrameNumber(0), initialized(false), targetPid(0) {}
    
    ~GameCapture() { Cleanup(); }
    
    // Hedef process'i bul (process adına göre)
    static DWORD FindProcessByName(const std::string& name) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        DWORD pid = 0;
        
        std::wstring wname(name.begin(), name.end());
        
        if (Process32FirstW(snap, &entry)) {
            do {
                
                if (_wcsicmp(entry.szExeFile, wname.c_str()) == 0) {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);
        return pid;
    }
    
    // DLL'i process'e inject et
    static bool InjectDll(DWORD pid, const std::string& dllPath, std::string& errorMsg) {
        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess) {
            errorMsg = "OpenProcess basarisiz, Error=" + std::to_string(GetLastError());
            return false;
        }
        
        SIZE_T pathSize = dllPath.size() + 1;
        LPVOID remotePath = VirtualAllocEx(hProcess, NULL, pathSize,
                                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remotePath) {
            errorMsg = "VirtualAllocEx basarisiz";
            CloseHandle(hProcess);
            return false;
        }
        
        if (!WriteProcessMemory(hProcess, remotePath, dllPath.c_str(), pathSize, NULL)) {
            errorMsg = "WriteProcessMemory basarisiz";
            VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }
        
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        LPVOID loadLibraryAddr = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryA");
        
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)loadLibraryAddr, remotePath, 0, NULL);
        
        if (!hThread) {
            errorMsg = "CreateRemoteThread basarisiz, Error=" + std::to_string(GetLastError());
            VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }
        
        WaitForSingleObject(hThread, 5000);
        
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return true;
    }
    
    bool Start(const std::string& processName, const std::string& dllPath, std::string& errorMsg, bool skipInject = false) {
        if (initialized) { errorMsg = "Zaten baslamış"; return false; }
        
        // Hedef process bul
        targetPid = FindProcessByName(processName);
        if (!targetPid) {
            errorMsg = "Process bulunamadi: " + processName;
            return false;
        }
        
        // Shared memory'i ŞIMDI olustur (DLL bağlanacak)
        hMap = CreateFileMappingW(
            INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
            0, SHM_SIZE, SHM_NAME
        );
        if (!hMap) {
            errorMsg = "CreateFileMapping basarisiz, Error=" + std::to_string(GetLastError());
            return false;
        }
        
        void* mapped = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE);
        if (!mapped) {
            errorMsg = "MapViewOfFile basarisiz";
            CloseHandle(hMap); hMap = NULL;
            return false;
        }
        
        header = (SharedFrameHeader*)mapped;
        pixels = (UINT8*)mapped + sizeof(SharedFrameHeader);
        
        // Header'ı sıfırla (eğer inject yapacaksak; dış injector'a güveniyorsak DOKUNMA)
        if (!skipInject) {
            memset(header, 0, sizeof(SharedFrameHeader));
        }
        
        // Event olustur
        hEvent = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME);
        
        // DLL'i inject et (skipInject=false ise)
        // 32-bit oyunlar için JS tarafı 32-bit injector.exe spawn etmiş olacak,
        // bu durumda skipInject=true geçilir ve sadece SHM'e bağlanırız.
        if (!skipInject) {
            if (!InjectDll(targetPid, dllPath, errorMsg)) {
                Cleanup();
                return false;
            }
        }
        
        initialized = true;
        lastFrameNumber = 0;
        return true;
    }
    
    // Yeni frame oku (bloklamaz, hazırsa true döner)
    bool ReadFrame(std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight,
                   UINT32& outFrameNumber, UINT32& outPixelFormat, int timeoutMs, std::string& errorMsg) {
        if (!initialized || !header) {
            errorMsg = "Game capture baslamadi";
            return false;
        }
        
        // Event ile yeni frame bekle
        if (hEvent && timeoutMs > 0) {
            WaitForSingleObject(hEvent, timeoutMs);
        }
        
        UINT32 frameNum = header->frameNumber;
        if (frameNum == 0 || frameNum == lastFrameNumber) {
            errorMsg = "no_new_frame";
            return false;
        }
        
        int w = header->width;
        int h = header->height;
        if (w <= 0 || h <= 0 || w > GC_MAX_WIDTH || h > GC_MAX_HEIGHT) {
            errorMsg = "invalid_size";
            return false;
        }
        
        size_t size = w * h * 4;
        outPixels.resize(size);
        memcpy(outPixels.data(), pixels, size);
        
        outWidth = w;
        outHeight = h;
        outFrameNumber = frameNum;
        outPixelFormat = header->pixelFormat;  // ⭐ 0=BGRA, 1=RGBA
        lastFrameNumber = frameNum;
        return true;
    }
    
    void Cleanup() {
        if (hEvent) { CloseHandle(hEvent); hEvent = NULL; }
        if (header) { UnmapViewOfFile(header); header = NULL; pixels = NULL; }
        if (hMap) { CloseHandle(hMap); hMap = NULL; }
        initialized = false;
        targetPid = 0;
        lastFrameNumber = 0;
    }
    
    bool IsRunning() const { return initialized; }
    DWORD GetTargetPid() const { return targetPid; }
    UINT32 GetCurrentFrameNumber() const { return header ? header->frameNumber : 0; }

private:
    HANDLE hMap;
    HANDLE hEvent;
    SharedFrameHeader* header;
    UINT8* pixels;
    UINT32 lastFrameNumber;
    bool initialized;
    DWORD targetPid;
};

static GameCapture* g_gameCapture = nullptr;

// ═══════════════════════════════════════════════════════════════════════
// JAVASCRIPT EXPORTS — DXGI (mevcut)
// ═══════════════════════════════════════════════════════════════════════

Napi::Value Initialize(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!g_capturer) g_capturer = new ScreenCapturer();
    std::string errorMsg;
    if (!g_capturer->Initialize(errorMsg)) {
        Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
        return env.Null();
    }
    Napi::Object result = Napi::Object::New(env);
    result.Set("width", Napi::Number::New(env, g_capturer->GetWidth()));
    result.Set("height", Napi::Number::New(env, g_capturer->GetHeight()));
    return result;
}

Napi::Value CaptureFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!g_capturer || !g_capturer->GetWidth()) {
        Napi::Error::New(env, "Once initialize() cagirin").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Dosya yolu bekleniyor").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::string outputPath = info[0].As<Napi::String>().Utf8Value();
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> pixels;
    std::string errorMsg;
    if (!g_capturer->CaptureFrame(pixels, errorMsg)) {
        Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
        return env.Null();
    }
    auto captureTime = std::chrono::high_resolution_clock::now();
    if (!WriteBMP(outputPath, pixels.data(), g_capturer->GetWidth(), g_capturer->GetHeight())) {
        Napi::Error::New(env, "BMP yazilamadi").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto end = std::chrono::high_resolution_clock::now();
    double captureMs = std::chrono::duration<double, std::milli>(captureTime - start).count();
    double writeMs = std::chrono::duration<double, std::milli>(end - captureTime).count();
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, true));
    result.Set("captureTimeMs", Napi::Number::New(env, captureMs));
    result.Set("writeTimeMs", Napi::Number::New(env, writeMs));
    result.Set("width", Napi::Number::New(env, g_capturer->GetWidth()));
    result.Set("height", Napi::Number::New(env, g_capturer->GetHeight()));
    return result;
}

Napi::Value CaptureFrameOnly(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!g_capturer) {
        Napi::Error::New(env, "Once initialize() cagirin").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> pixels;
    std::string errorMsg;
    bool ok = g_capturer->CaptureFrame(pixels, errorMsg);
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, ok));
    result.Set("timeMs", Napi::Number::New(env, ms));
    if (!ok) result.Set("error", Napi::String::New(env, errorMsg));
    return result;
}

Napi::Value Shutdown(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (g_capturer) { delete g_capturer; g_capturer = nullptr; }
    if (g_gameCapture) { delete g_gameCapture; g_gameCapture = nullptr; }
    return Napi::Boolean::New(env, true);
}

Napi::Value CaptureToBuffer(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!g_capturer) {
        Napi::Error::New(env, "Once initialize() cagirin").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> pixels;
    std::string errorMsg;
    bool ok = g_capturer->CaptureFrame(pixels, errorMsg);
    if (!ok) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, false));
        result.Set("error", Napi::String::New(env, errorMsg));
        return result;
    }
    auto captureTime = std::chrono::high_resolution_clock::now();
    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, pixels.data(), pixels.size());
    auto end = std::chrono::high_resolution_clock::now();
    double captureMs = std::chrono::duration<double, std::milli>(captureTime - start).count();
    double copyMs = std::chrono::duration<double, std::milli>(end - captureTime).count();
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, true));
    result.Set("buffer", buffer);
    result.Set("width", Napi::Number::New(env, g_capturer->GetWidth()));
    result.Set("height", Napi::Number::New(env, g_capturer->GetHeight()));
    result.Set("captureMs", Napi::Number::New(env, captureMs));
    result.Set("copyMs", Napi::Number::New(env, copyMs));
    result.Set("size", Napi::Number::New(env, pixels.size()));
    return result;
}

// ═══════════════════════════════════════════════════════════════════════
// JAVASCRIPT EXPORTS — GAME CAPTURE (YENI)
// ═══════════════════════════════════════════════════════════════════════

// gameCaptureStart(processName, dllPath, skipInject=false) → { success, pid, error? }
// Oyun process'ine DLL inject eder, shared memory baglar
// skipInject=true ise inject yapmaz (32-bit oyunlar için, JS tarafı 32-bit injector spawn etmiş olur)
Napi::Value GameCaptureStart(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
        Napi::TypeError::New(env, "processName ve dllPath bekleniyor").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    std::string processName = info[0].As<Napi::String>().Utf8Value();
    std::string dllPath = info[1].As<Napi::String>().Utf8Value();
    bool skipInject = false;
    if (info.Length() >= 3 && info[2].IsBoolean()) {
        skipInject = info[2].As<Napi::Boolean>().Value();
    }
    
    if (g_gameCapture) {
        delete g_gameCapture;
        g_gameCapture = nullptr;
    }
    g_gameCapture = new GameCapture();
    
    std::string errorMsg;
    bool ok = g_gameCapture->Start(processName, dllPath, errorMsg, skipInject);
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, ok));
    if (ok) {
        result.Set("pid", Napi::Number::New(env, g_gameCapture->GetTargetPid()));
        result.Set("skipInject", Napi::Boolean::New(env, skipInject));
    } else {
        result.Set("error", Napi::String::New(env, errorMsg));
        delete g_gameCapture;
        g_gameCapture = nullptr;
    }
    return result;
}

// gameCaptureRead(timeoutMs=100) → { success, buffer, width, height, frameNumber, error? }
Napi::Value GameCaptureRead(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_gameCapture || !g_gameCapture->IsRunning()) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, false));
        result.Set("error", Napi::String::New(env, "Game capture baslamadi"));
        return result;
    }
    
    int timeoutMs = 100;
    if (info.Length() >= 1 && info[0].IsNumber()) {
        timeoutMs = info[0].As<Napi::Number>().Int32Value();
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<uint8_t> pixels;
    int w, h;
    UINT32 frameNum;
    UINT32 pixelFormat = 0;  // 0=BGRA, 1=RGBA
    std::string errorMsg;
    bool ok = g_gameCapture->ReadFrame(pixels, w, h, frameNum, pixelFormat, timeoutMs, errorMsg);
    
    auto captureTime = std::chrono::high_resolution_clock::now();
    
    Napi::Object result = Napi::Object::New(env);
    if (!ok) {
        result.Set("success", Napi::Boolean::New(env, false));
        result.Set("error", Napi::String::New(env, errorMsg));
        return result;
    }
    
    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, pixels.data(), pixels.size());
    
    auto end = std::chrono::high_resolution_clock::now();
    double captureMs = std::chrono::duration<double, std::milli>(captureTime - start).count();
    double copyMs = std::chrono::duration<double, std::milli>(end - captureTime).count();
    
    result.Set("success", Napi::Boolean::New(env, true));
    result.Set("buffer", buffer);
    result.Set("width", Napi::Number::New(env, w));
    result.Set("height", Napi::Number::New(env, h));
    result.Set("frameNumber", Napi::Number::New(env, frameNum));
    result.Set("pixelFormat", Napi::Number::New(env, pixelFormat));  // ⭐ 0=BGRA, 1=RGBA
    result.Set("captureMs", Napi::Number::New(env, captureMs));
    result.Set("copyMs", Napi::Number::New(env, copyMs));
    result.Set("size", Napi::Number::New(env, pixels.size()));
    return result;
}

// gameCaptureStop() → bool
Napi::Value GameCaptureStop(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (g_gameCapture) {
        delete g_gameCapture;
        g_gameCapture = nullptr;
    }
    return Napi::Boolean::New(env, true);
}

// gameCaptureStatus() → { running, pid, frameNumber }
Napi::Value GameCaptureStatus(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Object result = Napi::Object::New(env);
    if (g_gameCapture && g_gameCapture->IsRunning()) {
        result.Set("running", Napi::Boolean::New(env, true));
        result.Set("pid", Napi::Number::New(env, g_gameCapture->GetTargetPid()));
        result.Set("frameNumber", Napi::Number::New(env, g_gameCapture->GetCurrentFrameNumber()));
    } else {
        result.Set("running", Napi::Boolean::New(env, false));
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════
// AUTO-DETECT GAMES (Phase 7)
// 
// EnumProcesses ile çalışan tüm process'leri tarar, her birinin yüklü
// DLL'lerini kontrol eder. Eğer aşağıdaki graphics API DLL'lerinden biri
// yüklüyse, o process bir "oyun" sayılır:
//   - d3d9.dll      → DX9
//   - d3d10.dll     → DX10
//   - d3d11.dll     → DX11
//   - d3d12.dll     → DX12
//   - opengl32.dll  → OpenGL
//   - vulkan-1.dll  → Vulkan
// 
// Filtrelenenler (sistem process'leri, browser'lar, vs):
//   - Windows sistem process'leri
//   - Bilinen tarayıcılar (Chrome, Firefox, vs - bunlar DX11 kullanır ama oyun değil)
//   - ShadowRec'in kendisi
// ═══════════════════════════════════════════════════════════════════════

// Sistem ve bilinen non-game process'ler (case-insensitive)
static const std::vector<std::string> kBlacklistExes = {
    // Windows sistem
    "system", "registry", "smss.exe", "csrss.exe", "wininit.exe", "services.exe",
    "lsass.exe", "winlogon.exe", "svchost.exe", "fontdrvhost.exe", "dwm.exe",
    "explorer.exe", "taskhostw.exe", "RuntimeBroker.exe", "SearchApp.exe",
    "TextInputHost.exe", "ApplicationFrameHost.exe", "ShellExperienceHost.exe",
    "dllhost.exe", "conhost.exe", "ctfmon.exe", "sihost.exe", "spoolsv.exe",
    "audiodg.exe", "WmiPrvSE.exe", "smartscreen.exe", "backgroundTaskHost.exe",
    "MiniBug.exe", "CompPkgSrv.exe", "UserOOBEBroker.exe", "SystemSettings.exe",
    
    // Tarayıcılar (DX11 kullanır ama oyun değil)
    "chrome.exe", "firefox.exe", "msedge.exe", "opera.exe", "brave.exe",
    "msedgewebview2.exe", "iexplore.exe", "vivaldi.exe",
    
    // Discord, Slack, Teams (Electron - DX11)
    "discord.exe", "slack.exe", "teams.exe", "skype.exe", "zoom.exe",
    
    // NVIDIA/AMD overlay'ler
    "NVIDIA Overlay.exe", "nvcontainer.exe", "NVDisplay.Container.exe",
    "nvsphelper64.exe", "NVIDIA Web Helper.exe",
    
    // Steam UI
    "steam.exe", "steamwebhelper.exe", "steamservice.exe", "gameoverlayui64.exe",
    
    // ShadowRec'in kendisi (Electron)
    "shadowrec.exe", "electron.exe",
    
    // VS Code, IDE'ler
    "Code.exe", "devenv.exe", "claude.exe",
    
    // Anti-cheat (banlamayalım)
    "EasyAntiCheat.exe", "BEService.exe", "vgc.exe", "vgk.exe",
    
    // Office, PDF
    "WINWORD.EXE", "EXCEL.EXE", "POWERPNT.EXE", "OUTLOOK.EXE",
    "AcroRd32.exe", "Acrobat.exe",
    
    // WhatsApp, mesajlaşma
    "WhatsApp.exe", "WhatsApp.Root.exe",
    
    // Photoshop, vs
    "Photoshop.exe", "Illustrator.exe", "Premiere.exe", "AfterFX.exe",
    
    // Sistem servisleri
    "MSPCManager.exe", "MSPCManagerCore.exe", "ekrn.exe", "eguiProxy.exe",
    "warp-svc.exe", "FvContainer.exe", "FvContainer.System.exe",
    "PresentMon_x64.exe", "MxNotify.exe", "MxRedirect.exe",
    "SecurityHealthService.exe", "SecurityHealthSystray.exe",
};

bool IsBlacklisted(const std::string& exeName) {
    for (const auto& bad : kBlacklistExes) {
        // Case-insensitive karşılaştırma
        if (exeName.size() != bad.size()) continue;
        bool match = true;
        for (size_t i = 0; i < exeName.size(); i++) {
            if (tolower((unsigned char)exeName[i]) != tolower((unsigned char)bad[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

struct GameInfo {
    DWORD pid;
    std::string exeName;
    std::string windowTitle;
    std::string api;  // "DX9", "DX11", "DX12", "OpenGL", "Vulkan", "DX9+DX11" vs
};

// Process'in pencere başlığını al
struct EnumWindowsData {
    DWORD pid;
    std::string title;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumWindowsData* data = (EnumWindowsData*)lParam;
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    
    if (windowPid != data->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    
    // Pencere başlığını al
    wchar_t title[256] = { 0 };
    int len = GetWindowTextW(hwnd, title, 256);
    if (len > 0) {
        // wchar_t → std::string (UTF-8)
        int sz = WideCharToMultiByte(CP_UTF8, 0, title, len, nullptr, 0, nullptr, nullptr);
        if (sz > 0) {
            data->title.resize(sz);
            WideCharToMultiByte(CP_UTF8, 0, title, len, &data->title[0], sz, nullptr, nullptr);
            return FALSE;  // Bulduk, dur
        }
    }
    return TRUE;
}

std::string GetProcessWindowTitle(DWORD pid) {
    EnumWindowsData data = { pid, "" };
    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    return data.title;
}

// Process'in yüklü modüllerini kontrol et, hangi graphics API'leri kullanıyor öğren
// EnumProcessModulesEx ile LIST_MODULES_ALL: hem 32-bit hem 64-bit modülleri listeler
std::string DetectGraphicsAPI(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) {
        // PROCESS_QUERY_INFORMATION reddedilirse, daha düşük yetkiyle dene
        hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProcess) return "";
    }
    
    HMODULE modules[2048];
    DWORD bytesNeeded = 0;
    
    // ⭐ EnumProcessModulesEx + LIST_MODULES_ALL — cross-architecture (32+64 bit)
    BOOL ok = EnumProcessModulesEx(hProcess, modules, sizeof(modules), &bytesNeeded, LIST_MODULES_ALL);
    if (!ok) {
        // Fallback: normal EnumProcessModules
        ok = EnumProcessModules(hProcess, modules, sizeof(modules), &bytesNeeded);
    }
    
    if (!ok) {
        CloseHandle(hProcess);
        return "";
    }
    
    int moduleCount = bytesNeeded / sizeof(HMODULE);
    std::set<std::string> apis;
    bool hasDgVoodoo = false;
    
    for (int i = 0; i < moduleCount; i++) {
        wchar_t modName[MAX_PATH];
        if (GetModuleBaseNameW(hProcess, modules[i], modName, MAX_PATH)) {
            // Lowercase compare
            std::wstring name(modName);
            std::transform(name.begin(), name.end(), name.begin(),
                [](wchar_t c) { return towlower(c); });
            
            // Standart graphics API
            if (name == L"d3d9.dll") apis.insert("DX9");
            else if (name == L"d3d10.dll" || name == L"d3d10_1.dll") apis.insert("DX10");
            else if (name == L"d3d11.dll") apis.insert("DX11");
            else if (name == L"d3d12.dll") apis.insert("DX12");
            else if (name == L"dxgi.dll") apis.insert("DXGI");  // DX10+
            else if (name == L"opengl32.dll") apis.insert("OpenGL");
            else if (name == L"vulkan-1.dll") apis.insert("Vulkan");
            
            // dgVoodoo wrapper tespiti (T3 ve diğer eski oyunlar için)
            else if (name == L"d3d8.dll") { apis.insert("DX8"); hasDgVoodoo = true; }
            else if (name == L"ddraw.dll") { apis.insert("DDraw"); hasDgVoodoo = true; }
            else if (name == L"dgvoodoo.dll" || name == L"dgvoodoocpl.exe") hasDgVoodoo = true;
        }
    }
    
    CloseHandle(hProcess);
    
    // DXGI tek başına = DX10/11 backend ile gösterilebilir, "DXGI" göstermeyelim çıplak
    if (apis.count("DXGI") && (apis.count("DX10") || apis.count("DX11") || apis.count("DX12"))) {
        apis.erase("DXGI");
    }
    
    // API'leri birleştir
    std::string result;
    for (const auto& a : apis) {
        if (!result.empty()) result += "+";
        result += a;
    }
    if (hasDgVoodoo && !result.empty()) result += " (dgVoodoo)";
    return result;
}

// Hedef process'in 32-bit (WOW64) mı yoksa 64-bit mi olduğunu tespit eder
// true = 32-bit (WOW64 üzerinde çalışıyor), false = 64-bit
bool IsTargetWow64(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;
    
    BOOL isWow64 = FALSE;
    IsWow64Process(hProcess, &isWow64);
    CloseHandle(hProcess);
    return isWow64 ? true : false;
}

// getProcessArchitecture(processName) → { found, pid, isWow64, arch, error? }
// Process adından mimari tespit eder, JS tarafı doğru DLL+injector seçer
Napi::Value GetProcessArchitecture(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Object result = Napi::Object::New(env);
    
    if (info.Length() < 1 || !info[0].IsString()) {
        result.Set("found", Napi::Boolean::New(env, false));
        result.Set("error", Napi::String::New(env, "processName bekleniyor"));
        return result;
    }
    
    std::string processName = info[0].As<Napi::String>().Utf8Value();
    
    // Process'i bul
    DWORD pid = GameCapture::FindProcessByName(processName);
    if (!pid) {
        result.Set("found", Napi::Boolean::New(env, false));
        result.Set("error", Napi::String::New(env, "Process bulunamadi: " + processName));
        return result;
    }
    
    bool isWow64 = IsTargetWow64(pid);
    result.Set("found", Napi::Boolean::New(env, true));
    result.Set("pid", Napi::Number::New(env, pid));
    result.Set("isWow64", Napi::Boolean::New(env, isWow64));
    result.Set("arch", Napi::String::New(env, isWow64 ? "x86" : "x64"));
    return result;
}

// enumGames() → [{ pid, exeName, windowTitle, api, isWow64 }, ...]
Napi::Value EnumGames(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Array result = Napi::Array::New(env);
    
    DWORD myPid = GetCurrentProcessId();
    
    // Tüm process'leri al
    DWORD pids[2048];
    DWORD bytesReturned = 0;
    if (!EnumProcesses(pids, sizeof(pids), &bytesReturned)) {
        return result;
    }
    
    int pidCount = bytesReturned / sizeof(DWORD);
    int found = 0;
    
    for (int i = 0; i < pidCount; i++) {
        DWORD pid = pids[i];
        if (pid == 0 || pid == 4 || pid == myPid) continue;  // System, Idle, kendimiz
        
        // Process adını al
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProcess) continue;
        
        char exeName[MAX_PATH];
        DWORD size = MAX_PATH;
        if (!QueryFullProcessImageNameA(hProcess, 0, exeName, &size)) {
            CloseHandle(hProcess);
            continue;
        }
        
        // ⭐ 32-bit / 64-bit tespiti
        BOOL isWow64 = FALSE;
        IsWow64Process(hProcess, &isWow64);
        
        CloseHandle(hProcess);
        
        // Sadece exe dosya adını al (path'i at)
        std::string fullPath = exeName;
        size_t lastSlash = fullPath.find_last_of("\\/");
        std::string baseName = (lastSlash != std::string::npos) 
            ? fullPath.substr(lastSlash + 1) 
            : fullPath;
        
        // Blacklist kontrolü
        if (IsBlacklisted(baseName)) continue;
        
        // Pencere başlığını al
        std::string windowTitle = GetProcessWindowTitle(pid);
        
        // Graphics API tespit et
        std::string api = DetectGraphicsAPI(pid);
        
        // ⭐ Module listelenemediyse (32-bit Wow64 sınırı veya korumalı process),
        // pencere başlığı varsa ekle - kullanıcı manuel onaylayacak.
        // İçinde 3D oyun anlamına gelen anahtar kelimeler olabilir.
        bool moduleListFailed = api.empty();
        if (moduleListFailed) {
            if (windowTitle.empty()) continue;  // Hem API yok hem pencere yok → oyun değil
            // Pencere başlığı var → muhtemelen oyun, "Unknown" olarak göster
            api = "?";
        }
        
        // JS objesini oluştur
        Napi::Object game = Napi::Object::New(env);
        game.Set("pid", Napi::Number::New(env, pid));
        game.Set("exeName", Napi::String::New(env, baseName));
        game.Set("fullPath", Napi::String::New(env, fullPath));
        game.Set("windowTitle", Napi::String::New(env, windowTitle));
        game.Set("api", Napi::String::New(env, api));
        game.Set("isWow64", Napi::Boolean::New(env, isWow64 ? true : false));  // ⭐ 32-bit mi?
        game.Set("arch", Napi::String::New(env, isWow64 ? "x86" : "x64"));     // ⭐ İnsan okunabilir
        result.Set(found++, game);
    }
    
    return result;
}

// ═══════════════════════════════════════════════════════════════════════
// MODULE INIT
// ═══════════════════════════════════════════════════════════════════════
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // DXGI (mevcut, hiç değişmedi)
    exports.Set("initialize", Napi::Function::New(env, Initialize));
    exports.Set("captureFrame", Napi::Function::New(env, CaptureFrame));
    exports.Set("captureFrameOnly", Napi::Function::New(env, CaptureFrameOnly));
    exports.Set("captureToBuffer", Napi::Function::New(env, CaptureToBuffer));
    exports.Set("shutdown", Napi::Function::New(env, Shutdown));
    
    // Game Capture (YENI - Phase 4)
    exports.Set("gameCaptureStart", Napi::Function::New(env, GameCaptureStart));
    exports.Set("gameCaptureRead", Napi::Function::New(env, GameCaptureRead));
    exports.Set("gameCaptureStop", Napi::Function::New(env, GameCaptureStop));
    exports.Set("gameCaptureStatus", Napi::Function::New(env, GameCaptureStatus));
    
    // Auto-detect games (YENI - Phase 7)
    exports.Set("enumGames", Napi::Function::New(env, EnumGames));
    
    // Architecture detection (YENI - Phase 8 - 32/64-bit)
    exports.Set("getProcessArchitecture", Napi::Function::New(env, GetProcessArchitecture));
    
    return exports;
}

NODE_API_MODULE(screen_capture, Init)
