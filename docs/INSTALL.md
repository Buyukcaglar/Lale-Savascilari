# Oyunun verileri: DATA, SAVE ve VIDEO / The game's data: DATA, SAVE and VIDEO

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Bu uyarlama (port), orijinal oyunun hiçbir parçasını içermez. İlk açılışta oyunu, oyuncunun elinde
ne varsa oradan alır ve programın yanındaki klasörlerde saklar:

| Klasör | İçindekiler | Yazan |
|---|---|---|
| `DATA/` | içe aktarılan (imported) oyun: `LALE.CHD` (bir disk) ya da oyunun veri dosyaları (2005 sürümü) | bir kez, içe aktarma sırasında |
| `SAVE/` | oyunun kendisinin yazdığı her şey: kayıtlı oyunlar (`*.SAV`), `GRP.DAT`, `SES.CFG` | oyun, her baskıda (edition) |
| `VIDEO/` | isteğe bağlı: videoların daha kaliteli bir kopyası (`INTRO.mp4` …), bkz. [4K videolar](#4k-videolar) | oyuncu |

"Programın yanı", `Lale64.exe` dosyasının bulunduğu klasör demektir; macOS'ta ise `Lale64.app`
uygulamasını içeren klasör. Bu klasöre yazılamıyorsa onun yerine sistemin bu program için ayırdığı,
kullanıcıya özel klasör kullanılır. `--base-dir <folder>` (`lale.ini` içinde `base_dir`) ile başka
bir yer belirtilebilir.

### Oyuncunun verebileceği kaynaklar

| Kaynak | Neye dönüşür | Oynanan baskı |
|---|---|---|
| sürücüdeki oyun CD'si (Windows) | `DATA/LALE.CHD` | CD (1996) |
| bir disk kalıbı (disc image): `.cue` + `.bin`, `.iso`, `.chd` ya da bunlardan birini içeren bir klasör (bir redumper döküm (dump) klasörü olduğu gibi verilebilir) | `DATA/LALE.CHD` | CD (1996); `.iso` dosyasında ses izleri yoktur, 2005 baskısı olarak oynanır |
| Level dergisinin Ocak 1998 kapak CD'si (sürücüde ya da disk kalıbı olarak; bkz. [DISC.md](DISC.md#level-dergisinin-cdsi-_referencescd_level)) | `DATA/LALE.CHD` | 2005, çünkü diskte ses izi yoktur; `edition = cd` ile diskteki 1996 programı, giriş ve menü müziği olmadan |
| 2005 sürümü: bir `.zip`, `.7z` ya da `.rar` arşivi veya arşivin açıldığı klasör | veri dosyaları `DATA/` içine, kayıtlı oyunlar ve ayarlar `SAVE/` içine | 2005 |

İki sürüm yalnızca programda ayrılır (bkz. [EDITIONS.md](EDITIONS.md)); veri dosyaları bayt bayt
aynıdır, bu yüzden hangi kaynağı verirseniz verin oyunun tamamı elde edilir. `--edition cd|2005`
bu seçimi geçersiz kılar: 2005 baskısı tek başına `LALE.CHD` ile de çalışır; CD baskısı ise bir
disk ister (ses izleri olmayan bir diskte giriş, menü ve hikâye müziksiz kalır).

Kaynak şu sırayla aranır:

1. komut satırında belirtilmişse (`Lale64 <path>` ya da kaynağı programın simgesinin üzerine
   sürükleyip bırakın) veya `lale.ini` içinde `source` olarak yazılmışsa;
2. daha önce içe aktarılmışsa: `DATA/` içinde `LALE.CHD` ya da oyunun dosyaları varsa;
3. programın yanında, temel klasörde (base folder) ya da çalışma dizininde duruyorsa (önce
   kalıplar, sonra oyunu içeren klasörler, en son arşivler);
4. içindeki diskin etiketi `ISTANBUL` olan ya da kök klasöründe oyunu taşıyan (`IST.EXE`,
   `0GFL.CKD`, `3D.CKD`: Level dergisinin CD'si) bir CD sürücüsü;
5. hiçbiri yoksa program sorar: *Dosya Seç…* (bir dosya seçmek için), *Klasör Seç…* (bir klasör
   seçmek için), *Çıkış* (programdan çıkmak için).

İçe aktarma sırasında bir ilerleme penceresi görünür; Esc ile iptal edilebilir ve geride hiçbir
şey bırakmaz. Oyun olmayan bir kaynak, nedeni söylenerek reddedilir (`3D.CKD` içermeyen bir disk,
eksik bir klasör, hasarlı bir arşiv ya da CHD).

#### Disk ya da disk kalıbı → `DATA/LALE.CHD`

Disk bir CHD olarak saklanır (MAME'in sıkıştırılmış disk biçimi, sürüm 5, `chdman createcd`
komutunun yazdığı düzen): veri sektörleri hata düzeltme baytları çıkarılıp deflate ile
sıkıştırılarak, ses ise FLAC olarak. Gerçek disk (331 MB) 208 MB'a iner. Oyun bundan sonra her
şeyi bu dosyadan okur: veri izini, geri derlenmiş (decompiled) MSCDEX istemcisi üzerinden D:
sürücüsü olarak, ses izlerini de CD müziği olarak. Okuyucu ve yazıcı uyarlamanın kendi kodudur
(`src/platform/chd.c`, `zdeflate.c`, `flac.c`, `cdsector.c`); sonucu `chdman` da okuyabilir, ve
`chdman` ile cdzl ve cdfl kodekleriyle yazılmış CHD'ler de kaynak olarak kabul edilir.

* **cue/bin**: tek bir `.bin` ya da her iz için bir tane; `MODE1/2352`, `MODE1/2048` ve `AUDIO`
  izleri. `.bin` dosyası verilirse yanındaki `.cue` kendiliğinden bulunur.
* **bu diskin redumper dökümleri**: diskin veri izi 48 örnek (sample) erken basılmıştır
  (mastered), redumper'ın bölme işlemi de 1. izin son 150 sektörünü karıştırılmış (scrambled),
  sesi ise 192 bayt erken bırakır. İçe aktarıcı bunu fark eder (veri izinin sonunda eşitleme
  (sync) deseni yoktur ama birkaç bayt ileride vardır, ve karıştırma çözülünce geçerli sektörler
  çıkar) ve düzeltilmiş konumdan okur; böylece CHD, redump.org düzenindeki bir kalıptan elde
  edilenle aynı olur.
* **`.iso`**: tek bir veri izi; kullanıcı verisi (user data) olarak saklanır.
* **`.chd`**: kendi SHA-1 toplamlarıyla denetlenir ve kopyalanır.
* **bir sürücü** (Windows): içindekiler tablosu (table of contents), kullanıcı verisi olarak
  okunup ham (raw) sektörlere yeniden kurulan veri izi ve CD-DA olarak okunan ses izleri. macOS'ta
  ve başka sistemlerde disk bunun yerine bağlanmış (mounted) biriminin dosyaları olarak okunur (bu
  da 2005 baskısını verir).

#### 2005 sürümü → `DATA/` ve `SAVE/`

`DATA/` içine kopyalananlar: `*.CKD`, `*.DSD`, `TRDOS.DAT`, `GRPHAZIR.DAT`, `BOSCH.KOR`,
`SONPAL.PAL`. `SAVE/` içine, dosya orada zaten yoksa kopyalananlar: `GRP.DAT`, `SES.CFG`, `*.SAV`.
Geri kalan hiçbir şeye (DOS programları, bunların PIF'leri ve metin dosyaları) gerek yoktur. Oyun
eksiksiz olmalıdır: sekiz `.CKD` dosyası, `TRDOS.DAT` ve `GRPHAZIR.DAT`.

ZIP arşivlerini uyarlama kendisi okur (sıkıştırılmamış (stored) ve deflate ile sıkıştırılmış
üyeler). 7Z ve RAR arşivleri, ve uyarlamanın okuyamadığı ZIP'ler, sistemdeki bir programla açılır:
`tar` (bsdtar; Windows 10/11 ve macOS ile birlikte gelir), o yoksa 7-Zip, o da yoksa `unar`.

#### `SAVE/` klasörü

Oyun dosyalarını önce `SAVE/` içinde, sonra `DATA/` içinde, en son diskte arar; yazdığı her şey
`SAVE/` klasörüne gider (şimdiye dek yalnızca `DATA/` içinde ya da diskte bulunan bir dosya önce
oraya kopyalanır). Bu, iki baskı için de geçerlidir. CD'de `SES.CFG` yoktur (onu CD'nin kurulum
programı yazıyordu); bu yüzden diskten yapılan bir kurulum, bir `SES.CFG` bulunmadıkça 2005
sürümüyle gelen ayarları alır (Sound Blaster 16, 220h, IRQ 7, DMA 5, 22050 Hz).

### 4K videolar

`tools/export_videos.py`, oyunun 35 videosunun her birini `<NAME>.mp4` olarak yazar (320×200,
saniyede 10 kare). Bunların büyütülmüş (upscaled) kopyaları aynı adlarla `VIDEO/` klasörüne
konursa, orijinal görüntünün yerine onlar gösterilir:

* Oyunun kendi video oynatıcısı arka planda hiç değişmeden çalışır: videosunu okur ve çözer,
  zamanlamasını tutar, sesini çalar, tuşları alır. Yalnızca penceredeki görüntü farklıdır: oyunun
  *n*. karesi, dosyanın *n*/10. saniyesindeki kareyi gösterir. Dosyanın kendi ses izi kullanılmaz.
* **Ctrl+V** (Windows ve macOS; macOS'ta ⌘V de) ikisi arasında istediğiniz an geçiş yapar, bir
  videonun ortasında bile. Her geçişte sağ üst köşede üç saniye boyunca `4K Video` ya da
  `Orijinal Video` yazısı belirir. `lale.ini` içinde `video = original` (`--video original`)
  oyunu orijinal videolarla başlatır.
* `VIDEO/` içinde dosya yoksa bu özellik kapalıdır ve Ctrl+V oyuna iletilir. Dosyası olmayan bir
  video orijinal haliyle oynar.
* Adlar: `INTRO.mp4` (ayrıca `.m4v`, `.mov`), büyük/küçük harf fark etmez; adın sonuna `_ - . (`
  ya da bir boşlukla ayrılarak eklenen bir ek kabul edilir (`INTRO_4k.mp4`). Boyut serbesttir;
  görüntü oyunun 4:3 alanının üzerine çizilir. 16:10'dan geniş bir karenin yanlarının dolgu
  olduğu varsayılır ve ortası kullanılır.
* Videoyu çözme (decoding) işini sistem yapar: Windows'ta Media Foundation, macOS'ta
  AVFoundation. H.264 hiçbir şey kurmadan oynar. Bir dosyayı açıp ilk karesini çözmek 0.1-0.2 s
  sürer (2882×2160 H.264 ile ölçüldü); bu süre, oyun videoyu başlatmadan önce harcanır. Bir
  videonun ortasında 4K'ya geçilirse, o andan önceki anahtar kareden (key frame) başlayarak
  çözülür ve o ana kadar orijinal görüntü gösterilir: birkaç saniyede bir anahtar kare varsa
  0.2-0.4 s.

### Pencere ve tuşlar

* **Alt+Enter**: tam ekran ile pencere arasında geçiş (her sistemde; macOS'ta ⌘Return ve ⌘F de).
  Görüntü 4:3 kalır; daha geniş bir ekranda yanlarda siyah şeritler olur. Alt+Enter yalnızca
  programındır, oyuna iletilmez (oyunun kendisi Alt+Enter kullanmaz).
* **Ctrl+V**: 4K ve orijinal videolar arasında geçiş (bkz. [4K videolar](#4k-videolar)).
* **Alt+Boşluk** (Windows): pencerenin menüsü, içinde *Hakkında…*.

### Programın iletişim kutuları

Uyarlamanın oyuncuya kendisinin gösterdiği her şey (yukarıdaki soru, içe aktarmanın ilerlemesi,
hata iletileri, Hakkında kutusu; bu kutu Windows'ta pencerenin sistem menüsünde, macOS'ta
uygulama menüsündedir) Türkçedir ve sistemin açık ya da koyu moduna uyar; bir iletişim kutusu
açıkken mod değişse bile. Metinler tek bir yerde durur: `src/platform/uitext.h`.

* **Windows** (`src/platform/ui_win32.c`): uyarlamanın kendi iletişim kutuları, Windows 11'in
  kendi kutularını çizdiği gibi çizilir (gövde, düğmelerin bulunduğu alt bölüm, sistemin ileti
  yazı tipi ve vurgu rengi, DPI'ya duyarlı), açık ya da koyu bir başlık çubuğuyla; oyunun
  penceresi de buna uyan başlık çubuğunu alır. Dosya ve klasör seçicileri Windows'un kendisinindir:
  başlıkları ve düğmeleri Türkçe ayarlanır, içlerindeki geri kalan her şey Windows'un dilindedir.
  Hakkında kutusundaki lisanslar Türkçe gösterilir; bağlayıcı olan İngilizce asılları
  `docs/LICENSES.txt` içindedir ve paketlerde `LICENSES.txt` olarak bulunur.
* **macOS**: soru ve hatalar sistem uyarılarıdır (`NSAlert`), ilerleme ise bir sistem paneli
  (`src/platform/ui_macos.m`, henüz bir Mac'te derlenmedi).
* Konsoldaki tanılama günlüğü İngilizce kalır; oyunun kendi metinleri orijinaldeki gibidir.

Test kancaları (test hooks) (Windows): `LALE_UI_SHOT=<prefix>` her iletişim kutusunun resmini
kaydeder ve kutuyu yanıtlar (`LALE_UI_ANSWER=<button>`, varsayılanı Esc düğmesi);
`LALE_UI_THEME=light|dark` sistemin modunu geçersiz kılar; bunlara ek olarak `LALE_UI_ABOUT=1`
verilirse Hakkında kutusunun da resmi çekilir.

### Seçenekler

| `lale.ini` | Komut satırı | Varsayılan | |
|---|---|---|---|
| `source` | `<path>` ya da `--source <path>` | | bu açılışta neyin içe aktarılacağı |
| `edition` | `--edition auto\|cd\|2005` | `auto` | `DATA/LALE.CHD` ses izleri içeriyorsa CD baskısı |
| `base_dir` | `--base-dir <folder>` | programın yanı | `DATA`, `SAVE` ve `VIDEO` klasörlerinin yeri |
| `video` | `--video 4k\|original` | `4k` | `VIDEO/` içinde dosya varsa videoların hangi halde başlayacağı |

`game_dir` ve `cd_image` geliştirme seçenekleridir (oyun doğrudan `orig/` gibi bir klasörden,
`DATA`/`SAVE` olmadan; `cd_image` orada `edition = cd` için kullanılan disktir). `cd_music`,
`edition = cd` seçeneğinin eski adıdır.

### Araçlar

* `chdtool` (uyarlamayla birlikte derlenir): `create <cue> <chd>`, `info`, `verify`, `ls`, `cat`,
  `extract`, `import <base> <source>`; oyunun kullandığı kodun aynısı.
* `tools/chd.py`: zlib ve ffmpeg'in FLAC çözücüsü üzerine kurulu, bağımsız bir okuyucu (`info`,
  `verify`); izleri redump.org'un toplamlarıyla da karşılaştırır.

### Doğrulama

`tests/test_codecs.c` (ctest `codecs`) her modülü kendi ürettiği veriler üzerinde sınar: deflate,
FLAC, Mode 1 sektörleri, ISO 9660, CHD yazma ve okuma, ZIP, ve içe aktarıcıyı bir klasör, bir ZIP,
cue/bin, bir ISO, bir CHD ve yedi tür kullanılamaz kaynakla.

İki referans kaynakla yapılan çalıştırmalar (2026-11-24, Windows 11):

| Kaynak | Sonuç |
|---|---|
| CD'nin redumper döküm klasörü (`lale.cue`, 3 `.bin`) | `LALE.CHD`, 208,112,607 bayt, SHA-1 `18e0f0f394bcb6948be1c8c279c9d54a39600608`; 13 s'de yazıldı, 5 s'de doğrulandı |
| aynı disk, redump.org düzeninde bir kalıp olarak | birebir aynı CHD |
| o CHD'nin geri okunması | üç izin üçü de `.bin` dosyalarına eşit; `tools/chd.py verify` (zlib, ffmpeg) de aynı sonucu verir; 1. ve 2. izler redump.org 91232 ile eşleşir |
| CHD'den okunan `IST.EXE`, `0GFL.CKD`, `SOUNDS.CKD` | dosyalarla birebir aynı |
| ZIP, 7Z, RAR ve klasör olarak 2005 sürümü | `DATA/` içinde aynı 16 dosya ve `SAVE/` içinde `GRP.DAT`, `SAKLA001.SAV`, `SES.CFG`, orijinallerine eşit |
| `.iso` olarak veri izi; var olan bir `.chd` | içe aktarıldı; geri okunan dosyalar aynı |
| sürücü olarak bağlanmış bir ISO (Windows) | sürücü yolu üzerinden içe aktarıldı (içindekiler tablosu, veri sektörleri) |
| iki baskı da kendi içe aktarımlarından başlatıldı | 2005, ZIP'ten, adında Türkçe harfler ve bir boşluk bulunan bir temel klasörde; CD, redumper klasöründen: CD denetimleri geçer, giriş videosu CHD'den oynar |

Doğrulanmayanlar:

* **Fiziksel bir sürücüden ses**: geliştirme makinesinde optik sürücü yok. Bir sürücünün veri
  yolu, bağlanmış bir ISO ile çalıştırıldı.
* Bu dökümün **3. izinin** son 192 baytı eksiktir (48 örnek: sürücü kapanış alanına (lead-out)
  kadar okuyamadı), bu yüzden redump.org ile eşleşmeyen tek iz odur; eksik baytlar sessizlik
  olarak saklanır.
* **macOS**: yeni açılış, içe aktarma ve `hdvideo_avf.m` henüz orada derlenmedi ve
  çalıştırılmadı.

## English

The port ships without any part of the original game. At its first start it takes the game from
whatever the player has and keeps it in folders beside the executable:

| Folder | Holds | Written |
|---|---|---|
| `DATA/` | the game as imported: `LALE.CHD` (a disc) or the game's data files (the 2005 release) | once, by the import |
| `SAVE/` | everything the game itself writes: saved games (`*.SAV`), `GRP.DAT`, `SES.CFG` | by the game, in every edition |
| `VIDEO/` | optional: the videos in a better copy (`INTRO.mp4` …), see [4K videos](#4k-videos) | by the player |

"Beside the executable" is the folder of `Lale64.exe`; on macOS the folder that holds
`Lale64.app`. If that folder cannot be written to, the system's per-user folder for the program
is used instead. `--base-dir <folder>` (`base_dir` in `lale.ini`) names another place.

### What the player can provide

| Source | Becomes | Edition played |
|---|---|---|
| the game CD in a drive (Windows) | `DATA/LALE.CHD` | CD (1996) |
| a disc image: `.cue` + `.bin`, `.iso`, `.chd`, or a folder holding one (a redumper dump folder as it is) | `DATA/LALE.CHD` | CD (1996); an `.iso` has no audio tracks and plays as 2005 |
| the cover CD of the Level magazine, January 1998 (in a drive or as a disc image; see [DISC.md](DISC.md#the-level-magazine-cd-_referencescd_level)) | `DATA/LALE.CHD` | 2005, as the disc has no audio tracks; `edition = cd` gives the disc's 1996 program, without intro and menu music |
| the 2005 release: a `.zip`, `.7z` or `.rar`, or the folder it was unpacked to | the data files in `DATA/`, saved games and settings in `SAVE/` | 2005 |

The two releases differ in the program only (see [EDITIONS.md](EDITIONS.md)); their data files
are the same byte for byte, so either source gives the complete game. `--edition cd|2005`
overrides the choice: the 2005 edition also runs from `LALE.CHD` alone; the CD edition needs a
disc (on one without audio tracks the intro, menu and story have no music).

How the source is found, in this order:

1. named on the command line (`Lale64 <path>`, or drag it onto the program) or as `source` in
   `lale.ini`;
2. already imported: `DATA/` holds `LALE.CHD` or the files;
3. lying beside the program, in the base folder or in the working directory (images first, then
   folders with the game, then archives);
4. a CD drive whose disc is labelled `ISTANBUL` or holds the game in its root folder (`IST.EXE`,
   `0GFL.CKD`, `3D.CKD`: the Level magazine CD);
5. the program asks: *Dosya Seç…* (choose a file), *Klasör Seç…* (choose a folder), *Çıkış* (quit).

A progress window shows the import; Esc cancels it and leaves nothing behind. A source that is
not the game is refused with the reason (a disc without `3D.CKD`, an incomplete folder, a
damaged archive or CHD).

#### A disc or disc image → `DATA/LALE.CHD`

The disc is stored as a CHD (MAME's compressed disc format, version 5, the layout `chdman
createcd` writes): data sectors with their error-correction bytes removed and deflated, audio as
FLAC. The real disc (331 MB) becomes 208 MB. The game then reads everything from that file: the
data track as drive D: through the decompiled MSCDEX client, the audio tracks as CD music.
Reader and writer are the port's own code (`src/platform/chd.c`, `zdeflate.c`, `flac.c`,
`cdsector.c`); `chdman` reads the result, and CHDs written by `chdman` with the cdzl and cdfl
codecs are accepted as a source.

* **cue/bin**: one `.bin` or one per track, `MODE1/2352`, `MODE1/2048` and `AUDIO` tracks. Naming
  the `.bin` finds the `.cue` beside it.
* **redumper dumps of this disc**: the disc's data track was mastered 48 samples early, and
  redumper's split leaves the last 150 sectors of track 1 scrambled and the audio 192 bytes
  early. The importer notices this (the end of the data track has no sync pattern, but has one
  a few bytes on, and descrambles to valid sectors) and reads at the corrected position, so the
  CHD is the same as from an image in redump.org's layout.
* **`.iso`**: one data track, stored as user data.
* **`.chd`**: checked against its own SHA-1 sums and copied.
* **a drive** (Windows): the table of contents, the data track read as user data and rebuilt to
  raw sectors, the audio tracks read as CD-DA. On macOS and elsewhere a disc is read as the
  files of its mounted volume instead (which gives the 2005 edition).

#### The 2005 release → `DATA/` and `SAVE/`

Copied to `DATA/`: `*.CKD`, `*.DSD`, `TRDOS.DAT`, `GRPHAZIR.DAT`, `BOSCH.KOR`, `SONPAL.PAL`.
Copied to `SAVE/` unless the file is already there: `GRP.DAT`, `SES.CFG`, `*.SAV`. Everything
else (the DOS programs, their PIFs and texts) is not needed. The game must be complete: the
eight `.CKD` files, `TRDOS.DAT` and `GRPHAZIR.DAT`.

ZIP archives are read by the port itself (stored and deflated members). 7Z and RAR archives -
and ZIPs it cannot read - are unpacked by a program of the system: `tar` (bsdtar, part of
Windows 10/11 and macOS), else 7-Zip, else `unar`.

#### `SAVE/`

The game opens its files in `SAVE/` first, then in `DATA/`, then on the disc; whatever it writes
goes to `SAVE/` (a file that so far only exists in `DATA/` or on the disc is copied there first).
This holds for both editions. The CD carries no `SES.CFG` - its installer wrote one - so a disc
install gets the settings the 2005 release ships with (Sound Blaster 16, 220h, IRQ 7, DMA 5,
22050 Hz) unless one is found.

### 4K videos

`tools/export_videos.py` writes each of the game's 35 videos as `<NAME>.mp4` (320×200, 10 frames
a second). Upscaled copies of those, put into `VIDEO/` under the same names, are shown in place
of the original picture:

* The game's own player runs unchanged underneath - it reads and decodes its video, keeps its
  clock, plays its sound, takes its keys. Only the picture in the window differs: frame *n* of
  the game shows the frame at *n*/10 s of the file. The file's sound track is not used.
* **Ctrl+V** (Windows and macOS; on macOS also ⌘V) switches between the two at any moment, also
  in the middle of a video. `4K Video` or `Orijinal Video` appears in the top right corner for
  three seconds at each switch. `video = original` in `lale.ini` (`--video original`) starts
  with the originals.
* Without files in `VIDEO/` the feature is off and Ctrl+V goes to the game. A video without a
  file plays as the original.
* Names: `INTRO.mp4` (also `.m4v`, `.mov`), case ignored; a suffix set off by `_ - . (` or a blank
  is accepted (`INTRO_4k.mp4`). Any size; the picture is drawn over the game's 4:3 area. A frame
  wider than 16:10 is taken to be padded at the sides and its middle is used.
* Decoding is the system's: Media Foundation on Windows, AVFoundation on macOS. H.264 plays
  without anything installed. Opening a file and decoding its first frame takes 0.1-0.2 s
  (measured with 2882×2160 H.264), spent before the game starts the video. A switch to 4K in the
  middle of a video decodes from the key frame before that moment and shows the original until
  then: 0.2-0.4 s with a key frame every few seconds.

### Window and keys

* **Alt+Enter**: full screen <-> window (every system; on macOS also ⌘Return and ⌘F). The
  picture stays 4:3, with black bars at the sides of a wider display. Alt+Enter belongs to the
  program and does not reach the game (the game itself has no use for Alt+Enter).
* **Ctrl+V**: 4K <-> original videos (see [4K videos](#4k-videos)).
* **Alt+Space** (Windows): the window's menu, with *Hakkında…* (About).

### The program's dialogs

Everything the port itself shows the player - the question above, the import's progress, error
messages, the About box (in the window's system menu on Windows, the application menu on macOS)
- is in Turkish, and follows the system's light or dark mode, also when it changes while a
dialog is open. The texts are in one place, `src/platform/uitext.h`.

* **Windows** (`src/platform/ui_win32.c`): dialogs of the port's own, drawn the way Windows 11
  draws its own (body, footer with the buttons, the system's message font and accent colour,
  DPI-aware), with a light or dark title bar; the game's window gets the matching title bar.
  The file and folder pickers are Windows' own: their title and buttons are set in Turkish,
  everything else in them is in the language of Windows. The licences in the About box are
  shown in Turkish; their binding English originals are in `docs/LICENSES.txt`, which the
  packages carry as `LICENSES.txt`.
* **macOS**: the question and the errors are system alerts (`NSAlert`), the progress a system
  panel (`src/platform/ui_macos.m`, not yet built on a Mac).
* The diagnostic log on the console stays English; the game's own texts are the original's.

Test hooks (Windows): `LALE_UI_SHOT=<prefix>` saves a picture of every dialog and answers it
(`LALE_UI_ANSWER=<button>`, default the Esc button); `LALE_UI_THEME=light|dark` overrides the
system's mode; with `LALE_UI_ABOUT=1` as well, the About box is pictured.

### Options

| `lale.ini` | Command line | Default | |
|---|---|---|---|
| `source` | `<path>` or `--source <path>` | | what to import at this start |
| `edition` | `--edition auto\|cd\|2005` | `auto` | CD edition when `DATA/LALE.CHD` has audio tracks |
| `base_dir` | `--base-dir <folder>` | beside the executable | where `DATA`, `SAVE` and `VIDEO` are |
| `video` | `--video 4k\|original` | `4k` | what videos start as when `VIDEO/` has files |

`game_dir` and `cd_image` are development options (the game straight from a folder such as
`orig/`, without `DATA`/`SAVE`; `cd_image` is the disc for `edition = cd` there). `cd_music` is
the old name of `edition = cd`.

### Tools

* `chdtool` (built with the port): `create <cue> <chd>`, `info`, `verify`, `ls`, `cat`,
  `extract`, `import <base> <source>` - the same code the game uses.
* `tools/chd.py`: an independent reader (`info`, `verify`) built on zlib and ffmpeg's FLAC
  decoder, which also compares the tracks with redump.org's sums.

### Verification

`tests/test_codecs.c` (ctest `codecs`) exercises every module on data it makes up: deflate,
FLAC, Mode 1 sectors, ISO 9660, CHD writing and reading, ZIP, and the importer with a folder, a
ZIP, cue/bin, an ISO, a CHD and seven kinds of unusable source.

Runs with the two references (2026-11-24, Windows 11):

| Source | Result |
|---|---|
| redumper dump folder of the CD (`lale.cue`, 3 `.bin`) | `LALE.CHD`, 208,112,607 bytes, SHA-1 `18e0f0f394bcb6948be1c8c279c9d54a39600608`; written in 13 s, verified in 5 s |
| the same disc as an image in redump.org's layout | the identical CHD |
| that CHD read back | all three tracks equal the `.bin` files; `tools/chd.py verify` (zlib, ffmpeg) agrees; tracks 1 and 2 match redump.org 91232 |
| `IST.EXE`, `0GFL.CKD`, `SOUNDS.CKD` read from the CHD | identical to the files |
| 2005 release as ZIP, 7Z, RAR and folder | the same 16 files in `DATA/` and `GRP.DAT`, `SAKLA001.SAV`, `SES.CFG` in `SAVE/`, equal to the originals |
| data track as `.iso`; an existing `.chd` | imported; files read back identical |
| a mounted ISO as a drive (Windows) | imported through the drive path (table of contents, data sectors) |
| both editions started from their imports | 2005 from the ZIP, in a base folder with Turkish letters and a blank in its name; CD from the redumper folder: the CD checks pass, the intro video plays from the CHD |

Not verified:

* **Audio from a physical drive** - no optical drive on the development machine. The data path
  of a drive was run with a mounted ISO.
* **Track 3** of this dump lacks its last 192 bytes (48 samples: the drive could not read into
  the lead-out), so it alone does not match redump.org; they are stored as silence.
* **macOS**: the new start-up, the import and `hdvideo_avf.m` have not been built or run there
  yet.
