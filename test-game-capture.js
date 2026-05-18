// ═══════════════════════════════════════════════════════════════════════
// test-game-capture.js
// Native module game capture pipeline testi
// ═══════════════════════════════════════════════════════════════════════

const screen = require('./build/Release/screen_capture')
const path = require('path')
const fs = require('fs')

const DLL_PATH = 'F:\\game-capture\\hook\\build\\Release\\shadowrec_hook.dll'
const TARGET_PROCESS = 'test_app.exe'

console.log('═══════════════════════════════════════')
console.log('  ShadowRec Game Capture Test')
console.log('═══════════════════════════════════════\n')

// 1. test_app.exe açık mı kontrol et (kullanıcı önce açmalı)
console.log(`Hedef process: ${TARGET_PROCESS}`)
console.log(`DLL: ${DLL_PATH}\n`)

if (!fs.existsSync(DLL_PATH)) {
  console.error(`❌ DLL bulunamadi: ${DLL_PATH}`)
  console.error(`   Once game-capture'i derle: cd F:\\game-capture\\hook\\build && cmake --build . --config Release`)
  process.exit(1)
}

// 2. Game capture başlat (DLL inject)
console.log('🚀 Game capture baslatiliyor (DLL inject)...')
const startResult = screen.gameCaptureStart(TARGET_PROCESS, DLL_PATH)

if (!startResult.success) {
  console.error(`❌ Baslatilamadi: ${startResult.error}`)
  console.error(`   Once ${TARGET_PROCESS} uygulamasini ac:`)
  console.error(`   F:\\game-capture\\hook\\build\\Release\\test_app.exe`)
  process.exit(1)
}

console.log(`✅ Inject basarili, PID=${startResult.pid}`)
console.log('   DLL yukleniyor, hook kuruluyor (1 sn bekleniyor)...\n')

// 3. DLL'in hook'u kurması için kısa bekleme
setTimeout(() => {
  console.log('📸 Frame okuma testi (10 frame)...\n')
  
  let captured = 0
  let attempts = 0
  const startTime = Date.now()
  
  const captureInterval = setInterval(() => {
    attempts++
    const result = screen.gameCaptureRead(200)
    
    if (result.success) {
      captured++
      console.log(`Frame ${captured}/10: ${result.width}x${result.height}` +
                  ` (game frame#=${result.frameNumber})` +
                  ` capture=${result.captureMs.toFixed(2)}ms` +
                  ` copy=${result.copyMs.toFixed(2)}ms` +
                  ` size=${(result.size/1024/1024).toFixed(2)}MB`)
      
      if (captured >= 10) {
        clearInterval(captureInterval)
        const elapsed = (Date.now() - startTime) / 1000
        console.log(`\n✅ 10 frame yakalandi, sure=${elapsed.toFixed(2)}s`)
        
        // Durum kontrol
        const status = screen.gameCaptureStatus()
        console.log(`Status: running=${status.running} frameNumber=${status.frameNumber}`)
        
        // Durdur
        screen.gameCaptureStop()
        console.log('🛑 Game capture durduruldu')
        
        process.exit(0)
      }
    } else {
      if (attempts > 50) {
        console.error(`\n❌ 50 denemede frame alinmadi. Son hata: ${result.error}`)
        screen.gameCaptureStop()
        process.exit(1)
      }
    }
  }, 50)  // ~20 fps polling
  
}, 1000)

// Hata yakala
process.on('uncaughtException', (err) => {
  console.error('Hata:', err.message)
  try { screen.gameCaptureStop() } catch(e){}
  process.exit(1)
})
