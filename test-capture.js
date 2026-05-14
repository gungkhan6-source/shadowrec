const screen = require('./build/Release/screen_capture');
const path = require('path');

console.log('═══════════════════════════════════════');
console.log('  Ekran Yakalama Testi');
console.log('═══════════════════════════════════════');

const outputPath = path.join(__dirname, 'screenshot.bmp');

console.log('Ekran yakalanıyor...');
const startTime = Date.now();

try {
    const result = screen.captureScreen(outputPath);
    const elapsed = Date.now() - startTime;
    
    console.log('✅ Başarılı!');
    console.log('   Boyut:', result.width + 'x' + result.height);
    console.log('   Dosya:', result.path);
    console.log('   Süre:', elapsed + 'ms');
    console.log('');
    console.log('📂 Dosyayı aç ve gör:');
    console.log('   ' + outputPath);
} catch (err) {
    console.error('❌ Hata:', err.message);
}