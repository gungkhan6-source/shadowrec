const screen = require('./build/Release/screen_capture');
const { spawn } = require('child_process');
const path = require('path');

console.log('═══════════════════════════════════════');
console.log('  ShadowRec → FFmpeg Pipe Test');
console.log('  İlk Native Capture → MP4 Kayıt');
console.log('═══════════════════════════════════════');

// FFmpeg yolu (sende /bin/ffmpeg.exe vardı)
const FFMPEG_PATH = 'F:\\ShadowRec-PC\\bin\\ffmpeg.exe';

// Initialize
console.log('\n📋 Initialize...');
const info = screen.initialize();
console.log(`   Ekran: ${info.width}x${info.height}`);

const TARGET_FPS = 30;
const DURATION_SEC = 5;  // 5 saniyelik kayıt
const FRAME_TIME_MS = 1000 / TARGET_FPS;
const outputFile = path.join(__dirname, 'native-capture.mp4');

console.log(`\n⚙️  Ayarlar:`);
console.log(`   Çözünürlük: ${info.width}x${info.height}`);
console.log(`   FPS: ${TARGET_FPS}`);
console.log(`   Süre: ${DURATION_SEC}s`);
console.log(`   Çıktı: ${outputFile}`);

// FFmpeg'i başlat — stdin'den rawvideo alacak
console.log('\n🚀 FFmpeg başlatılıyor...');
const ffmpeg = spawn(FFMPEG_PATH, [
    '-y',                           // overwrite
    '-hide_banner',
    '-loglevel', 'warning',
    '-stats',
    
    // GİRİŞ: stdin'den rawvideo
    '-f', 'rawvideo',
    '-pixel_format', 'bgra',        // DXGI bize bgra veriyor
    '-video_size', `${info.width}x${info.height}`,
    '-framerate', String(TARGET_FPS),
    '-i', 'pipe:0',                 // stdin
    
    // ÇIKIŞ: MP4
    '-c:v', 'libx264',
    '-preset', 'veryfast',
    '-crf', '23',
    '-pix_fmt', 'yuv420p',          // YouTube uyumlu
    '-vf', 'format=yuv420p',
    
    outputFile
]);

// FFmpeg log
ffmpeg.stderr.on('data', (data) => {
    const msg = data.toString().trim();
    if (msg.includes('frame=') || msg.includes('error')) {
        process.stdout.write(`\r   FFmpeg: ${msg.slice(0, 80)}...`);
    }
});

ffmpeg.on('close', (code) => {
    console.log(`\n\n✅ FFmpeg kapandı (kod: ${code})`);
    console.log(`📂 Çıktı: ${outputFile}`);
    console.log('\nDosyayı oynat ve gör! 🎬');
    screen.shutdown();
});

ffmpeg.on('error', (err) => {
    console.error('❌ FFmpeg hatası:', err.message);
});

// Frame loop — capture et ve FFmpeg'e yaz
let frameCount = 0;
let lastBuffer = null;
const startTime = Date.now();
const endTime = startTime + (DURATION_SEC * 1000);

console.log('\n🎬 Kayıt başladı (mouse hareket ettir, video oynat)...');

function captureLoop() {
    if (Date.now() >= endTime) {
        // ⭐ Süre doldu — FFmpeg'in son frame'leri işlemesini bekle
        console.log(`\n\n📊 Toplam yakalanan frame: ${frameCount}`);
        console.log('⏳ FFmpeg final encoding...');
        
       ffmpeg.stdin.end();  // stdin'i temiz kapat
        // FFmpeg kendi 'close' event'inde sonucu yazacak
        return;
    }
    
    const loopStart = Date.now();
    const result = screen.captureToBuffer();
    
    let bufferToWrite = null;
    if (result.success) {
        bufferToWrite = result.buffer;
        lastBuffer = result.buffer;
    } else if (lastBuffer) {
        bufferToWrite = lastBuffer;  // son frame'i tekrar gönder
    }
    
    if (bufferToWrite) {
        // FFmpeg'in stdin'ine yaz
        const ok = ffmpeg.stdin.write(bufferToWrite);
        frameCount++;
        
        if (!ok) {
            // FFmpeg buffer dolu, drain bekle
            ffmpeg.stdin.once('drain', () => {
                scheduleNext();
            });
            return;
        }
    }
    
    scheduleNext();
}

let nextFrameTime = 0;

function scheduleNext() {
    if (nextFrameTime === 0) {
        nextFrameTime = startTime + FRAME_TIME_MS;
    } else {
        nextFrameTime += FRAME_TIME_MS;
    }
    
    const now = Date.now();
    const wait = nextFrameTime - now;
    
    if (wait > 1) {
        setTimeout(captureLoop, wait);
    } else {

        setImmediate(captureLoop);
    }
}

setImmediate(captureLoop);