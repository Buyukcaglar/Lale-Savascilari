# Oyun modülü dalgası (2026-11-21) – ortak kurallar / Game-module wave (2026-11-21) – shared rules

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Her özgün modül için bir tane olmak üzere beş iş paketi (work package, WP) paralel yürür:

| WP | Bloklar | Dosyalar | Sorumluluğu |
|---|---|---|---|
| WP13 | seg0546 (ISTANBUL.C main) + ovl3C2D (DEMOLAR.L) | src/game/istanbul.c/.h, src/game/demolar*.c/.h | `ist_main()` – SDL ana programının (host) çağırdığı oyun girişi |
| WP14 | ovl39E5 (CHARGEN.L) | src/game/chargen*.c/.h | karakter oluşturma, GRP.DAT |
| WP15 | ovl3FD1 (FRPOS.L) | src/game/frpos*.c/.h | dükkân/olay betiği yorumlayıcısı |
| WP16 | ovl303F (3DSYS.L) | src/game/s3d*.c/.h | keşif döngüsü, kamp, kaydet/yükle, seçenekler; ayrıca 3DQUICK testlerini (tests/py/test_q3dview*.py) ortak derlemeye katar |
| WP17 | ovl4595 (ISOSYS.L) | src/game/iso*.c/.h | izometrik savaş |

docs/DECOMPILING.md'ye ek olarak geçerli kurallar:

1. **Önce arayüzünü yayımla.** İlk adımlarında modül başlık dosyanı (`src/game/<module>.h`),
   diğer modüllerin çağırdığı her işlevin prototipiyle oluştur (bloğunun dışındaki çağıranlar
   için listelerdeki `called from:` satırlarına bak); adları analysis/names.tsv'ye göre ver.
   Bu adları/imzaları sonrasında sabit tut; birini değiştirmek zorunda kalırsan bunu raporunda
   belirt.
2. **Başka bir oyun modülünü çağırmak:** başlık dosyası varsa onu dahil et; yoksa prototipi
   kendi `.c` dosyanda `/* provided by <module> (WPnn) */` altında bildir. Kendi özel test
   DLL'in için eksik çağrılanları `tests/stubs/<wp>_stubs.c` içinde saplama (stub) olarak yaz
   (ve aynı yordamları öykünücü (emulator) tarafında da `e.stub` ile sapla).
3. **DGROUP'taki oyun durumu** (parti, karakterler, harita konumu, bayraklar…): bir makro
   tanımlamadan önce `grep -rn "0x<OFFSET>" src/` çalıştır. Birden çok oyun modülü aynı yapıyı
   paylaşıyorsa (ör. parti/karakter kayıtları), ona ihtiyaç duyan en küçük numaralı WP onu kendi
   başlığında tanımlar; diğerleri o başlığı dahil eder (henüz yoksa DG*() erişimcilerini
   `/* TODO: <name> from <header> */` yorumuyla kullanır).
4. UI pencere kayıtlarında saklanan **uzak (far) kod işaretçileri**
   `ui_register_far_fn(flat_seg, off, fn)` (src/engine/intrface.h) üzerinden geçer; bilinmeyen
   bir adres ölümcül hatadır. Senin işlevlerine çözümlenenleri kaydet (bindirme (overlay) thunk
   saplamaları 1670…16DA: build/ist_map.json "thunks" saplamayı → bindirme hedefine eşler).
5. **Klavye:** `ui_bioskey`, `ui_kbhit`, `ui_getch`, `ui_flush_keys` (intrface.h).
   Rastgele sayılar: `bc_random(n)` / `bc_rand()` (src/rtl/bc_rand.h) – Borland'ın argümanı
   iki kez değerlendiren `max()`/`min()` makrolarına dikkat et (PLAN §2).
6. **Kayan nokta:** başvuru (reference) x87'dir; src/game/x87ext.h içindeki 80 bitlik modeli
   yeniden kullan.
7. Büyük bloklar: adres sırasıyla çalış, alanlara göre birkaç .c dosyasına böl, ilerledikçe
   doğrula, bitene kadar devam et. Bağlamın (context) azalırsa: o anki işlevi bitir, her şeyin
   derlenip testleri geçmesini sağla ve neyin bittiğini/kaldığını tam olarak raporla.

## English

Five packages run in parallel, one per original module:

| WP | Blocks | Files | Owner of |
|---|---|---|---|
| WP13 | seg0546 (ISTANBUL.C main) + ovl3C2D (DEMOLAR.L) | src/game/istanbul.c/.h, src/game/demolar*.c/.h | `ist_main()` – the game entry the SDL host calls |
| WP14 | ovl39E5 (CHARGEN.L) | src/game/chargen*.c/.h | character generation, GRP.DAT |
| WP15 | ovl3FD1 (FRPOS.L) | src/game/frpos*.c/.h | shop/event script interpreter |
| WP16 | ovl303F (3DSYS.L) | src/game/s3d*.c/.h | exploration loop, camp, save/load, options; also merges the 3DQUICK tests (tests/py/test_q3dview*.py) into the shared build |
| WP17 | ovl4595 (ISOSYS.L) | src/game/iso*.c/.h | isometric battle |

Rules on top of docs/DECOMPILING.md:

1. **Publish your interface first.** Within your first steps, create your module header
   (`src/game/<module>.h`) with prototypes of every function other modules call
   (callers outside your block: see the listings' `called from:` lines), named after
   analysis/names.tsv. Keep those names/signatures stable afterwards; if you must change
   one, say so in your report.
2. **Calling another game module:** include its header if it exists; otherwise declare
   the prototype in your own `.c` under `/* provided by <module> (WPnn) */`. For your
   private test DLL, stub missing callees in `tests/stubs/<wp>_stubs.c` (and stub the
   same routines on the emulator side with `e.stub`).
3. **Game state in DGROUP** (party, characters, map position, flags…): before defining a
   macro, `grep -rn "0x<OFFSET>" src/`. If several game modules share a structure
   (e.g. the party/character records), the lowest-numbered WP that needs it defines it
   in its header; others include that header (and if it does not exist yet, use DG*()
   accessors with a `/* TODO: <name> from <header> */` comment).
4. **Far code pointers** stored in UI window records go through
   `ui_register_far_fn(flat_seg, off, fn)` (src/engine/intrface.h); an unknown address is
   fatal. Register the ones that resolve to your functions (overlay thunk stubs 1670…16DA:
   build/ist_map.json "thunks" maps stub → overlay target).
5. **Keyboard:** `ui_bioskey`, `ui_kbhit`, `ui_getch`, `ui_flush_keys` (intrface.h).
   Random numbers: `bc_random(n)` / `bc_rand()` (src/rtl/bc_rand.h) – mind Borland's
   double-evaluating `max()`/`min()` macros (PLAN §2).
6. **Floating point:** x87 reference; reuse the 80-bit model in src/game/x87ext.h.
7. Big blocks: work in address order, split into several .c files by area, verify as
   you go, keep going until done. If you run low on context: finish the current
   function, make everything compile and pass, and report exactly what is done/left.
