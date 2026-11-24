# Lale Savaşçıları – geri derleme ve taşınabilir C sürümü / decompilation and portable C port

**Türkçe.** Bu depo, 1995–2005 tarihli DOS oyunu **İstanbul Efsaneleri – Bölüm I: Lale
Savaşçıları**'nın açıklamalı, taşınabilir C koduyla yeniden kurulmuş halidir. Amaç, özgün
`IST.EXE` ile birebir aynı davranıştır. Program Windows ve macOS'ta 64 bit olarak çalışır
(`Lale64`). Oyunun özgün dosyaları bu depoda ve programın içinde yer almaz; program onları
sizin elinizdeki kopyadan alır.

**English.** This is a reconstruction of the 1995–2005 DOS game **İstanbul Efsaneleri – Bölüm I:
Lale Savaşçıları** (the Turkish title means "Istanbul Legends – Part I: Tulip Warriors") as
annotated, portable C. The aim is 1:1 fidelity with the original `IST.EXE`. The program runs as
a 64-bit application on Windows and macOS (`Lale64`). The original game's files are neither in
this repository nor in the program; it takes them from your own copy.

## Belgeler / Documentation

**Türkçe.** Belgeler İngilizcedir.

* Oynamak için: oyun verisinin nereden alındığı (CD, disk kalıbı, 2005 sürümü) ve `DATA`,
  `SAVE`, `VIDEO` klasörleri: [`docs/INSTALL.md`](docs/INSTALL.md)
* Oyunun iki sürümü (1996 CD'si ve 2005 yeniden yayımı): [`docs/EDITIONS.md`](docs/EDITIONS.md)
* Oyun CD'si: [`docs/DISC.md`](docs/DISC.md)
* Strateji, modül haritası ve yol haritası: [`docs/PLAN.md`](docs/PLAN.md)
* Dosya biçimleri: [`docs/FORMATS.md`](docs/FORMATS.md)
* İlerleme durumu: [`docs/PROGRESS.md`](docs/PROGRESS.md)
* Çözümleme araçları: [`tools/README.md`](tools/README.md)
* macOS sürümü: [`docs/MACOS.md`](docs/MACOS.md)

**English.** The documents are in English.

* Playing: where the game's data comes from (CD, disc image, the 2005 release) and the folders
  `DATA`, `SAVE`, `VIDEO`: [`docs/INSTALL.md`](docs/INSTALL.md)
* The two editions of the game (the 1996 CD and the 2005 re-release):
  [`docs/EDITIONS.md`](docs/EDITIONS.md)
* The game CD: [`docs/DISC.md`](docs/DISC.md)
* Strategy, module map and roadmap: [`docs/PLAN.md`](docs/PLAN.md)
* File formats: [`docs/FORMATS.md`](docs/FORMATS.md)
* Progress: [`docs/PROGRESS.md`](docs/PROGRESS.md)
* Analysis tooling: [`tools/README.md`](tools/README.md)
* The macOS port: [`docs/MACOS.md`](docs/MACOS.md)

## Oyunu oynamak / Playing the game

**Türkçe.** Program ilk açılışta oyunu elinizdeki kopyadan alır ve çalıştırılabilir dosyanın
yanındaki klasörlere yerleştirir. Oyunun her iki sürümü de desteklenir.

| Elinizdeki | Ne olur | Oynanan sürüm |
|---|---|---|
| Sürücüdeki oyun CD'si (Windows) | `DATA/LALE.CHD` olarak saklanır | CD (1996) |
| CD'nin disk kalıbı: `.cue` + `.bin`, `.iso`, `.chd` | `DATA/LALE.CHD` olarak saklanır | CD (1996); `.iso` ses izleri içermediği için 2005 |
| 2005 sürümü: `.zip`, `.7z`, `.rar` arşivi ya da açılmış klasörü | Oyun dosyaları `DATA/` klasörüne kopyalanır | 2005 |

1. Kaynağı programın yanına koyun ya da `Lale64 <yol>` ile verin. Hiçbir şey bulunamazsa
   program sorar: *Choose File* (dosya seç), *Choose Folder* (klasör seç).
2. Aktarma bir kez yapılır; bundan sonra oyun `DATA/` klasöründen çalışır.
3. Oyunun yazdığı her şey (kayıtlı oyunlar `*.SAV`, `GRP.DAT`, `SES.CFG`), her iki sürümde de
   çalıştırılabilir dosyanın yanındaki `SAVE/` klasörüne gider.

CD sürümünde müzik CD'nin ses izlerinden, 2005 sürümünde `DEMO.AMF` modülünden çalınır; veri
dosyaları iki sürümde bayt bayt aynıdır. `--edition cd|2005` ile sürümü kendiniz seçebilirsiniz.
Ayrıntılar: [`docs/INSTALL.md`](docs/INSTALL.md), [`docs/EDITIONS.md`](docs/EDITIONS.md).

**English.** At its first start the program takes the game from your own copy and keeps it in
folders beside the executable. Both editions of the game are supported.

| What you have | What happens | Edition played |
|---|---|---|
| The game CD in a drive (Windows) | stored as `DATA/LALE.CHD` | CD (1996) |
| A disc image of the CD: `.cue` + `.bin`, `.iso`, `.chd` | stored as `DATA/LALE.CHD` | CD (1996); an `.iso` has no audio tracks and plays as 2005 |
| The 2005 release: a `.zip`, `.7z` or `.rar` archive, or its unpacked folder | the game's files are copied to `DATA/` | 2005 |

1. Put the source beside the program, or name it: `Lale64 <path>`. If nothing is found, the
   program asks: *Choose File*, *Choose Folder*.
2. The import happens once; from then on the game runs from `DATA/`.
3. Everything the game writes (saved games `*.SAV`, `GRP.DAT`, `SES.CFG`) goes to `SAVE/` beside
   the executable, in both editions.

The CD edition plays its music from the CD's audio tracks, the 2005 edition from the module
`DEMO.AMF`; the data files of the two are the same byte for byte. `--edition cd|2005` lets you
choose the edition yourself. Details: [`docs/INSTALL.md`](docs/INSTALL.md),
[`docs/EDITIONS.md`](docs/EDITIONS.md).

## 4K videolar / 4K videos

**Türkçe.** Oyunun 34 videosunun tamamı, 320x200'den 4K'ya büyütülmüş halde (H.264 MP4,
1,08 GB): **[ModDB'de Lale Savaşçıları 4K Video Paketi](https://www.moddb.com/mods/lale-savascilari-4k-video-pack/downloads/lale-savascilari-4k-video-pack)**
(MD5 `2ad684edf009f77449def40685f1bce3`).

1. `Lale-Savascilari-Video-4k.zip` dosyasını indirip açın.
2. `VIDEO` klasörünü programın yanına (çalıştırılabilir dosyanın bulunduğu klasöre) koyun.
3. Oyunu başlatın. Videolar 4K oynar. İstediğiniz anda **Ctrl+V** ile 4K ve orijinal video
   arasında geçiş yapabilirsiniz; her geçişte sağ üst köşede üç saniye boyunca `4K Video` ya da
   `Orijinal Video` yazar. Oyunun kendi sesi korunur; yalnızca görüntü değişir.

**English.** All 34 videos of the game, upscaled from 320x200 to 4K (H.264 MP4, 1.08 GB):
**[Lale Savascilari 4K Video Pack on ModDB](https://www.moddb.com/mods/lale-savascilari-4k-video-pack/downloads/lale-savascilari-4k-video-pack)**
(MD5 `2ad684edf009f77449def40685f1bce3`).

1. Download and unzip `Lale-Savascilari-Video-4k.zip`.
2. Put the `VIDEO` folder next to the program (the folder that holds the executable).
3. Start the game. The videos play in 4K. Press **Ctrl+V** at any time to switch between 4K
   and the original video; `4K Video` or `Orijinal Video` appears in the top right corner for
   three seconds at each switch. The game's own sound is kept; only the picture changes.

## Derleme (Windows) / Building (Windows)

**Türkçe.** Git Bash içinde:

```sh
tools/bootstrap.sh --tools          # başvuru zip'ini açar; Ghidra, llvm-mingw ve SDL3'ü indirir
tools/bootstrap.sh --ghidra         # isteğe bağlı: çözümlemeyi yeniden üretir (yaklaşık 3 dakika)
cmake --preset windows && cmake --build --preset windows
ctest --test-dir out/windows
```

Program `out/windows/Lale64.exe` olarak oluşur. Oyun verisi git'te izlenmez; yukarıda
anlatıldığı gibi ilk açılışta içe aktarılır. Geliştirme için `--game-dir orig`, oyunu özgün
dosyaların bulunduğu klasörden doğrudan çalıştırır.

**English.** In Git Bash:

```sh
tools/bootstrap.sh --tools          # extract the reference zip, download Ghidra/llvm-mingw/SDL3
tools/bootstrap.sh --ghidra         # optional: regenerate the analysis (about 3 minutes)
cmake --preset windows && cmake --build --preset windows
ctest --test-dir out/windows
```

The program is built as `out/windows/Lale64.exe`. The game data is not tracked in git; it is
imported at the first start as described above. For development, `--game-dir orig` runs the
game straight from a folder of the original's files.

## macOS (Apple Silicon)

**Türkçe.**

```sh
brew install cmake ninja sdl3                 # çözümleme / karşılaştırma için: + openjdk@21 dosbox-x
python3 -m venv .tools/venv && .tools/venv/bin/pip install capstone unicorn numpy pillow
tools/bootstrap.sh                            # başvuru zip'ini açar, src/gen'i üretir
cmake --preset macos && cmake --build --preset macos
out/macos/Lale64 --game-dir orig              # oyun, doğrudan özgün dosyalardan
open out/macos/Lale64.app                     # aynı program, uygulama olarak
tools/package_macos.sh                        # dağıtım için dist/Lale64-<sürüm>-macOS.zip
```

`tools/package_macos.sh`, `macos-release` ön ayarını derler: SDL3'ün statik bağlandığı, ad hoc
imzalı evrensel bir uygulama (arm64 + x86_64, macOS 11 ve sonrası).

Testler, çözümleme görüntüsüne ve DOSBox-X kayıtlarına ihtiyaç duyar; bunlar Mac'te de
Windows'taki gibi üretilir:

```sh
tools/bootstrap.sh --tools --ghidra           # Ghidra .tools/ içine, FPU'su geri yüklenmiş görüntü
.tools/venv/bin/python tools/dosbox/capture_boot.py
.tools/venv/bin/python tools/dosbox/capture_startup.py
ctest --test-dir out/macos
```

Yeni açılış ve içe aktarma (`DATA`, `SAVE`) ile 4K video çözücüsü henüz Mac'te derlenip
denenmedi. Mac'e özgü konular (tuşlar, pencere, Hakkında kutusu):
[`docs/MACOS.md`](docs/MACOS.md).

**English.**

```sh
brew install cmake ninja sdl3                 # + openjdk@21 dosbox-x for the analysis / oracle
python3 -m venv .tools/venv && .tools/venv/bin/pip install capstone unicorn numpy pillow
tools/bootstrap.sh                            # extract the reference zip, generate src/gen
cmake --preset macos && cmake --build --preset macos
out/macos/Lale64 --game-dir orig              # the game, straight from the original's files
open out/macos/Lale64.app                     # the same program as an application
tools/package_macos.sh                        # dist/Lale64-<version>-macOS.zip to give away
```

`tools/package_macos.sh` builds the preset `macos-release`: a universal app (arm64 +
x86_64, macOS 11 or later) with SDL3 linked statically, signed ad hoc.

The tests need the analysis image and the DOSBox-X captures, made on the Mac the same
way as on Windows:

```sh
tools/bootstrap.sh --tools --ghidra           # Ghidra into .tools/, FPU-restored image
.tools/venv/bin/python tools/dosbox/capture_boot.py
.tools/venv/bin/python tools/dosbox/capture_startup.py
ctest --test-dir out/macos
```

The new start-up and import (`DATA`, `SAVE`) and the 4K video decoder have not been built or run
on the Mac yet. What is particular to the Mac (keys, window, the About box):
[`docs/MACOS.md`](docs/MACOS.md).
