# Araçlar / Tools

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

| Betik | Amaç |
|---|---|
| `bootstrap.sh` | `orig/`, `extracted/` ve `build/` dizinlerini başvuru zip'inden yeniden oluşturur; `--tools` Ghidra'yı (Windows'ta ayrıca llvm-mingw ve SDL3'ü) indirir; `--ghidra` çözümleme hattını (pipeline) çalıştırır |
| `hostenv.sh` | Kabuk betiklerince içe alınır (source): ana sistemin (host) Python'u (varsa `.tools/venv`), JDK 21, başlatıcı soneki ve yerel yollar - Windows (Git Bash), macOS, Linux |
| `testlib.sh` | CMake olmadan özel karşılaştırmalı test (differential test) kitaplığı (`.dll` / `.dylib` / `.so`) |
| `package_windows.sh` | Dağıtılacak Windows programı: preset `windows`, hata ayıklama bilgisi çıkarılmış `Lale64.exe` + `SDL3.dll` -> `dist/Lale64-<version>-Windows.zip`. Paket, depo kökündeki `recomplauncher.json` dosyasını programın yanında taşır (RecompLauncher için; bkz. `docs/PROGRESS.md`, "Başlatıcı desteği") |
| `package_macos.sh` | Dağıtılacak macOS uygulaması: preset `macos-release` (evrensel, statik SDL3) -> `dist/Lale64-<version>-macOS.zip`. Paket, `recomplauncher.json` dosyasını `Lale64.app`'in yanında taşır |
| `../launcher/setup.ps1` | sp00nznet'in recomp-launcher'ının çalıştırdığı kurulum (`launcher/launcher.json` onu adlandırır; yalnızca Windows): oyun klasörünü denetler, en yeni Windows sürümünü GitHub'dan indirir, `play\` klasörüne açar ve `Lale Savascilari (recomp).cmd` dosyasını yazar. Sürüm dosyasını ad kalıbıyla (`$Asset`), programı adıyla (`$Program`) bulur |
| `genicon.py` | `assets/icon`'dan program simgesi: `src/gen/lale.ico` (Windows), `app_icon.c` (pencere), `lale.icns` (macOS uygulaması) |
| `flib.py` | FLiB `.CKD` arşivlerini listeler/açar (biçimi `docs/FORMATS.md` içinde) |
| `exeload.py` | Bir MZ + Borland FBOV çalıştırılabilir dosyasını düz, bağlanmış bir imaja (`build/<exe>_flat.bin`) ve bir haritaya (`build/<exe>_map.json`) açar. Bindirmeler (overlay) 0x2700 segmentinden başlayarak eşlenir; saplama thunk'ları `JMP FAR` olur |
| `segmodel.py` | Segment sınıflandırması + yer değiştirme kayıtlarından (relocation) kesin uzak giriş noktaları → `build/<exe>_segments.json` |
| `ghidra_patch.sh` | `ghidra/ia_rel16.patch`'i indirilen Ghidra'ya uygular ve x86 dilini yeniden derler. `ghidra_run.sh`'den önce **gereklidir**: değiştirilmemiş Ghidra gerçek kipteki yakın dallanmaları doğrusal 64 KB sınırlarında sarar |
| `ghidra_run.sh` | Başsız (headless) Ghidra: içe aktarma → `SetupIST` → çözümleme → `FixNear` → `FixFPU` → `FixSwitch` → `FixFPU` → `PruneFunctions` → `ApplyNames` → `ExportDecomp` → `ExportListing` → `branchcheck.py`; switch tabloları değiştiyse kendini ikinci kez çalıştırır (fpupatch.py onları okur) |
| `ghidra/ia_rel16.patch` | `rmseg` bağlam alanını (kod segmenti) ekler ve `rel8`/`rel16`'nın CPU'daki gibi segment ofsetinde sarmasını sağlar |
| `ghidra/SetupIST.java` | Segment başına bellek blokları, CS/DS/`rmseg` bağlamı, giriş işlevleri, bindirme thunk'ları |
| `ghidra/FixNear.java` | Komutları yabancı bir `rmseg` ile çözülmüş blokları yeniden ayrıştırır (re-disassemble) (Ghidra bağlamı uzak çağrılar boyunca taşır) |
| `ghidra/FixSwitch.java` | Borland switch tablolarını (yoğun indis, değer arama ve sınırsız satır içi indis biçimleri) çözer → case hedefleri, geri derleyicinin atlama tablosu geçersiz kılmaları, `build/<exe>_switches.tsv` |
| `ghidra/PruneFunctions.java` | DGROUP'taki işlevleri ve girişi gövdenin ortasında olan (fall-through), başvurulmayan otomatik işlevleri kaldırır; işaretçiyle ulaşılan yordamları korur |
| `ghidra/FixFPU.java` | Borland 8087 öykünücüsü `INT 34h–3Dh` → gerçek x87 işlem kodları |
| `ghidra/ApplyNames.java` | `analysis/names.tsv`'yi uygular (adlar, yorumlar) |
| `ghidra/ExportDecomp.java` | Blok başına taslak C + `functions.tsv` |
| `ghidra/ExportListing.java` | Blok başına açıklamalı `.asm` + FPU komutları geri yüklenmiş imaj `ist_fpu.bin` |
| `strrefs.py` | Anlık değerleri (immediate) DGROUP dizgileriyle eşleştirir → `analysis/strrefs_<exe>.tsv` |
| `strings.py` | Düz imajın bir aralığından cp857 dizgileri döker |
| `branchcheck.py` | Listelerdeki her göreli dallanmayı CPU anlamına göre doğrular (hattın geçiş kapısı) |
| `names_merge.py` | `analysis/names/*.tsv`'yi doğrulamayla `analysis/names.tsv` içinde birleştirir |
| `cmpview.py` | CMP imajlarını çözer, hepsini doğrular, PNG olarak çizer |
| `fmtcheck.py` | `docs/FORMATS.md`'deki KOR, RIX ve AMF biçimlerini her oyun dosyasına karşı denetler (`kor`, `rix`, `amf`, `all`) |
| `funcstatus.py` | `src/` içindeki `Original:` / `Verified:` yorumlarından portun işlev durumu (`docs/PROGRESS.md`'deki tablo; `--list` her yordam için bir satır) |
| `cddump.py` | Oyun CD'sinin bir redumper dökümünü çözümler (izler, ofsetler, karıştırılmış sektörler, ISO birimi) - bkz. `docs/DISC.md` |
| `chd.py` | Bağımsız CHD okuyucu: `info`, `verify` (zlib + ffmpeg'in FLAC çözücüsü; izleri redump.org ile karşılaştırır) - bkz. `docs/INSTALL.md` |
| `chdtool.c` | Portla birlikte derlenir (`chdtool`): `create`, `info`, `verify`, `ls`, `cat`, `extract`, `import` - oyunun kendi disk ve içe aktarma kodu komut satırında |
| `exediff.py` | `IST.EXE`'nin iki derlemesini yordam yordam karşılaştırır (`docs/EDITIONS.md`) |
| `export_videos.py` | Her GFL videosunu `<NAME>.mp4` olarak çıkarır (`VIDEO/` içindeki büyütülmüş kopyaların girdisi) |
| `cdsim.py` | CD imajını oluşturur/doğrular (redump 91232 düzeni; benzetilmiş veya gerçek) — bkz. `docs/DISC.md` |
| `dosbox/oracle.py` | Betikli DOSBox-X hata ayıklayıcı kâhini (oracle) (kesme noktaları, DGROUP/VGA/DAC dökümleri); öykünücü `$DOSBOX_X`, yoksa `C:\DOSBox-X\dosbox-x.exe` / Homebrew'un `dosbox-x`'i |
| `verify/build_hook.py` | `verify/hook.S`'yi (öykünülen PC içindeki girdi kancası) derler; Windows'ta llvm-mingw, başka yerlerde sistem clang'ı + yerleşik bir düz bağlama |

Adresler her zaman düz imajın segment uzayında `SSSS:OOOO` biçiminde yazılır. Bu uzay özgün
programın yüklemeye göreli segmentleriyle aynıdır; bindirmeler 2700 ve üzerinde yer alır.

## English

| Script | Purpose |
|---|---|
| `bootstrap.sh` | Recreate `orig/`, `extracted/` and `build/` from the reference zip; `--tools` downloads Ghidra (and on Windows llvm-mingw and SDL3); `--ghidra` runs the analysis pipeline |
| `hostenv.sh` | Sourced by the shell scripts: the host's Python (`.tools/venv` if present), JDK 21, launcher suffix and native paths - Windows (Git Bash), macOS, Linux |
| `testlib.sh` | Private differential-test library without CMake (`.dll` / `.dylib` / `.so`) |
| `package_windows.sh` | The Windows program to give away: preset `windows`, `Lale64.exe` without its debug information + `SDL3.dll` -> `dist/Lale64-<version>-Windows.zip`. The package carries `recomplauncher.json` from the repository root next to the program (for RecompLauncher; see `docs/PROGRESS.md`, "Launcher support") |
| `package_macos.sh` | The macOS application to give away: preset `macos-release` (universal, static SDL3) -> `dist/Lale64-<version>-macOS.zip`. The package carries `recomplauncher.json` beside `Lale64.app` |
| `../launcher/setup.ps1` | The setup that sp00nznet's recomp-launcher runs (`launcher/launcher.json` names it; Windows only): checks the game folder, downloads the newest Windows release from GitHub, unpacks it into `play\` and writes `Lale Savascilari (recomp).cmd`. It finds the release file by a name pattern (`$Asset`) and the program by name (`$Program`) |
| `genicon.py` | Program icon from `assets/icon`: `src/gen/lale.ico` (Windows), `app_icon.c` (window), `lale.icns` (macOS app) |
| `flib.py` | List/extract FLiB `.CKD` archives (format in `docs/FORMATS.md`) |
| `exeload.py` | Unpack an MZ + Borland FBOV executable into a flat, linked image (`build/<exe>_flat.bin`) and a map (`build/<exe>_map.json`). Overlays are mapped at segment 0x2700 and up; stub thunks become `JMP FAR` |
| `segmodel.py` | Segment classification + exact far entry points from relocations → `build/<exe>_segments.json` |
| `ghidra_patch.sh` | Apply `ghidra/ia_rel16.patch` to the downloaded Ghidra and rebuild the x86 language. **Required** before `ghidra_run.sh`: stock Ghidra wraps real-mode near branches at linear 64 KB boundaries |
| `ghidra_run.sh` | Headless Ghidra: import → `SetupIST` → analysis → `FixNear` → `FixFPU` → `FixSwitch` → `FixFPU` → `PruneFunctions` → `ApplyNames` → `ExportDecomp` → `ExportListing` → `branchcheck.py`; runs itself a second time when the switch tables changed (fpupatch.py reads them) |
| `ghidra/ia_rel16.patch` | Adds the context field `rmseg` (code segment) and makes `rel8`/`rel16` wrap at the segment offset like the CPU |
| `ghidra/SetupIST.java` | Memory blocks per segment, CS/DS/`rmseg` context, entry functions, overlay thunks |
| `ghidra/FixNear.java` | Re-disassemble blocks whose instructions were decoded with a foreign `rmseg` (Ghidra flows context across far calls) |
| `ghidra/FixSwitch.java` | Resolve Borland switch tables (dense index, value-search and unbounded inline-index forms) → case targets, decompiler jump-table overrides, `build/<exe>_switches.tsv` |
| `ghidra/PruneFunctions.java` | Remove functions in DGROUP and unreferenced auto functions whose entry is mid-body (fall-through); keeps pointer-reached routines |
| `ghidra/FixFPU.java` | Borland 8087-emulator `INT 34h–3Dh` → real x87 opcodes |
| `ghidra/ApplyNames.java` | Apply `analysis/names.tsv` (names, comments) |
| `ghidra/ExportDecomp.java` | Draft C per block + `functions.tsv` |
| `ghidra/ExportListing.java` | Annotated `.asm` per block + FPU-restored image `ist_fpu.bin` |
| `strrefs.py` | Match immediates against DGROUP strings → `analysis/strrefs_<exe>.tsv` |
| `strings.py` | Dump cp857 strings from a range of the flat image |
| `branchcheck.py` | Verify every relative branch in the listings against CPU semantics (pipeline gate) |
| `names_merge.py` | Merge `analysis/names/*.tsv` into `analysis/names.tsv` with validation |
| `cmpview.py` | Decode CMP images, verify all, render PNG |
| `fmtcheck.py` | Check the KOR, RIX and AMF formats of `docs/FORMATS.md` against every game file (`kor`, `rix`, `amf`, `all`) |
| `funcstatus.py` | Function status of the port from the `Original:` / `Verified:` comments of `src/` (the table in `docs/PROGRESS.md`; `--list` one line per routine) |
| `cddump.py` | Analyse a redumper dump of the game CD (tracks, offsets, scrambled sectors, the ISO volume) - see `docs/DISC.md` |
| `chd.py` | Independent CHD reader: `info`, `verify` (zlib + ffmpeg's FLAC decoder; compares the tracks with redump.org) - see `docs/INSTALL.md` |
| `chdtool.c` | Built with the port (`chdtool`): `create`, `info`, `verify`, `ls`, `cat`, `extract`, `import` - the game's own disc and import code on the command line |
| `exediff.py` | Compare the two builds of `IST.EXE` routine by routine (`docs/EDITIONS.md`) |
| `export_videos.py` | Every GFL video as `<NAME>.mp4` (the input for upscaled copies in `VIDEO/`) |
| `cdsim.py` | Build/verify the CD image (redump 91232 layout; simulated or real) — see `docs/DISC.md` |
| `dosbox/oracle.py` | Scripted DOSBox-X debugger oracle (breakpoints, DGROUP/VGA/DAC dumps); the emulator is `$DOSBOX_X`, else `C:\DOSBox-X\dosbox-x.exe` / Homebrew's `dosbox-x` |
| `verify/build_hook.py` | Assembles `verify/hook.S` (the input hook inside the emulated PC); llvm-mingw on Windows, the system clang + a built-in flat link elsewhere |

Addresses are always written `SSSS:OOOO` in the flat image's segment space. That space
equals the original load-relative segments; overlays sit at 2700 and above.
