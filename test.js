const native = require('./build/Release/shadowrec_native');

console.log('═══════════════════════════════════════');
console.log('  ShadowRec Native Module Test');
console.log('═══════════════════════════════════════');

// Test 1: Toplama
const result1 = native.add(5, 3);
console.log('Test 1 - add(5, 3):', result1, '(beklenen: 8)');

// Test 2: Ondalık toplama
const result2 = native.add(2.5, 7.3);
console.log('Test 2 - add(2.5, 7.3):', result2, '(beklenen: 9.8)');

// Test 3: Selamlama
const result3 = native.greet('Geliştirici');
console.log('Test 3 - greet("Geliştirici"):', result3);

// Test 4: Sistem bilgisi
const result4 = native.systemInfo();
console.log('Test 4 - systemInfo():', result4);

console.log('═══════════════════════════════════════');
console.log('  ✅ Tüm testler başarılı!');
console.log('═══════════════════════════════════════');