#include <napi.h>
#include <string>

// Basit toplama fonksiyonu
// Electron tarafından çağrılacak, 2 sayı alıp toplamını döndürür
Napi::Number Add(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    // Parametre kontrolü
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "İki sayı bekleniyor").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }
    
    double a = info[0].As<Napi::Number>().DoubleValue();
    double b = info[1].As<Napi::Number>().DoubleValue();
    double result = a + b;
    
    return Napi::Number::New(env, result);
}

// Selamlama fonksiyonu
// Electron string verir, C++ ekler ve geri döndürür
Napi::String Greet(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "İsim bekleniyor").ThrowAsJavaScriptException();
        return Napi::String::New(env, "");
    }
    
    std::string name = info[0].As<Napi::String>().Utf8Value();
    std::string greeting = "Merhaba " + name + "! ShadowRec Native modülünden selamlar!";
    
    return Napi::String::New(env, greeting);
}

// Sistem bilgisi fonksiyonu
// Hiç parametre almaz, C++ tarafından bilgi döndürür
Napi::String SystemInfo(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    std::string info_str = "ShadowRec Native v0.1 - ";
    
    #ifdef _WIN32
        info_str += "Windows platformu";
    #elif __APPLE__
        info_str += "macOS platformu";
    #else
        info_str += "Linux platformu";
    #endif
    
    return Napi::String::New(env, info_str);
}

// Modül başlatma — bu fonksiyonlar Electron'a expose ediliyor
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("add", Napi::Function::New(env, Add));
    exports.Set("greet", Napi::Function::New(env, Greet));
    exports.Set("systemInfo", Napi::Function::New(env, SystemInfo));
    return exports;
}

NODE_API_MODULE(shadowrec_native, Init)