# macOS portu / The macOS port

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Oyun kodu her ana sistemde (host) aynıdır: `src/game`, `src/engine`, `src/dsmi`,
`src/rtl` ve öykünülen PC (`src/platform/pc.c`, `vga.c`, `audio.c`, `cdrom.c`) ana sisteme
özgü tek bir satır içermez.  Mac'e özgü olanlar SDL3 ana programında
(`src/platform/sdl3_main.c`, `__APPLE__` / `HOST_APPLE` altında), Hakkında kutusunda
(`src/platform/about_macos.m`), derleme dosyalarında (`CMakeLists.txt`, `cmake/Info.plist.in`)
ve araçlardadır (`tools/hostenv.sh`).  Derleme komutları: [`README.md`](../README.md).

### Ne derlenir

| Çıktı | Ne |
|---|---|
| `out/macos/Lale64` | terminal, testler ve doğrulama betikleri için program (preset `macos`: arm64, Homebrew SDL3) |
| `out/macos/Lale64.app` | aynı kaynaklardan bir uygulama (hedef `lale_app`): simge `src/gen/lale.icns`, `Info.plist`, ad hoc imza |
| `out/macos/lale_testlib.dylib` | karşılaştırmalı testlerin (differential test) kitaplığı |
| `dist/Lale64-<version>-macOS.zip` | `tools/package_macos.sh`: preset `macos-release` - evrensel (arm64 + x86_64), macOS 11+, kaynaktan derlenip statik olarak bağlanan SDL3 3.4.18 (`LALE_VENDOR_SDL`); böylece uygulama macOS dışında hiçbir şeye bağımlı değildir |

`out/macos` içindeki uygulama ad hoc imzalıdır.  Dağıtılan uygulamayı `tools/package_macos.sh`
sahibin Developer ID'si ile imzalar (hardened runtime, güvenli zaman damgası, yetki/entitlement
yok), Apple'a onaylatır (notarytool, anahtarlık profili `civ64-notary`) ve onay belgesini ekler
(staple); `LALE_NOTARIZE=0` bu adımı atlar.  v1.0'ın macOS paketi 27 Kasım 2026'da bu şekilde
yeniden imzalandı (program baytları aynı, yalnızca imza değişti).

### Mac'ten Windows derlemesi

`tools/bootstrap.sh --win-cross` macOS için llvm-mingw'yi (Windows derlemesinin kullandığı
sürüm, 20261104) ve SDL3'ün mingw paketini `.tools/` içine indirir; ardından
`cmake --preset windows-cross && cmake --build --preset windows-cross` bu ağaçtan
`out/windows-cross/Lale64.exe`'yi derler.  Bu, ortak koddaki değişiklikler için bir derleme
denetimidir: program Mac'te çalıştırılmaz ve test edilmez.

### Oyun klasörü

**2026-11-24'ten beri** oyunun verileri ilk açılışta, her sistemde aynı biçimde içe aktarılır
([`INSTALL.md`](INSTALL.md)): çalıştırılabilir dosyanın yanındaki `DATA/`, `SAVE/` ve `VIDEO/`
içine - macOS'ta `Lale64.app`'i barındıran klasör - ya da o klasöre yazılamıyorsa
`~/Library/Application Support/Lale64` içine.  Yalnızca Mac'e özgü arama (`mac_find_game_dir`)
ve onun `game_dir = ...` satırı kaldırıldı; `--game-dir <folder>` oyunu doğrudan özgün
dosyalardan çalıştıran geliştirme seçeneği olarak kalır.  Sürücüdeki bir disk macOS'ta bağlı
birimi üzerinden (dosya olarak) okunur; disk imajları başka yerlerde olduğu gibi
`DATA/LALE.CHD` olur.

`~/Library/Application Support/Lale64/lale.ini` hâlâ uygulamanın seçenekler dosyasıdır;
`./lale.ini`'den önce okunur ve komut satırı ikisini de geçersiz kılar.  Betikli ve kare sayılı
çalıştırmalar (`--script`, `--frames`) hiçbir zaman iletişim kutusu açmaz.

**Mac'te henüz derlenmeyen veya çalıştırılmayanlar**: bu açılış ve içe aktarma, 4K video
çözücüsü `src/platform/hdvideo_avf.m` (AVFoundation) ve içe aktarmanın ilerleme paneli
`src/platform/ui_macos.m`; Hakkında kutusunun metinleri artık Türkçedir (`uitext.h`).  Bunlar
Windows'ta, belgelenmiş arayüzlere göre yazıldı.

### Klavye, fare, pencere

| Mac'te | Anlamı |
|---|---|
| sol Option | PC'nin Alt tuşu (Alt+tuş sözcükleri, BIOS shift bayrağı).  Sağ Option tuşu karakter yazmayı sürdürür |
| Cmd+Return, Cmd+F, Option+Return (Alt+Enter), yeşil düğme | tam ekran; görüntü 4:3 kalır, daha geniş bir ekranda siyah şeritlerle |
| Cmd+. | PC'nin Ctrl-Break'i (ayrıca Ctrl+F15 / Ctrl+F14, genişletilmiş Apple klavyesinin Pause / ScrollLock tuşları) |
| Cmd+Q, Cmd+H, Cmd+M ... | SDL'nin menü çubuğu; Command bileşimleri oyuna hiçbir zaman ulaşmaz |
| Lale64 > About Lale64 | Hakkında kutusu; kutu açıkken oyun duraklatılır |

F1-F10 işlev tuşlarıdır (dizüstü bilgisayarda "F1, F2 vb. tuşları standart işlev tuşları
olarak kullan" açık değilse Fn ile).  Pencere bir Retina yüzeyidir
(`SDL_WINDOW_HIGH_PIXEL_DENSITY`) ve Windows'taki gibi en yakın komşu (nearest-neighbour)
yöntemiyle ölçeklenir.

4:3 kenar şeritli gösterim (letterboxing) (`present_rect`) her ana sistemde SDL'nin esnetmeli
sunumunun yerini aldı: pencere kipinde hiçbir şeyi değiştirmez (pencere 4:3'te tutulur), tam
ekranda ise görüntünün oranını koruyan budur.

### Taşıma sırasında bulunanlar

| Nerede | Sorun | Çözüm |
|---|---|---|
| `src/game/iso_map.c` | `<stddef.h>` olmadan `NULL` (mingw başlıkları onu rastlantıyla sağlıyordu) | include eklendi |
| `src/platform/pc.c` `pc_fopen` | POSIX `fopen` bir dizini okumak için açar; DOS ve Windows reddeder.  Boş adla çağrılan `o5737` `read_cfg`, özgün programın almadığı bir akış ve tanıtıcı (handle) alıyordu (`test_cd5737.test_read_cfg`) | POSIX ana sistemlerinde dizinler reddedilir |
| `src/platform/pc.h` `pc_a000`, `src/rtl/bc_farheap.c` `bc_fh_arena` | 64 KB'lık ekran penceresinin ötesine taşan bir blit - `ui_format_text`'in x = -1'de çizdiği bir metin (sağa hizalı, dış hatlı, yerinden geniş) 65535 + y*320 ofsetine gider - diziden taşıp bağlayıcının (linker) arkasına koyduğu her şeyin üzerine yazıyordu: macOS'ta EMS sayfa çerçevesi (bozuk bir yazı tipi hücresi, `test_intrface.test_format_text`), Windows'ta şans eseri görünür hiçbir şey.  Özgün program B000'e yazar (ya da ekran yüzeyi yığındaysa (heap) video belleğine) | iki dizinin de arkasında bu yazmaları karşılayan bir pay var (`PC_A000_SLACK`, `BC_FH_ARENA_SLACK`): tanımlı ve her ana sistemde aynı |
| `src/engine/systems.c` `sys_blit`, `sys_blit_recolor` | `test_q3dview_gap.test_render_100` Mac'te başarısız oldu: geleneksel bellek bloğunun üstündeki bir hedef satır için yapılan ilk düzeltme, genişliğin 16'nın katı olduğunu varsayıyordu | aynı hata Windows'ta bulunup ana depoda (upstream) `cfcacce` ile düzeltildi (özgün programın 1 MB sarması); port bu düzeltmeyi kullanır |
| `src/rtl/bc_farheap.c` `bc_fh_arena` (ayrıca `pc_a000`, `pc_ems_frame`) | özgün programın bir paragraf sınırı, yerel derlemede yalnızca bağlayıcının seçimi sayesinde 16 baytlık bir sınırdı; macOS'ta `-fno-common` ile arena tek sayılı bir adreste başladı (`test_dsmi_infra.test_dds_drivers`) | `_Alignas(16)` |
| `tests/stubs/testhook.c`, `tests/py/lale.py` | `test_iso_d.test_door14_fatal`, portun test derlemesinin bir NULL eşlemesine çarpmasını bekler ve Windows ctypes'ın erişim ihlalini `OSError`'a çevirmesine dayanıyordu; macOS'ta sinyal tüm çalıştırmayı sonlandırdı | `th_guard` / `lale.guarded()`: çağrı bir SIGSEGV/SIGBUS işleyicisi altında çalışır ve hata her ana sistemde `OSError` olarak yükseltilir |
| `CMakeLists.txt` | clang arm64'te varsayılan olarak `a*b+c`'yi tek bir komutta birleştirir | her hedef için `-ffp-contract=off` (x87 modeli tamsayı kodudur, ama ana sistemin kayan noktası her yerde aynı biçimde yuvarlamalıdır) |
| `tests/py/lale.py`, `test_cd5737.py` | testler C işlevlerini, girişlerinin üzerine bir x86-64 atlaması yazarak yönlendirir (`VirtualProtect`) | arm64 atlaması (`ldr x16, .+8; br x16; .quad`), kod sayfaları `mach_vm_protect(VM_PROT_COPY)` ile yazılabilir yapılır; test kitaplığında 16 baytlık işlev hizalaması |
| `tests/py/test_farheap.py` | `main()` anlık görüntüsünün (snapshot) 9FFE paragrafı DOSBox-X'in kendi bayat verisidir ve Windows ile macOS derlemeleri arasında farklıdır (macOS'ta sıfırlar) | orada sıfır bulunan bir anlık görüntü kabul edilir; `bc_farheap_reset()` Windows yakalamasının baytlarını korur |
| `tools/dosbox/oracle.py` | macOS DOSBox-X hata ayıklayıcısı `MEMDUMPBIN` içinde mutlak yol kabul etmez | öykünücünün çalışma dizinine göreli yollar |
| `tools/verify/build_hook.py` | llvm-mingw'nin `ld.lld`'sine ihtiyaç duyuyordu | sistem clang'ı + Python'da düz bir bağlama (`link_flat`: tek bir `.text`, yalnızca `R_386_16`) |
| `tools/*.sh` | Windows yolları (JDK, `.bat` başlatıcıları, `cygpath`, `python`) | `tools/hostenv.sh` |

Mac'te kullanılamayan: Ghidra'nın geri derleyicisi (decompiler) (12.2 sürümünde `mac_arm_64`
yerel kitaplıkları yok; `ExportDecomp` taslak C yazmaz).  Listeler, `ist_fpu.bin` ve dal
denetimi üretilir ve testlerin ihtiyaç duyduğuyla aynıdır.

### macOS'ta doğrulama

Sayılar için bkz. [`PROGRESS.md`](PROGRESS.md) ("macOS port").

* `ctest --test-dir out/macos`: `vga`, `cdaudio` ve karşılaştırmalı test takımı - Unicorn'daki
  özgün x86 koduna karşı arm64 kitaplığı.
* Oyunun tamamı: [`VERIFY.md`](VERIFY.md) betikleriyle (`play3`, `play4`, `play4load`, `play5`)
  `tools/verify/pair.py` - Homebrew DOSBox-X 2026.10.01'deki özgün programa karşı
  `out/macos/Lale64`.
* Yayım uygulamasının her iki dilimi: aynı betik `arch -arm64` ve `arch -x86_64` (Rosetta)
  üzerinden; dökümler ve yazılan dosyalar bayt bayt karşılaştırıldı.
* Hakkında kutusu her iki görünümde de ekran dışında çizildi; uygulama, yanında oyun dosyaları
  varken LaunchServices (`open`) üzerinden başlatıldı.

Klavye başındaki bir kişi tarafından denetlenmeyenler: kulakla ses, tam ekran geçişi, klasör
iletişim kutusu, Hakkında kutusunun ekrandaki hâli.  Bunlar çalışmış kod yollarıyla sınanmıştır
(ses aygıtı açıldı, kareler sunuldu), ama kimse bakmadı ya da dinlemedi.

## English

The game code is the same on every host: `src/game`, `src/engine`, `src/dsmi`,
`src/rtl` and the emulated PC (`src/platform/pc.c`, `vga.c`, `audio.c`, `cdrom.c`)
have no host-specific line.  What is particular to the Mac is in the SDL3 host
(`src/platform/sdl3_main.c`, under `__APPLE__` / `HOST_APPLE`), the About box
(`src/platform/about_macos.m`), the build (`CMakeLists.txt`, `cmake/Info.plist.in`)
and the tooling (`tools/hostenv.sh`).  Build commands: [`README.md`](../README.md).

### What is built

| Output | What |
|---|---|
| `out/macos/Lale64` | the program, for the terminal, the tests and the verification scripts (preset `macos`: arm64, Homebrew SDL3) |
| `out/macos/Lale64.app` | the same sources as an application (target `lale_app`): icon `src/gen/lale.icns`, `Info.plist`, ad-hoc signature |
| `out/macos/lale_testlib.dylib` | the differential tests' library |
| `dist/Lale64-<version>-macOS.zip` | `tools/package_macos.sh`: preset `macos-release` - universal (arm64 + x86_64), macOS 11+, SDL3 3.4.18 built from source and linked statically (`LALE_VENDOR_SDL`), so the app depends on nothing outside macOS |

The app in `out/macos` is signed ad hoc.  The app given away is signed by
`tools/package_macos.sh` with the owner's Developer ID (hardened runtime, secure
timestamp, no entitlements), notarised (notarytool, keychain profile `civ64-notary`)
and stapled; `LALE_NOTARIZE=0` skips that.  v1.0's macOS package was re-signed this way
on 27 November 2026 (the program's bytes the same, only the signature changed).

### The Windows build from the Mac

`tools/bootstrap.sh --win-cross` downloads llvm-mingw for macOS (the release the
Windows build uses, 20261104) and SDL3's mingw package into `.tools/`;
`cmake --preset windows-cross && cmake --build --preset windows-cross` then builds
`out/windows-cross/Lale64.exe` from this tree.  It is a compile check for changes
to shared code: the program is not run or tested on the Mac.

### The game folder

**Since 2026-11-24** the game's data is imported at the first start, on every system
alike ([`INSTALL.md`](INSTALL.md)): into `DATA/`, `SAVE/` and `VIDEO/` beside the
executable - on macOS the folder that holds `Lale64.app` - or, when that folder cannot
be written to, into `~/Library/Application Support/Lale64`.  The Mac-only search
(`mac_find_game_dir`) and its `game_dir = ...` line are gone; `--game-dir <folder>`
remains as the development option that runs the game straight from the original's
files.  A disc in a drive is read through its mounted volume (as files) on macOS;
disc images become `DATA/LALE.CHD` as elsewhere.

`~/Library/Application Support/Lale64/lale.ini` is still the app's options file; it is
read before `./lale.ini`, and the command line overrides both.  Scripted and counted
runs (`--script`, `--frames`) never put up a dialog.

**Not yet built or run on the Mac**: this start-up and import, the 4K-video decoder
`src/platform/hdvideo_avf.m` (AVFoundation), and the import's progress panel
`src/platform/ui_macos.m`; the About box's texts are now Turkish (`uitext.h`).  They were written on Windows against the
documented interfaces.

### Keyboard, mouse, window

| On the Mac | Meaning |
|---|---|
| left Option | the PC's Alt (Alt+key words, the BIOS shift flag).  The right Option key keeps typing characters |
| Cmd+Return, Cmd+F, Option+Return (Alt+Enter), the green button | full screen; the picture stays 4:3, with black bars on a wider display |
| Cmd+. | the PC's Ctrl-Break (also Ctrl+F15 / Ctrl+F14, the Pause / ScrollLock keys of an extended Apple keyboard) |
| Cmd+Q, Cmd+H, Cmd+M ... | SDL's menu bar; Command combinations never reach the game |
| Lale64 > About Lale64 | the About box; the game is paused while it is open |

F1-F10 are the function keys (with Fn on a laptop, unless "use F1, F2 as standard
function keys" is on).  The window is a Retina surface (`SDL_WINDOW_HIGH_PIXEL_DENSITY`),
scaled with nearest-neighbour as on Windows.

The 4:3 letterboxing (`present_rect`) replaced SDL's stretch presentation on every
host: windowed it changes nothing (the window is held at 4:3), in full screen it is
what keeps the picture's shape.

### Found while porting

| Where | Problem | Fix |
|---|---|---|
| `src/game/iso_map.c` | `NULL` without `<stddef.h>` (the mingw headers happened to provide it) | include added |
| `src/platform/pc.c` `pc_fopen` | POSIX `fopen` opens a directory for reading; DOS and Windows refuse.  `o5737` `read_cfg` with an empty name got a stream and a handle the original does not (`test_cd5737.test_read_cfg`) | directories are refused on POSIX hosts |
| `src/platform/pc.h` `pc_a000`, `src/rtl/bc_farheap.c` `bc_fh_arena` | a blit that runs past the 64 KB screen window - a text drawn at x = -1 by `ui_format_text` (right-aligned, outlined, wider than its place) goes to offset 65535 + y*320 - left the array and overwrote whatever the linker put behind it: on macOS the EMS page frame (a corrupted font cell, `test_intrface.test_format_text`), on Windows by luck nothing visible.  The original writes into B000 (or, with the screen surface on the heap, into video memory) | both arrays have slack behind them that takes those writes (`PC_A000_SLACK`, `BC_FH_ARENA_SLACK`): defined, and the same on every host |
| `src/engine/systems.c` `sys_blit`, `sys_blit_recolor` | `test_q3dview_gap.test_render_100` failed on the Mac: the first fix for a destination row above a conventional block assumed a width that is a multiple of 16 | the same bug was found on Windows and fixed upstream in `cfcacce` (the original's 1 MB wrap); the port uses that fix |
| `src/rtl/bc_farheap.c` `bc_fh_arena` (also `pc_a000`, `pc_ems_frame`) | a paragraph boundary of the original was a 16-byte boundary natively only by the linker's choice; with `-fno-common` on macOS the arena started at an odd address (`test_dsmi_infra.test_dds_drivers`) | `_Alignas(16)` |
| `tests/stubs/testhook.c`, `tests/py/lale.py` | `test_iso_d.test_door14_fatal` expects the port's test build to run into a NULL mapping and relied on Windows ctypes turning the access violation into `OSError`; on macOS the signal ended the whole run | `th_guard` / `lale.guarded()`: the call runs under a SIGSEGV/SIGBUS handler and the fault is raised as `OSError` on every host |
| `CMakeLists.txt` | clang fuses `a*b+c` into one instruction on arm64 by default | `-ffp-contract=off` for every target (the x87 model is integer code, but host floating point must round alike everywhere) |
| `tests/py/lale.py`, `test_cd5737.py` | the tests redirect C functions by writing an x86-64 jump over their entry (`VirtualProtect`) | arm64 jump (`ldr x16, .+8; br x16; .quad`), code pages made writable through `mach_vm_protect(VM_PROT_COPY)`; 16-byte function alignment in the test library |
| `tests/py/test_farheap.py` | paragraph 9FFE of the `main()` snapshot is DOSBox-X's own stale data and differs between its Windows and macOS builds (zeros on macOS) | a snapshot with zeros there is accepted; `bc_farheap_reset()` keeps the Windows capture's bytes |
| `tools/dosbox/oracle.py` | the macOS DOSBox-X debugger takes no absolute path in `MEMDUMPBIN` | paths relative to the emulator's working directory |
| `tools/verify/build_hook.py` | needed llvm-mingw's `ld.lld` | system clang + a flat link in Python (`link_flat`: one `.text`, `R_386_16` only) |
| `tools/*.sh` | Windows paths (JDK, `.bat` launchers, `cygpath`, `python`) | `tools/hostenv.sh` |

Not available on the Mac: Ghidra's decompiler (the 12.2 release has no `mac_arm_64`
natives; `ExportDecomp` writes no draft C).  The listings, `ist_fpu.bin` and the
branch check are produced and identical to what the tests need.

### Verification on macOS

See [`PROGRESS.md`](PROGRESS.md) ("macOS port") for the numbers.

* `ctest --test-dir out/macos`: `vga`, `cdaudio` and the differential suite - the
  original x86 code in Unicorn against the arm64 library.
* Whole game: `tools/verify/pair.py` with the scripts of [`VERIFY.md`](VERIFY.md)
  (`play3`, `play4`, `play4load`, `play5`) - `out/macos/Lale64` against the original
  in the Homebrew DOSBox-X 2026.10.01.
* Both slices of the release app: the same script through `arch -arm64` and
  `arch -x86_64` (Rosetta), dumps and written files compared byte by byte.
* The About box was rendered off screen in both appearances; the app was started
  through LaunchServices (`open`) with the game files beside it.

Not checked by a person at the keyboard: sound by ear, the full-screen switch, the
folder dialog, the About box on screen.  They are exercised by code paths that ran
(audio device opened, frames presented), but nobody looked or listened.
