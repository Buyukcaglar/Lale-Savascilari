# İstanbul Efsaneleri – Lale Savaşçıları: geri derleme ve taşıma planı / İstanbul Efsaneleri – Lale Savaşçıları: decompilation and port plan

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Amaç, DOS oyunu *İstanbul Efsaneleri – Bölüm I: Lale Savaşçıları*'nın (v0.12.96, 1995–2005)
tamamen açıklamalı, insanın okuyabileceği, taşınabilir bir C ile yeniden kurulumudur
(reconstruction). Kod **Windows** (geliştirme orada yapılıyor) ve **macOS** (Apple Silicon;
2026-11-23 tarihli taşıma (port), [`MACOS.md`](MACOS.md)) için derlenir.

**1:1 aslına uygunluk (fidelity) en yüksek önceliktir.** Oyunda görülebilen her davranış özgün
DOS çalıştırılabilir dosyasıyla aynı olmalıdır: oyun mantığı, RNG dizileri, zamanlama, piksel
düzeyine kadar çizim (rendering), palet, ses karıştırma ve kayıt dosyası baytları. Okunabilirlik
ikinci, taşınabilirlik üçüncü sıradadır.

---

### 1. Girdiler

`_references/Istanbul-Efsaneleri-Lale-Savascilari_DOS_TR.zip`, `orig/` dizinine açılır
(git tarafından yok sayılır).

| Dosya | Görevi |
|---|---|
| `IST.EXE` | Oyunun kendisi. 16 bit gerçek kip (real mode), Borland C++ 3.x, büyük bellek modeli (large model), VROOMM bindirmeleri (overlays), 8087 öykünücüsü (emulator), EMS gerektirir |
| `KUR.EXE` | Ses kurulumu ("kur" = install). `SES.CFG` dosyasını yazar |
| `*.CKD` | "FLiB" arşivleri (bkz. `docs/FORMATS.md`) |
| `SDI*.DSD` | DSMI ses sürücüleri (GUS, PAS, SB16, WSS) |
| `SES.CFG` | Ses yapılandırması (sürücü adı ve parametreleri) |
| `TRDOS.DAT` | 8×16 bit eşlem (bitmap) yazı tipi, 256 karakter şekli (glyph) (Türkçe kod sayfası) |
| `GRP.DAT`, `GRPHAZIR.DAT` | Grup / karakter verileri (`GRPHAZIR` = "hazır grup") |
| `SAKLA001.SAV` | Bir kayıtlı oyun ("sakla" = kaydet) |
| `SONPAL.PAL`, `BOSCH.KOR` | Palet, çeşitli |

Arşiv içerikleri (`tools/flib.py` ile `extracted/<archive>/` altına çıkarılır):

| Arşiv | İçerik |
|---|---|
| `0GFL` | 35 `.GFL` + 35 `.GHD` tam hareketli video (full-motion video) (semt giriş/çıkış videoları, oyun sonu) – 218 MB |
| `3D` | Birinci şahıs motoru: duvar/kapı/merdiven dokuları (textures) `.CMP`, `.PAL`, `.KOR` koordinat dosyaları, `.RES` |
| `FRPOS` | `PCn`/`PCSn` olay/dükkân betikleri (FRPOS.L yorumlayıcısı) |
| `IKON` | 94 simge/portre `.CMP` + `.PAL` |
| `IVIR` | Çeşitli ekranlar `.CMP`/`.PAL`, `.RIX` yazı tipleri, THANX.DOC |
| `MUZIK` | 15 `.AMF` DSMI modülü |
| `SAVAS` | Savaş grafikleri `.CMP`, `.KOR` |
| `SOUNDS` | 196 `.PUR` ham ses örneği (raw sample) + 10 `.SND` |

### 2. İkili dosya (binary) bilgileri (IST.EXE)

* MZ görüntüsü 0x26010 bayt, 2063 yer değiştirme kaydı (relocation), giriş noktası `0000:0000`
  (Borland `c0`), DGROUP = **16E0**.
* Borland `__SEGTABLE__` dosyada 0x18800 konumundadır ve 61 girdi `{seg, maxoff, flags, minoff}`
  içerir. TLINK segmentleri bayt hizalamasıyla paketler, bu yüzden içerik `[seg:minoff, seg:maxoff)`
  aralığında durur.
* FBOV bindirme bloğu dosyada 0x28410 konumundadır: 15 bindirme (bir koçan (stub) daha var ama
  boş) ve 280 giriş geçişi (entry thunk). Koçan başlığı `tools/exeload.py` içinde çözülür.
  Bindirme düzeltmeleri (fixups) *segtable indeksi × 8* değerini tutar.
* Yaklaşık 1700 kayan nokta (floating-point) işlemi 8087 öykünücü kesmeleridir (`INT 34h`–`3Dh`).
  `tools/ghidra/FixFPU.java` bunları yeniden x87 işlem kodlarına (opcode) çevirir, ama yalnızca
  gerçek komut sınırlarında.
* `asm2599`, veri sınıfı bir segmentte duran, elle yazılmış 186/386 assembly kodudur. Oyun bir
  386 gerektirir.

#### Modül haritası (`analysis/names/*.md` içindeki envanterden)

| Segment(ler) | Modül | Kaynak | Taşımada ele alınışı |
|---|---|---|---|
| seg0000 | Borland C++ 3.x RTL `_TEXT` (c0, stdio, malloc, printf, math…) | RTL | yerine başkası konur; `rand`/`time`/`itoa`/printf'in birebir davranışı `src/rtl` içinde |
| seg1351, seg15C9 | 8087 öykünücüsü / gerçek x87 düzeltme (fix-up) desteği | RTL | atılır (yerel FP, denetlenmiş) |
| seg122B | Borland VROOMM bindirme yöneticisi (`_OvrInitEms`, INT 3Fh) | RTL | atılır |
| seg0546 | `CODE\ISTANBUL.C` – `main`, başlangıç denetimleri | oyun | geri derlenir |
| seg05DC | `ALTDUZEY\INTRFACE.L` – kullanıcı arayüzü, örneklenmiş sesler | motor | geri derlenir |
| seg09D4 | `ALTDUZEY\MEMMAN.L` – EMS/konvansiyonel bellek | motor | bir yığın (heap) arka ucu üzerine geri derlenir |
| seg0AC6 | `ALTDUZEY\SYSTEMS.L` – saat, girdi, blit'ler | motor | geri derlenir |
| seg0C7F | `ALTDUZEY\NGRAPHS.L` – grafik | motor | geri derlenir |
| seg0D42 | `ALTDUZEY\NSES.L` – ses | motor | geri derlenir |
| seg0DB7 | `ALTDUZEY\NKONTROL.L` – klavye/fare, 100 Hz saat ISR'si | motor | geri derlenir; ISR bir platform tıkına (tick) dönüşür |
| seg08D0 | 3D dörtgen oluşturma/döndürme/kırpma/rasterleştirme (3DQUICK kullanır) | oyun | geri derlenir |
| asm2599 | elle yazılmış 386: ölçekli/aynalı blit'ler, palet karıştırma, 2× karıştırmalı büyütücü (upscaler) | oyun | geri derlenir (bit düzeyinde birebir) |
| seg0DF2 | DDS – DSMI dinamik sürücü/kaynak kaydı, `.DSD` yükler | DSMI | yerine başkası konur |
| seg0DDF, 0E9E, 1134, 1145, 1154 | DSMI yardımcıları, MemAlloc, EMS yığını, zaman tabanı | DSMI | yerine başkası konur / kısmen geri derlenir |
| seg0EAE | DSMI CDI (kanal dağıtıcısı, sürücüye yönlendirme) | DSMI | arayüzü geri derlenir |
| seg0EF6, 105B, 111B | DSMI AMP 2.00 çalar, AMF yükleyici, modül serbest bırakma | DSMI | **geri derlenir** (tempo/efekt aslına uygunluğu) |
| seg10EF | DSMI zamanlayıcı hizmeti (INT 08h, PIT ≈100 Hz, bölen 11931) | DSMI | aynı hızda çalışan platform zamanlayıcısı |
| `SDISB16.DSD` (dosya) | **MCP yazılım karıştırıcısı (mixer)** + SB16 sürücüsü | DSMI | **karıştırıcı sürücü dosyasından geri derlenir** |
| ovl2700 | `3DQUICK.L` – birinci şahıs çizicisi (renderer), FP başlangıç tabloları | oyun | geri derlenir |
| ovl303F | `3DSYS.L` – keşif döngüsü (303F:8524), kamp, kaydetme/yükleme, seçenekler, hileler | oyun | geri derlenir |
| ovl39E5 | `CHARGEN.L` – karakter oluşturma, GRP.DAT | oyun | geri derlenir |
| ovl3C2D | `DEMOLAR.L` – giriş, şehir haritası, semtler arası yolculuk, oyun sonu, jenerik (12h kipi) | oyun | geri derlenir |
| ovl3FD1 | `FRPOS.L` – dükkân/olay betiği yorumlayıcısı (33 işlem kodu) | oyun | geri derlenir |
| ovl4595 | `ISOSYS.L` – izometrik savaş (`iso_battle` 4595:B40A) | oyun | geri derlenir |
| ovl5313 | `GFL.C` – GFL/GHD video oynatıcı (RLE/delta kodek) | oyun | geri derlenir |
| ovl5447 | `ALTDUZEY\FILEMAN.L` – RLE/CMP çözücüler, resim/dosya yükleme | motor | geri derlenir |
| ovl5511 | `OGRAPHS.L` – kip ayarlama, paletler, ekran temizleme | motor | geri derlenir |
| ovl559A | ölümcül hata / çeşitli | motor | geri derlenir |
| ovl55CC | `OSES.L` – ses yardımcıları | motor | geri derlenir |
| ovl5646, ovl569A | FLiB arşivleri üzerinde sanal dosya katmanı + onun FILE tablosu | motor | geri derlenir |
| ovl5737 | MSCDEX CD ses denetimi (aygıt istekleri) | motor | platformda CD ses için yerine geçen çözüm |
| ovl5784 | `GRTEXT.L` – metin çizimi, Türkçe büyük/küçük harf yardımcıları | motor | geri derlenir |

Temel donanım bilgileri:

* **Görüntü (video)**: BIOS kipi 13h, zincirli (chained). 320×204'lük bir yüzey ekran dışında
  oluşturulur ve dikey geri dönüşten (vertical retrace) sonra A000'a kopyalanır; başlangıç adresi
  her zaman 0'dır. Jenerik ekranlarında **12h kipi** (640×480×16, düzlemsel (planar)) kullanılır.
  DAC, geri dönüşten sonra `rep outsb` ile yazılır.
* **Zamanlayıcı (timer)**: DSMI, INT 08h'yi kancalar (hook). Motorun 100 Hz rutini 16E0:E74A'daki
  bir saate 10 ms ekler. AMP çalar tıkı ⌊BPM·2/5⌋ Hz'dir ve 10 ms'ye nicemlenir (quantised). GFL
  videosu hızını bu saate göre ayarlar.
* **Girdi (input)**: BIOS `kbhit`/`getch` (INT 09h yalnızca Ctrl+Alt+Del'i yakalar). Fare, INT 33h
  işlevlerini (0/3/4/7/8) bir yazılım imleciyle kullanır.
* **INT 00h**: sıfıra bölme 0xFFFF sonucunu verir ve yürütme devam eder. Bu yeniden üretilmelidir.
* **RNG**: Borland `rand` (tohum 16E0:94C4), `random(n)` = satır içi `(long)rand()*n/0x8000`, ve
  `main` içinde `randomize()`. Borland'ın `max()` makrosu argümanlarını iki kez değerlendirir;
  ISOSYS'teki inisiyatif hesabı buna dayanır, dolayısıyla RNG akışı da buna bağlıdır.

### 3. Strateji

#### 3.1 Doğrudan okunabilir C'ye geri derleyin, sonra denkliği kanıtlayın

Her işlev için çalışma şöyle ilerler:

1. Ghidra taslağını (`analysis/ghidra_ist/*.c`) **ve** açıklamalı dökümü (listing)
   (`analysis/ghidra_ist/asm/*.asm`) okuyun. Esas olan dökümdür; taslak yalnızca bir ipucudur.
2. Okunabilir C kodunu adlar, türler ve yorumlarla `src/` altına yazın. Her işlev, özgün adresini
   veren bir başlık yorumu taşır (`/* IST.EXE ovl4595:1234 */`).
3. İşlevi `docs/PROGRESS.md` içinde tamamlandı olarak işaretlemeden önce doğrulayın (bkz. §4).

#### 3.2 C kodu için aslına uygunluk kuralları

* **Tamsayı genişlikleri açıkça belirtilir.** Borland `int` 16 bit, `long` 32 bittir.
  Kod `int16_t`/`uint16_t`/`int32_t` kullanır ve özgün kodun dayandığı yerlerde 16 bit taşma
  sarmasını (wrap-around), işaret genişletmeyi (sign extension) ve `/`/`%` kesme anlamını korur.
  BC++'ta `char` işaretlidir.
* **Kayan nokta.** Özgün kodun sonuçları 8087 öykünücüsünden ya da 80 bit ara değerlerle çalışan
  bir x87'den gelir. Her FP ifadesi bit düzeyinde birebir sonuç için denetlenir. `double`'ın
  farklı sonuç verdiği yerlerde kod, açık yuvarlama ya da özgün koddan alınmış, önceden
  hesaplanmış tablolar kullanır.
* **RNG.** Borland `rand()`/`random()`/`randomize()` birebir yeniden gerçeklenir
  (LCG `seed = seed*0x015A4E35 + 1`, `(seed>>16)&0x7FFF`).
* **Veri tabloları** DGROUP'tan araçlarla bayt bayt aynı olarak üretilmiş C koduna çıkarılır,
  asla elle yazılmaz.
* **Kayıt dosyaları ve veri dosyaları** açık little-endian serileştirme ve çözümleme
  ((de)serialisation) ile bayt bayt okunur ve yazılır. Yapı düzenleri asla `memcpy` ile kopyalanmaz.
* **Zamanlama.** PIT'in yeniden programlanması ve IRQ0 işleyicileri platform katmanı tarafından
  özgün tık hızıyla birebir yeniden üretilir. Özgün kodda bir ISR içinde çalışan oyun mantığı
  aynı tık takvimiyle çalışır.

#### 3.3 Geri derlenmeyip yerine başkası konanlar

| Özgün | Yerine konan |
|---|---|
| Borland RTL (stdio, string, malloc, …) | ISO C kütüphanesi, artı davranışa duyarlı parçalar için `src/rtl/` (rand, printf tuhaflıkları, `itoa`, …) |
| 8087 öykünücüsü | yerel FP, birebirlik denetimleriyle |
| Bindirme yöneticisi (VROOMM) | hiçbir şey – bindirmeler sıradan koda dönüşür |
| EMS (`INT 67h`) / XMS | aynı MEMMAN API'sinin arkasında düz yığın belleği |
| VGA donanımı (portlar, A000h) | `src/platform/` kare arabelleği (framebuffer) + 6 bit DAC paleti → SDL3 dokusu |
| Klavye/fare ISR'si, `INT 33h` | SDL3 olayları → özgün tarama kodları (scan codes) / fare durumu |
| DSMI donanım sürücüleri (`.DSD`) | SDL3 ses akışı |
| DSMI karıştırıcısı + AMF çalar mantığı | **geri derlenir** (ses aslına uygunluğu), çıktı SDL3 akışına gider |
| CD-ROM denetimleri | öykünülen "CD takılı" durumu, veriler oyun klasöründen okunur |

### 4. Doğrulama

1. **İşlev düzeyinde karşılaştırmalı testler (differential tests)** (`tests/`, Python + Unicorn).
   Özgün işlev `build/ist_flat.bin` üzerinden (x87 geri yüklenmiş olarak) yapay ya da yakalanmış
   bir bellek durumu üzerinde çalışır; C işlevi aynı girdilerle küçük bir paylaşımlı kütüphane
   düzeneği (harness) üzerinden çalışır; çıktılar ve bellekteki yan etkiler karşılaştırılır. Bu
   yöntem çözücüler, matematik, kurallar ve RNG için uygundur.
2. **Betikli bir DOSBox-X hata ayıklayıcısıyla tüm oyunun karşılaştırılması.** DOSBox-X 2026.10'un
   `mcp_server` seçeneği, öykünücünün hata ayıklayıcısını yerel bir dinleyiciye bağlar
   (`tools/dosbox/dbxdebug.py`, satır tabanlı bir protokol: `REQ id EXEC <cmd>`). Bu, *gerçek*
   oyun üzerinde kesme noktaları (breakpoints), yazmaç okuma/yazma, `MEMDUMPBIN` (ham belleği
   dosyaya), EMS tutamaç eşlemleri, DAC durumu, port G/Ç ve geri dönüşe kadar çalıştırma
   (run-to-retrace) olanağı verir. `tools/dosbox/ist.conf` ile görüntü **0x0824** paragrafına
   yüklenir; dolayısıyla yerleşik kod ve DGROUP için düz bir `S:O` adresi çalışma zamanında
   `S+0x824:O` olur; canlı DGROUP'un statik görüntüyle eşleştiği doğrulanmıştır. Bu yolla
   yakalanan referans veriler (ground truth) şunlardır: başlangıçta oluşturulan tablolar, betikli
   girdiden sonraki oyun durumu, kareler (VGA düzlemleri + DAC) ve kesme noktalarındaki işlev
   girdileri/çıktıları. Taşıma da buna uygun, belirlenimci (deterministic) bir girdi yeniden
   oynatma kipi alır; böylece durum ve kareler kontrol noktalarında karşılaştırılabilir (diff).
3. **Veri gidiş-dönüşleri (round-trips).** Taşımanın yazdığı kayıtlar, aynı oyun durumu için
   özgün oyunun yazdıklarıyla bayt bayt aynı olmalıdır.

### 5. Depo düzeni

```
docs/        PLAN.md (§2'de modül haritası), FORMATS.md, PROGRESS.md, VERIFY.md, DECOMPILING.md, …
tools/       tersine mühendislik (RE) araçları (Python, Ghidra betikleri) – bkz. tools/README.md
analysis/    seçilmiş analiz çıktıları (analysis/ghidra_*/ altında üretilen taslaklar hariç)
src/game/    geri derlenmiş oyun modülleri (istanbul.c, s3d_*.c, iso_*.c, frpos*.c, …)
src/engine/  geri derlenmiş ALTDUZEY kütüphanesi (fileman, memman, ngraphs, nses, nkontrol, …)
src/dsmi/    geri derlenmiş DSMI karıştırıcısı/çaları (taşınabilir)
src/rtl/     Borland uyumlu çalışma zamanı parçaları
src/platform/ SDL3 arka ucu (görüntü, ses, girdi, zamanlayıcı, dosyalar)
tests/       karşılaştırmalı testler
```

### 6. Araç zinciri

| Amaç | Araç |
|---|---|
| Analiz | Python 3.14 + capstone/unicorn, Ghidra 12.2 (JDK 21), `tools/*` |
| Referans çalışma ortamı | DOSBox-X (`C:\DOSBox-X\dosbox-x.exe`) |
| Windows derlemesi | `.tools/` içinde CMake + Ninja + llvm-mingw (clang 23, UCRT), SDL3 3.4.18 |
| macOS derlemesi | CMake + Ninja + Apple clang, Homebrew'dan SDL3 – aynı kaynaklar; dağıtılacak uygulama: `macos-release` hazır ayarı (preset) (universal, statik SDL3), `tools/package_macos.sh` |
| macOS analizi / referans ortam (oracle) | `.tools/venv` (Python), `.tools/` içinde Ghidra, Homebrew `openjdk@21` ve `dosbox-x` – `tools/hostenv.sh` |

### 7. Yol haritası (roadmap)

1. **Temel** – paket açıcı (unpacker), segment modeli, Ghidra iş hattı (pipeline), FPU düzeltmesi,
   dökümler ✔
2. **Envanter** – her RTL/DSMI işlevini tanımlamak, ALTDUZEY API'sini ve oyun modüllerini
   adlandırmak; modül haritası §2'de, adlar `analysis/names.tsv` ve `analysis/names/*.md` içinde ✔
   (2026-11-20/21).
3. **Biçimler** – CMP resimleri, PAL, KOR, RES, RIX karıştırma tabloları, GFL/GHD video, AMF,
   PUR/SND, FRPOS betikleri, SAV/GRP.DAT; anlaşıldıklarını kanıtlayan Python görüntüleyici ve
   denetleyicileriyle ✔ (2026-11-23, [`FORMATS.md`](FORMATS.md); KOR/RIX/AMF için
   `tools/fmtcheck.py`).
4. **Platform + motor** – SDL3 arka ucu, ardından FILEMAN, MEMMAN, NGRAPHS, NKONTROL, SYSTEMS,
   INTRFACE, NSES. İlk kilometre taşı: giriş ekranları birebir aynı çiziliyor ✔ (2026-11-21).
5. **Oyun modülleri** – ISTANBUL → DEMOLAR → 3DQUICK/3DSYS → CHARGEN → FRPOS → ISOSYS → GFL ✔
   (2026-11-21: IST.EXE'nin her işlevi geri derlendi).
6. **Ses** – DSMI MCP karıştırıcısı + AMP çalar geri derlendi, DOSBox-X yakalamalarıyla
   karşılaştırıldı ✔ (bit düzeyinde birebir karıştırıcı, aynı CDI çağrı akışları; her oynanışta
   ses açık).
7. **Tüm oyunun doğrulanması**, ardından macOS derlemesi ✔ (2026-11-23, [`VERIFY.md`](VERIFY.md),
   [`MACOS.md`](MACOS.md)); işlev durumu: `python tools/funcstatus.py`.

## English

The goal is a fully annotated, human-readable, portable C reconstruction of the DOS game
*İstanbul Efsaneleri – Bölüm I: Lale Savaşçıları* (v0.12.96, 1995–2005). It builds
for **Windows** (developed there) and **macOS** (Apple Silicon; the port of 2026-11-23,
[`MACOS.md`](MACOS.md)).

**1:1 fidelity is the top priority.** Every game-visible behaviour must match the
original DOS executable: logic, RNG sequences, timing, rendering down to the pixel,
palette, audio mixing and save-file bytes. Readability comes second, and portability
third.

---

### 1. Inputs

`_references/Istanbul-Efsaneleri-Lale-Savascilari_DOS_TR.zip` is extracted to `orig/`
(git-ignored).

| File | Role |
|---|---|
| `IST.EXE` | The game. 16-bit real mode, Borland C++ 3.x, large model, VROOMM overlays, 8087 emulator, needs EMS |
| `KUR.EXE` | Sound setup ("kur" = install). Writes `SES.CFG` |
| `*.CKD` | "FLiB" archives (see `docs/FORMATS.md`) |
| `SDI*.DSD` | DSMI sound drivers (GUS, PAS, SB16, WSS) |
| `SES.CFG` | Sound configuration (driver name and parameters) |
| `TRDOS.DAT` | 8×16 bitmap font, 256 glyphs (Turkish code page) |
| `GRP.DAT`, `GRPHAZIR.DAT` | Party / character data (`GRPHAZIR` = "ready-made party") |
| `SAKLA001.SAV` | A save game ("sakla" = save) |
| `SONPAL.PAL`, `BOSCH.KOR` | Palette, misc |

Archive contents (extracted to `extracted/<archive>/` by `tools/flib.py`):

| Archive | Content |
|---|---|
| `0GFL` | 35 `.GFL` + 35 `.GHD` full-motion video (district intros/outros, ending) – 218 MB |
| `3D` | First-person engine: wall/door/stair textures `.CMP`, `.PAL`, `.KOR` coordinate files, `.RES` |
| `FRPOS` | `PCn`/`PCSn` event/shop scripts (FRPOS.L interpreter) |
| `IKON` | 94 icon/portrait `.CMP` + `.PAL` |
| `IVIR` | Misc. screens `.CMP`/`.PAL`, `.RIX` fonts, THANX.DOC |
| `MUZIK` | 15 `.AMF` DSMI modules |
| `SAVAS` | Battle ("savaş") graphics `.CMP`, `.KOR` |
| `SOUNDS` | 196 `.PUR` raw samples + 10 `.SND` |

### 2. Binary facts (IST.EXE)

* MZ image 0x26010 bytes, 2063 relocations, entry `0000:0000` (Borland `c0`), DGROUP = **16E0**.
* Borland `__SEGTABLE__` is at file offset 0x18800 with 61 entries `{seg, maxoff, flags, minoff}`.
  TLINK packs segments at byte alignment, so contents live at `[seg:minoff, seg:maxoff)`.
* FBOV overlay block at file offset 0x28410: 15 overlays (one more stub is empty) and 280 entry thunks.
  The stub header is decoded in `tools/exeload.py`. Overlay fixups hold *segtable index × 8*.
* About 1,700 floating-point ops are 8087-emulator interrupts (`INT 34h`–`3Dh`).
  `tools/ghidra/FixFPU.java` turns them back into x87 opcodes, but only at true
  instruction boundaries.
* `asm2599` is hand-written 186/386 assembly in a data-class segment. The game needs a 386.

#### Module map (from the inventory in `analysis/names/*.md`)

| Segment(s) | Module | Origin | Port treatment |
|---|---|---|---|
| seg0000 | Borland C++ 3.x RTL `_TEXT` (c0, stdio, malloc, printf, math…) | RTL | replaced; exact `rand`/`time`/`itoa`/printf behaviour in `src/rtl` |
| seg1351, seg15C9 | 8087 emulator / real-x87 fix-up support | RTL | dropped (native FP, checked) |
| seg122B | Borland VROOMM overlay manager (`_OvrInitEms`, INT 3Fh) | RTL | dropped |
| seg0546 | `CODE\ISTANBUL.C` – `main`, start-up checks | game | decompile |
| seg05DC | `ALTDUZEY\INTRFACE.L` – UI, sample voices | engine | decompile |
| seg09D4 | `ALTDUZEY\MEMMAN.L` – EMS/conventional memory | engine | decompile over a heap backend |
| seg0AC6 | `ALTDUZEY\SYSTEMS.L` – clock, input, blits | engine | decompile |
| seg0C7F | `ALTDUZEY\NGRAPHS.L` – graphics | engine | decompile |
| seg0D42 | `ALTDUZEY\NSES.L` – sound | engine | decompile |
| seg0DB7 | `ALTDUZEY\NKONTROL.L` – keyboard/mouse, 100 Hz clock ISR | engine | decompile; ISR becomes a platform tick |
| seg08D0 | 3D quad build/rotate/clip/rasterise (used by 3DQUICK) | game | decompile |
| asm2599 | hand-written 386: scaled/mirrored blits, palette blend, 2× blending upscaler | game | decompile (bit-exact) |
| seg0DF2 | DDS – DSMI dynamic driver/resource registry, loads `.DSD` | DSMI | replaced |
| seg0DDF, 0E9E, 1134, 1145, 1154 | DSMI helpers, MemAlloc, EMS heap, time base | DSMI | replaced / partly decompiled |
| seg0EAE | DSMI CDI (channel distributor, dispatch into driver) | DSMI | decompile the interface |
| seg0EF6, 105B, 111B | DSMI AMP 2.00 player, AMF loader, module free | DSMI | **decompile** (tempo/effects fidelity) |
| seg10EF | DSMI timer service (INT 08h, PIT ≈100 Hz, divisor 11931) | DSMI | platform timer at an identical rate |
| `SDISB16.DSD` (file) | **MCP software mixer** + SB16 driver | DSMI | **decompile the mixer from the driver file** |
| ovl2700 | `3DQUICK.L` – first-person renderer, FP start-up tables | game | decompile |
| ovl303F | `3DSYS.L` – exploration loop (303F:8524), camp, save/load, options, cheats | game | decompile |
| ovl39E5 | `CHARGEN.L` – character generation, GRP.DAT | game | decompile |
| ovl3C2D | `DEMOLAR.L` – intro, city map, district travel, ending, credits (mode 12h) | game | decompile |
| ovl3FD1 | `FRPOS.L` – shop/event script interpreter (33 opcodes) | game | decompile |
| ovl4595 | `ISOSYS.L` – isometric battle (`iso_battle` 4595:B40A) | game | decompile |
| ovl5313 | `GFL.C` – GFL/GHD video player (RLE/delta codec) | game | decompile |
| ovl5447 | `ALTDUZEY\FILEMAN.L` – RLE/CMP decoders, picture/file loading | engine | decompile |
| ovl5511 | `OGRAPHS.L` – mode set, palettes, screen clears | engine | decompile |
| ovl559A | fatal error / misc | engine | decompile |
| ovl55CC | `OSES.L` – sound helpers | engine | decompile |
| ovl5646, ovl569A | virtual file layer over the FLiB archives + its FILE table | engine | decompile |
| ovl5737 | MSCDEX CD-audio control (device requests) | engine | platform CD-audio replacement |
| ovl5784 | `GRTEXT.L` – text drawing, Turkish case helpers | engine | decompile |

Key hardware facts:

* **Video**: BIOS mode 13h, chained. A 320×204 surface is composed off-screen and copied to
  A000 after vertical retrace; the start address is always 0. **Mode 12h** (640×480×16,
  planar) is used for the credit screens. The DAC is written with `rep outsb` after retrace.
* **Timer**: DSMI hooks INT 08h. The engine's 100 Hz routine adds 10 ms to a clock at 16E0:E74A.
  The AMP player tick is ⌊BPM·2/5⌋ Hz, quantised to 10 ms. GFL video paces itself on that clock.
* **Input**: BIOS `kbhit`/`getch` (INT 09h only traps Ctrl+Alt+Del). Mouse uses INT 33h
  functions 0/3/4/7/8, with a software cursor.
* **INT 00h**: a division by zero yields 0xFFFF and execution continues. This must be reproduced.
* **RNG**: Borland `rand` (seed 16E0:94C4), `random(n)` = inline `(long)rand()*n/0x8000`, and
  `randomize()` in `main`. Borland's `max()` macro evaluates its arguments twice; ISOSYS
  initiative relies on this, so the RNG stream depends on it.

### 3. Strategy

#### 3.1 Decompile directly to readable C, then prove equivalence

For each function the work goes:

1. Read the Ghidra draft (`analysis/ghidra_ist/*.c`) **and** the annotated listing
   (`analysis/ghidra_ist/asm/*.asm`). The listing is authoritative; the draft is a hint.
2. Write the readable C in `src/` with names, types and comments. Every function carries
   a header comment giving its original address (`/* IST.EXE ovl4595:1234 */`).
3. Verify it (see §4) before marking the function done in `docs/PROGRESS.md`.

#### 3.2 Fidelity rules for the C code

* **Integer widths are explicit.** Borland `int` is 16 bits and `long` is 32 bits.
  Code uses `int16_t`/`uint16_t`/`int32_t` and keeps 16-bit wrap-around, sign
  extension, and the `/`/`%` truncation semantics where the original relies on them.
  `char` is signed in BC++.
* **Floating point.** The original's results come from the 8087 emulator or an x87, with
  80-bit intermediates. Every FP expression is checked for bit-exact results. Where `double`
  differs, the code uses explicit rounding or precomputed tables taken from the original.
* **RNG.** Borland `rand()`/`random()`/`randomize()` are reimplemented exactly
  (LCG `seed = seed*0x015A4E35 + 1`, `(seed>>16)&0x7FFF`).
* **Data tables** in DGROUP are extracted byte-exact by tools into generated C, never hand-typed.
* **Save files and data files** are read and written byte-for-byte with explicit
  little-endian (de)serialisation. Struct layouts are never `memcpy`'d.
* **Timing.** PIT reprogramming and IRQ0 handlers are reproduced by the platform layer
  at the exact original tick rate. Game logic that ran in an ISR runs on the
  same tick schedule.

#### 3.3 What gets replaced instead of decompiled

| Original | Replacement |
|---|---|
| Borland RTL (stdio, string, malloc, …) | ISO C library, plus `src/rtl/` for behaviour-sensitive pieces (rand, printf quirks, `itoa`, …) |
| 8087 emulator | native FP, with exactness checks |
| Overlay manager (VROOMM) | nothing – overlays become normal code |
| EMS (`INT 67h`) / XMS | plain heap memory behind the same MEMMAN API |
| VGA hardware (ports, A000h) | `src/platform/` framebuffer + 6-bit DAC palette → SDL3 texture |
| Keyboard/mouse ISR, `INT 33h` | SDL3 events → original scan codes / mouse state |
| DSMI hardware drivers (`.DSD`) | SDL3 audio stream |
| DSMI mixer + AMF player logic | **decompiled** (audio fidelity), output to the SDL3 stream |
| CD-ROM checks | emulated "CD present", data read from the game folder |

### 4. Verification

1. **Function-level differential tests** (`tests/`, Python + Unicorn). The original
   function runs from `build/ist_flat.bin` (with x87 restored) on a synthetic or captured
   memory state; the C function runs on the same inputs through a small shared-library
   harness; outputs and memory side effects are compared. This suits decoders, math,
   rules and RNG.
2. **Whole-game comparison through a scripted DOSBox-X debugger.** DOSBox-X 2026.10's
   `mcp_server` option connects the emulator's debugger to a local listener
   (`tools/dosbox/dbxdebug.py`, a line protocol: `REQ id EXEC <cmd>`). That gives
   breakpoints, register reads/writes, `MEMDUMPBIN` (raw memory to file), EMS handle
   maps, DAC state, port I/O and run-to-retrace on the *real* game. With
   `tools/dosbox/ist.conf` the image loads at paragraph **0x0824**, so a flat address
   `S:O` is runtime `S+0x824:O` for resident code and DGROUP; live DGROUP was confirmed
   to match the static image. Ground truth captured this way includes tables built at
   start-up, game state after scripted input, frames (VGA planes + DAC) and function
   inputs/outputs at breakpoints. The port gets a matching deterministic input-replay
   mode so that state and frames can be diffed at checkpoints.
3. **Data round-trips.** Saves written by the port must be byte-identical to the
   original's for the same game state.

### 5. Repository layout

```
docs/        PLAN.md (module map in §2), FORMATS.md, PROGRESS.md, VERIFY.md, DECOMPILING.md, …
tools/       RE tooling (Python, Ghidra scripts) – see tools/README.md
analysis/    curated analysis outputs (generated drafts under analysis/ghidra_*/ are ignored)
src/game/    decompiled game modules (istanbul.c, s3d_*.c, iso_*.c, frpos*.c, …)
src/engine/  decompiled ALTDUZEY library (fileman, memman, ngraphs, nses, nkontrol, …)
src/dsmi/    decompiled DSMI mixer/player (portable)
src/rtl/     Borland-compatible runtime pieces
src/platform/ SDL3 backend (video, audio, input, timer, files)
tests/       differential tests
```

### 6. Toolchain

| Purpose | Tool |
|---|---|
| Analysis | Python 3.14 + capstone/unicorn, Ghidra 12.2 (JDK 21), `tools/*` |
| Reference runtime | DOSBox-X (`C:\DOSBox-X\dosbox-x.exe`) |
| Windows build | CMake + Ninja + llvm-mingw (clang 23, UCRT) in `.tools/`, SDL3 3.4.18 |
| macOS build | CMake + Ninja + Apple clang, SDL3 from Homebrew – same sources; the app to give away: preset `macos-release` (universal, static SDL3), `tools/package_macos.sh` |
| macOS analysis / oracle | `.tools/venv` (Python), Ghidra in `.tools/`, Homebrew `openjdk@21` and `dosbox-x` – `tools/hostenv.sh` |

### 7. Roadmap

1. **Foundation** – unpacker, segment model, Ghidra pipeline, FPU fix, listings ✔
2. **Inventory** – identify every RTL/DSMI function, name the ALTDUZEY API and the game
   modules; module map in §2, names in `analysis/names.tsv` and `analysis/names/*.md` ✔
   (2026-11-20/21).
3. **Formats** – CMP images, PAL, KOR, RES, RIX blend tables, GFL/GHD video, AMF, PUR/SND,
   FRPOS scripts, SAV/GRP.DAT, with Python viewers/checkers to prove understanding ✔
   (2026-11-23, [`FORMATS.md`](FORMATS.md); `tools/fmtcheck.py` for KOR/RIX/AMF).
4. **Platform + engine** – SDL3 backend, then FILEMAN, MEMMAN, NGRAPHS, NKONTROL, SYSTEMS,
   INTRFACE, NSES. First milestone: intro screens render identically ✔ (2026-11-21).
5. **Game modules** – ISTANBUL → DEMOLAR → 3DQUICK/3DSYS → CHARGEN → FRPOS → ISOSYS → GFL ✔
   (2026-11-21: every function of IST.EXE decompiled).
6. **Audio** – DSMI MCP mixer + AMP player decompiled, compared against DOSBox-X captures ✔
   (bit-exact mixer, identical CDI call streams; sound on in every play-through).
7. **Full-game verification**, then the macOS build ✔ (2026-11-23, [`VERIFY.md`](VERIFY.md),
   [`MACOS.md`](MACOS.md)); function status: `python tools/funcstatus.py`.
