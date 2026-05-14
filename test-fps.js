const screen = require('./build/Release/screen_capture');
const path = require('path');

console.log('═══════════════════════════════════════');
console.log('  ShadowRec Frame Loop Test');
console.log('═══════════════════════════════════════');

// 1. Initialize (sadece bir kere)
console.log('\n📋 Initialize...');
const info = screen.initialize();
console.log(`   Ekran: ${info.width}x${info.height}`);

// 2. Tek frame test (sample BMP al)
console.log('\n📸 İlk frame yakala (BMP\'ye yaz)...');
const sample = screen.captureFrame(path.join(__dirname, 'fps-sample.bmp'));
console.log(`   Yakala: ${sample.captureTimeMs.toFixed(1)}ms`);
console.log(`   Yaz: ${sample.writeTimeMs.toFixed(1)}ms`);

// 3. Benchmark — 100 frame yakala (BMP yazmadan)
console.log('\n⚡ 100 frame benchmark (BMP yazmadan)...');
const times = [];
let success = 0;
let timeouts = 0;

const benchStart = Date.now();
for (let i = 0; i < 100; i++) {
    const result = screen.captureFrameOnly();
    if (result.success) {
        times.push(result.timeMs);
        success++;
    } else if (result.error === 'timeout') {
        timeouts++;
    }
}
const benchTotal = Date.now() - benchStart;

// 4. İstatistikler
if (times.length > 0) {
    const avg = times.reduce((a,b) => a+b, 0) / times.length;
    const min = Math.min(...times);
    const max = Math.max(...times);
    const fps = 1000 / avg;
    
    console.log(`   Başarılı: ${success}/100`);
    console.log(`   Timeout: ${timeouts}/100`);
    console.log(`   Toplam süre: ${benchTotal}ms`);
    console.log(`   Ortalama: ${avg.toFixed(2)}ms per frame`);
    console.log(`   Min: ${min.toFixed(2)}ms`);
    console.log(`   Max: ${max.toFixed(2)}ms`);
    console.log(`   🚀 FPS potansiyeli: ${fps.toFixed(1)}`);
} else {
    console.log('   ⚠️  Hiç frame alınamadı! Mouse hareket ettir, tekrar dene.');
}

// 5. Temizlik
screen.shutdown();
console.log('\n✅ Test tamamlandı');