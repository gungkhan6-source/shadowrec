// ═══════════════════════════════════════════════════════════════════════
// ShadowRec Screen Capture v2 - Persistent Device + Frame Loop
// Device bir kere oluşturulur, her frame yakalama hızlı olur
// Hedef: 30-60 FPS sürekli yakalama
// ═══════════════════════════════════════════════════════════════════════

#include <napi.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ═══════════════════════════════════════════════════════════════════════
// SCREEN CAPTURER SINIFI
// Tüm DirectX kaynaklarını içinde tutar, persistent çalışır
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
    
    // ── Initialize: DirectX ve DXGI'yi bir kez kur ──
    bool Initialize(std::string& errorMsg) {
        if (initialized) return true;
        
        HRESULT hr;
        
        // 1. D3D11 Device
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
        
        // 2. DXGI Adapter
        IDXGIDevice* dxgiDevice = nullptr;
        hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        if (FAILED(hr)) { errorMsg = "DXGIDevice alinamadi"; return false; }
        
        IDXGIAdapter* adapter = nullptr;
        hr = dxgiDevice->GetAdapter(&adapter);
        dxgiDevice->Release();
        if (FAILED(hr)) { errorMsg = "Adapter alinamadi"; return false; }
        
        // 3. Output (birinci ekran)
        IDXGIOutput* output = nullptr;
        hr = adapter->EnumOutputs(0, &output);
        adapter->Release();
        if (FAILED(hr)) { errorMsg = "Ekran bulunamadi"; return false; }
        
        IDXGIOutput1* output1 = nullptr;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
        output->Release();
        if (FAILED(hr)) { errorMsg = "Output1 alinamadi"; return false; }
        
        // 4. Output Duplication
        hr = output1->DuplicateOutput(device, &duplication);
        output1->Release();
        if (FAILED(hr)) { errorMsg = "Duplication baslatilamadi"; return false; }
        
        // 5. Boyutları öğren ve staging texture oluştur
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
    
    // ── Capture: Tek frame yakala (HIZLI - device zaten hazır) ──
    bool CaptureFrame(std::vector<uint8_t>& outPixels, std::string& errorMsg) {
        if (!initialized) { errorMsg = "Initialize edilmedi"; return false; }
        
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        IDXGIResource* resource = nullptr;
        HRESULT hr;
        
        // ⭐ Boş frame'leri atla — gerçek içerikli frame bekle
        // DXGI ilk frame'lerde sadece "pointer hareket etti" sinyali gönderebilir
        bool gotRealFrame = false;
        for (int attempt = 0; attempt < 5; attempt++) {
            // Önceki resource'u temizle
            if (resource) {
                resource->Release();
                resource = nullptr;
                duplication->ReleaseFrame();
            }
            
            hr = duplication->AcquireNextFrame(200, &frameInfo, &resource);
            
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;  // tekrar dene
            }
            if (FAILED(hr)) {
                errorMsg = "AcquireNextFrame failed";
                return false;
            }
            
            // ⭐ LastPresentTime != 0 → gerçek frame var
            if (frameInfo.LastPresentTime.QuadPart != 0) {
                gotRealFrame = true;
                break;
            }
            // Boş frame: pointer hareketi gibi sinyaller, içerik yok
        }
        
        if (!gotRealFrame || !resource) {
            if (resource) resource->Release();
            errorMsg = "timeout";
            return false;
        }
        
        // Texture'a dönüştür
        ID3D11Texture2D* gpuTexture = nullptr;
        hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&gpuTexture);
        resource->Release();
        if (FAILED(hr)) { 
            duplication->ReleaseFrame();
            errorMsg = "GPU texture alinamadi"; 
            return false; 
        }
        
        // GPU → CPU kopyala
        context->CopyResource(cpuTexture, gpuTexture);
        gpuTexture->Release();
        
        // Map et ve oku
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = context->Map(cpuTexture, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            errorMsg = "Map failed";
            return false;
        }
        
        // Pixel buffer'ı kopyala
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

// ═══════════════════════════════════════════════════════════════════════
// GLOBAL INSTANCE - Tek bir capturer, her çağrıda kullanılır
// ═══════════════════════════════════════════════════════════════════════
static ScreenCapturer* g_capturer = nullptr;

// ── BMP yazıcı (test için) ──
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
// JAVASCRIPT EXPORT FONKSİYONLARI
// ═══════════════════════════════════════════════════════════════════════

// initialize() - Capturer'ı başlat
Napi::Value Initialize(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_capturer) {
        g_capturer = new ScreenCapturer();
    }
    
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

// captureFrame(filename) - Tek frame yakala, BMP'ye yaz
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

// captureFrameOnly() - BMP yazmadan sadece yakala (benchmark için)
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

// shutdown() - Temizlik
Napi::Value Shutdown(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (g_capturer) {
        delete g_capturer;
        g_capturer = nullptr;
    }
    return Napi::Boolean::New(env, true);
}

// captureToBuffer() - Frame'i Buffer olarak Electron'a döndür
// BMP yazmaz, RAM'den direkt veri verir → ÇOK HIZLI
// Bu fonksiyon FFmpeg'e pipe'lamak için lazım
Napi::Value CaptureToBuffer(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (!g_capturer) {
        Napi::Error::New(env, "Once initialize() cagirin").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Frame'i yakala
    std::vector<uint8_t> pixels;
    std::string errorMsg;
    bool ok = g_capturer->CaptureFrame(pixels, errorMsg);
    
    if (!ok) {
        // Timeout veya hata: null Buffer döndür (renderer skip eder)
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, false));
        result.Set("error", Napi::String::New(env, errorMsg));
        return result;
    }
    
    auto captureTime = std::chrono::high_resolution_clock::now();
    
    // ⭐ Pixel verisini Node Buffer'a KOPYALA
    // Buffer::Copy → C++ vector'unu Node Buffer'a transfer eder
    // Veri Node tarafına geçer, C++ tarafı temizlenir
    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(
        env,
        pixels.data(),
        pixels.size()
    );
    
    auto end = std::chrono::high_resolution_clock::now();
    double captureMs = std::chrono::duration<double, std::milli>(captureTime - start).count();
    double copyMs = std::chrono::duration<double, std::milli>(end - captureTime).count();
    
    // Sonuç objesi: success, buffer, boyut, süre
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

// Modül başlatma
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("initialize", Napi::Function::New(env, Initialize));
    exports.Set("captureFrame", Napi::Function::New(env, CaptureFrame));
    exports.Set("captureFrameOnly", Napi::Function::New(env, CaptureFrameOnly));
    exports.Set("captureToBuffer", Napi::Function::New(env, CaptureToBuffer));  // ⭐ YENİ
    exports.Set("shutdown", Napi::Function::New(env, Shutdown));
    return exports;
}

NODE_API_MODULE(screen_capture, Init)