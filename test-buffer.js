const screen = require('./build/Release/screen_capture');
const fs = require('fs');
const path = require('path');

console.log('═══════════════════════════════════════');
console.log('  ShadowRec Buffer Capture Test');
console.log('═══════════════════════════════════════');

// Initialize
console.log('\n📋 Initialize...');
const info = screen.initialize();
console.log(`   Ekran: ${info.width}x${info.height}`);

// Test 1: Buffer ile yakala
console.log('\n📸 Buffer\'a yakala...');
let attempts = 0;
let result = null;

// İlk başarılı frame'i bekle (mouse hareket etmiyor olabilir)
while (attempts < 30) {
    result = screen.captureToBuffer();
    if (result.success) break;
    attempts++;
    // Küçük bekleme
    const wait = Date.now() + 50;
    while (Date.now() < wait) {}
}

if (!result.success) {
    console.log('   ❌ Frame alınamadı (mouse hareket ettir)');
    process.exit(1);
}

console.log(`   ✅ Yakaladım! (${attempts} deneme sonrası)`);
console.log(`   Boyut: ${result.width}x${result.height}`);
console.log(`   Buffer: ${result.size} byte (${(result.size/1024/1024).toFixed(2)} MB)`);
console.log(`   Yakalama: ${result.captureMs.toFixed(2)}ms`);
console.log(`   Buffer kopya: ${result.copyMs.toFixed(2)}ms`);

// Test 2: Buffer'ı dosyaya yaz (kontrol için)
// BMP header ekleyip yazıyoruz, sadece kontrol amaçlı
console.log('\n💾 Buffer\'ı BMP olarak kaydet (kontrol için)...');
const bmpHeader = Buffer.alloc(54);
const fileSize = result.size + 54;

// BMP file header
bmpHeader.writeUInt16LE(0x4D42, 0);           // "BM"
bmpHeader.writeUInt32LE(fileSize, 2);          // dosya boyutu
bmpHeader.writeUInt32LE(54, 10);               // pixel data offset

// BMP info header
bmpHeader.writeUInt32LE(40, 14);               // info header size
bmpHeader.writeInt32LE(result.width, 18);      // width
bmpHeader.writeInt32LE(-result.height, 22);    // negatif = top-down
bmpHeader.writeUInt16LE(1, 26);                // planes
bmpHeader.writeUInt16LE(32, 28);               // bits per pixel
bmpHeader.writeUInt32LE(0, 30);                // compression
bmpHeader.writeUInt32LE(result.size, 34);      // image size

const outPath = path.join(__dirname, 'buffer-test.bmp');
const fileBuffer = Buffer.concat([bmpHeader, result.buffer]);
fs.writeFileSync(outPath, fileBuffer);
console.log(`   Yazıldı: ${outPath}`);

// Test 3: Benchmark - 100 buffer yakala
console.log('\n⚡ 100 buffer benchmark...');
const times = [];
let success = 0;

const benchStart = Date.now();
for (let i = 0; i < 100; i++) {
    const r = screen.captureToBuffer();
    if (r.success) {
        times.push(r.captureMs + r.copyMs);
        success++;
    }
}
const benchTotal = Date.now() - benchStart;

if (times.length > 0) {
    const avg = times.reduce((a,b) => a+b, 0) / times.length;
    const min = Math.min(...times);
    const max = Math.max(...times);
    
    console.log(`   Başarılı: ${success}/100`);
    console.log(`   Toplam: ${benchTotal}ms`);
    console.log(`   Ortalama: ${avg.toFixed(2)}ms per buffer`);
    console.log(`   Min: ${min.toFixed(2)}ms`);
    console.log(`   Max: ${max.toFixed(2)}ms`);
    console.log(`   🚀 FPS: ${(1000/avg).toFixed(1)}`);
    
    // Gerçek throughput hesabı
    const dataMBps = (success * result.size) / (benchTotal / 1000) / 1024 / 1024;
    console.log(`   📊 Throughput: ${dataMBps.toFixed(1)} MB/s`);
}

screen.shutdown();
console.log('\n✅ Test tamamlandı');
console.log('\n📂 buffer-test.bmp dosyasını aç ve gör (gerçek ekran görüntüsü olmalı)');