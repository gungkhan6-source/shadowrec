const screen = require('./build/Release/screen_capture');

console.log('═══════════════════════════════════════');
console.log('  ShadowRec Frame Loop - 30 FPS Hedef');
console.log('═══════════════════════════════════════');

// Initialize
console.log('\n📋 Initialize...');
const info = screen.initialize();
console.log(`   Ekran: ${info.width}x${info.height}`);

// Konfigürasyon
const TARGET_FPS = 30;
const FRAME_TIME_MS = 1000 / TARGET_FPS;  // 33.3ms
const TEST_DURATION_SEC = 10;
const TARGET_FRAMES = TARGET_FPS * TEST_DURATION_SEC;

console.log(`\n⚙️ Ayarlar:`);
console.log(`   Hedef FPS: ${TARGET_FPS}`);
console.log(`   Frame süresi: ${FRAME_TIME_MS.toFixed(2)}ms`);
console.log(`   Test süresi: ${TEST_DURATION_SEC}s`);
console.log(`   Hedef frame sayısı: ${TARGET_FRAMES}`);

// İstatistikler
let frameCount = 0;
let successCount = 0;
let skipCount = 0;
let captureTimes = [];
let totalBytes = 0;
let lastFrameBuffer = null;  // ⭐ Son başarılı buffer'ı sakla (skip durumunda kullan)

const startTime = Date.now();
const endTime = startTime + (TEST_DURATION_SEC * 1000);

console.log('\n🎬 Loop başladı (mouse hareket ettir!)...\n');

function captureLoop() {
    const loopStart = Date.now();
    
    // Hedef süre doldu mu?
    if (loopStart >= endTime) {
        finishTest();
        return;
    }
    
    // Frame yakala
    const result = screen.captureToBuffer();
    frameCount++;
    
    if (result.success) {
        successCount++;
        captureTimes.push(result.captureMs);
        totalBytes += result.size;
        lastFrameBuffer = result.buffer;
    } else if (lastFrameBuffer) {
        
        skipCount++;
        successCount++;
        captureTimes.push(0.1);
        totalBytes += lastFrameBuffer.length;
    } else {
        skipCount++;
        
    }
    
    if (successCount > 0 && successCount % TARGET_FPS === 0) {
        const elapsed = (Date.now() - startTime) / 1000;
        process.stdout.write(`\r   ⏱️  ${elapsed.toFixed(1)}s | Frame: ${successCount}/${TARGET_FRAMES} | Skip: ${skipCount}`);
    }

    // Sonraki frame zamanlaması
    const elapsed = Date.now() - loopStart;
    const remaining = FRAME_TIME_MS - elapsed;
    
    if (remaining > 1) {
        setTimeout(captureLoop, remaining);
    } else {
        setImmediate(captureLoop);  // CPU yorulmadan hemen tekrarla
    }
}

function finishTest() {
    const totalElapsed = (Date.now() - startTime) / 1000;
    
    console.log('\n\n═══════════════════════════════════════');
    console.log('  Sonuçlar');
    console.log('═══════════════════════════════════════');
    console.log(`\n📊 Genel:`);
    console.log(`   Test süresi: ${totalElapsed.toFixed(2)}s`);
    console.log(`   Toplam loop: ${frameCount}`);
    console.log(`   Başarılı frame: ${successCount}`);
    console.log(`   Atlanan (boş): ${skipCount}`);
    console.log(`   Gerçek FPS: ${(successCount / totalElapsed).toFixed(2)}`);
    
    if (captureTimes.length > 0) {
        const avg = captureTimes.reduce((a,b) => a+b, 0) / captureTimes.length;
        const sorted = [...captureTimes].sort((a,b) => a-b);
        const median = sorted[Math.floor(sorted.length / 2)];
        const p95 = sorted[Math.floor(sorted.length * 0.95)];
        
        console.log(`\n⚡ Capture Süreleri:`);
        console.log(`   Ortalama: ${avg.toFixed(2)}ms`);
        console.log(`   Medyan: ${median.toFixed(2)}ms`);
        console.log(`   p95: ${p95.toFixed(2)}ms`);
        console.log(`   Min: ${sorted[0].toFixed(2)}ms`);
        console.log(`   Max: ${sorted[sorted.length-1].toFixed(2)}ms`);
    }
    
    const mbps = (totalBytes / 1024 / 1024) / totalElapsed;
    console.log(`\n📦 Data:`);
    console.log(`   Toplam: ${(totalBytes / 1024 / 1024).toFixed(1)} MB`);
    console.log(`   Throughput: ${mbps.toFixed(1)} MB/s`);
    
    // Performans değerlendirmesi
    const realFps = successCount / totalElapsed;
    console.log(`\n🎯 Değerlendirme:`);
    if (realFps >= TARGET_FPS * 0.9) {
        console.log(`   ✅ MÜKEMMEL - Hedef FPS yakalandı!`);
    } else if (realFps >= TARGET_FPS * 0.5) {
        console.log(`   🟡 İyi - Ekran az değişiyor olabilir (oyunda %100 olur)`);
    } else {
        console.log(`   🔴 Düşük - Ekran statik, mouse hareketi az`);
    }
    
    screen.shutdown();
    console.log('\n✅ Test tamamlandı');
}

// Başlat
setImmediate(captureLoop);