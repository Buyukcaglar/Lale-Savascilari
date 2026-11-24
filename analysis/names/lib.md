# "lib" grubu: DSMI / DDS / Zamanlayıcı / EMS, asm2599, FP öykünücüsü, küçük segmentler / Group "lib": DSMI / DDS / Timer / EMS, asm2599, FP emulator, small segments

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

`lib.tsv` içinde listelenen her fonksiyon. Tüm DGROUP ofsetleri 16E0 segmentindedir.

### 1. Her segment nedir

| Seg | Boyut | Nedir | Köken |
|---|---|---|---|
| 0DF2 | 0xAC0 | **DDS**, dinamik sürücü sistemi: ad→fonksiyon ve ad→kaynak kayıt defterleri ile `.DSD` sürücülerinin yükleyicisi | DSMI (Otto Chrons) |
| 0DDF | 0x130 | DDS'in kullandığı küçük pascal dizge ve bellek yardımcıları (strcpy, strcmp, strlen, memcpy, memcmp, memset, abs) | DSMI |
| 0E9E | 0x105 | `MemAlloc/MemCalloc/MemRealloc/MemFree`. Bunlar Borland farmalloc, farcalloc, farrealloc ve farfree'yi sarmalar; DDS bunları adlarıyla dışa aktarır | DSMI |
| 0EAE | 0x48A | **CDI**, kanal dağıtıcısı: `cdiInit`, `cdiRegister` ve 26 ince yönlendirme sarmalayıcısı | DSMI |
| 0EF6 | 0x1659 | **AMP 2.00** modül çalıcısı. Veri tabloları 0x000A–0x039A arasındadır ("AMP 2.00 - (C) 1992,1994 Otto Chrons" ve "MCPSampleRealAddress"). **MCP karıştırıcısı (mixer) bu segmentte değildir** | DSMI |
| 105B | 0x945 | AMP'nin AMF yükleyicisi: `ampLoadModule`, `ampLoadAMF`, `ampLinkTracks` | DSMI |
| 10EF | 0x2C3 | **Timer Service v2.0**, "(C) 1993-1995 Otto Chrons": INT 08'e kanca atar ve PIT'i yeniden programlar | DSMI |
| 111B | 0x19D | `ampFreeModule` | DSMI |
| 1134 | 0x114 | Alt düzey EMS 4.0 çağrıları (INT 67h) | DSMI |
| 1145 | 0xF1 | DSMI zaman tabanı: `GetDSMITime`, `LockDSMITime`, `UnlockDSMITime` ve 100 Hz artırıcı | DSMI |
| 1154 | 0xD7A | EMS öbeği (heap): `EMSAlloc/EMSFree/EMSCopyTo/EMSCopyFrom/EMSCopy/EMSLock/EMSSaveState/EMSRestoreState`, DDS üzerinden dışa aktarılır. **MEMMAN (seg09D4) da bunu kullanır**, yani oyunun EMS belleği DSMI'nin EMS öbeğinden geçer | DSMI |
| 2599 | 0x675 | Elle yazılmış 386 grafik çekirdekleri: ölçekli, saydam ve aynalanmış blit'ler, 32 bitlik memset ve memcpy, harmanlama tablosuyla harmanlama ve 2× görünüm büyütücü. Bkz. §6 | yazar (ALTDUZEY asm) |
| 08D0 | 0x1046 | 3B çokgen kodu: duvar dörtgenlerini kurar, döndürür, kırpar ve piksellere döker (rasterise). Tüm çağrılar ovl2700 `F_2700_8197/8896`'dan gelir. Muhtemelen 3DQUICK.L'nin bellekte kalıcı (resident) kısmı (q3d_) | oyun / motor |
| 1351 | 0x2776 | Borland **8087 yazılım öykünücüsü (emulator)** (EMU). Durumu SS:0000'da tutulur (durum, denetim, SS:[0Ch]'de öykünülen yığın işaretçisi, 12 baytlık geçici reel sayılar). INT 34h–3Dh giriş noktalarına çağrılarla değil, vektörler üzerinden ulaşılır | Borland RTL |
| 15C9 | 0x5C8 | Borland **gerçek x87 desteği** (FP87). 0x0000–0x0033 80 bitlik sabitleri tutar (π/4, π/2 …). 0x0034–0x02EF x87 aşkın fonksiyon (transcendental) çekirdekleridir (fptan, fpatan, f2xm1, fyl2x; her biri 9B önekli). Ayrıca INT 34h–3Dh **düzeltme (fixup) işleyicisi** ve x87 istisna işleyicisi. "bosch.cmp" eşleşmesi yanlış bir dizge başvurusudur: `MOV DX,0x269B`, `fp87_reexec_on_stack` içindeki kod baytlarıdır | Borland RTL |

Kim neyi çağırır: `ovl55CC` kaplaması (overlay) (`T:ALTDUZEY\OSES.L`, başka bir grubun sorumluluğunda) ses ilklendirmesidir. Sırasıyla DDSRegisterFunction(MCPDisableVirtualSamples, GetEnvString, SetVector), cdiInit, DDSLoadDriver(e85c'deki SES.CFG adı), ardından DDSAccessFunction(SDIInitDevice, SDIStartOutput, SDIStopOutput), ampInit, tsAddRoutine ve cdiSetupChannels çalıştırır. `ovl559A` DSMI zaman tabanını kurar.

### 2. DDS (seg0DF2)

Genel değişkenler:
- `e776` (sözcük): sonraki sürücü tanıtıcısı (handle), 1'den başlar.
- `e778`: ilklendirildikten sonra sihirli değer 0xBEEF.
- `e77a`: kaynak listesinin gözcü düğümü (far ptr).
- `e77e`: fonksiyon listesinin gözcüsü.
- `e782`: sürücü listesinin gözcüsü.

Üç liste de gözcü (sentinel) düğümüyle kapanan dairesel, tek bağlı listelerdir.

DDS fonksiyonları pascal kurallı uzak (far) çağrılardır. Başarıda 0, aksi hâlde bir hata kodu döndürürler: 1 = NULL argüman, 2 = DDS ilklendirilmemiş ya da bellek yok, 3 = dosya açılamıyor, 4 = hatalı başlık, 5 = sürücü ilklendirmesi başarısız, 6 = bilinmeyen tanıtıcı, 7 = yinelenen ad, 8 = sürücü kimliği zaten yüklü, 9 = fonksiyon bulunamadı, 10 = kaynak bulunamadı, 11 = sürücü bellekten kaldırılmayı reddetti, 12 = bellek yetersiz.

Düğüm düzenleri:
- fonksiyon düğümü (12 bayt): `{char far *name; void far *fn; node far *next}`.
- kaynak düğümü (16 bayt): `{name; void far *ptr; long size; next @0C}`.
- sürücü düğümü (24 bayt): `{word loaded(1=file,0=static); word deviceId; long handle; void far *entry; void far *imageAligned; void far *imageRaw; next @14}`.

**.DSD dosya biçimi.** Sihirli değer olarak "DS" (0x5344) taşıyan bir MZ yürütülebilir dosyasıdır. Yükleyici şu başlık alanlarını kullanır:

| Ofset | Alan | Kullanım |
|---|---|---|
| 0x02 | son sayfadaki bayt sayısı | görüntü boyutu |
| 0x04 | sayfa sayısı | görüntü boyutu |
| 0x06 | yer değiştirme kaydı (relocation) sayısı | yer değiştirme kayıtları |
| 0x08 | başlık paragrafları | görüntü başlangıcı |
| 0x0A | asgari ek paragraflar | bellek ayırma |
| 0x14 | CS:IP | giriş noktası |
| 0x18 | yer değiştirme tablosu ofseti | yer değiştirme kayıtları |
| 0x0C | aygıt kimliği | GUS16=0xC8, SB16=0x64, WSS16=0x65, PAS16=0x66 |
| 0x12 | sürüm | 0x0100 |
| 0x1A | bayraklar | bit0 1 olmalı |

Yükleme adımları:
1. Görüntü boyutu = (pages−1)·512 + lastbytes − hdr·16 + 0x20.
2. size + minalloc·16 + 0x20 bayt ayır ve paragraf sınırına hizala.
3. Yer değiştirme kayıtlarını oku, görüntüyü oku ve her yer değiştirme noktasına yükleme segmentini ekle.
4. `entry(long cmd=1, DDSAccessFunction, long 0)` çağır. DX:AX, AX = hata ve DX = sürücü/aygıt kimliği olarak döner.

Bellekten kaldırma `entry(2,0,0)` çağırır.

### 3. CDI (seg0EAE)

Genel değişkenler:
- `7694`: sihirli değer 0xDA.
- `7696[256]`: kanal → aygıt indisi.
- `7796 + n·0x68`: kaydedilen CDIDEVICE tablolarının kopyaları. 8 tanesine yer var.
- `7AD6`: aygıt sayısı.

`cdiRegister` her aygıta 32 kanal verir: n aygıtı n·32 … n·32+31 kanallarına sahiptir.

CDIDEVICE 26 uzak fonksiyon işaretçisi içerir. Sıraları SDIGUS16.DSD içindeki `gus*` sembol adlarıyla doğrulanmıştır:

`00 setSample, 04 playSample, 08 setVolume, 0C setFrequency(rate), 10 setLinearRate, 14 setPosition, 18 setPanning, 1C setMasterVolume, 20 mute, 24 unmute, 28 stopChannel, 2C muteAll, 30 unmuteAll, 34 download, 38 unload, 3C unloadAll, 40 getVolume, 44 getFrequency, 48 getPosition, 4C getPanning, 50 getSample, 54 setupChannels, 58 getChannelStatus, 5C poll, 60 setEffect, 64 getStatus`.

Her sarmalayıcı aygıtı kanal argümanından seçer ve kalan argümanları iletir. Çağrılar 32 bitlik argümanlarla pascal kuralındadır.

### 4. AMP çalıcısı (seg0EF6) – veri yapıları

**Kod segmenti tabloları (CS:)**

| Ofset | İçerik |
|---|---|
| 0x45 | track·45 (iz (track) dizisine ofset) |
| 0x85 | track·26 (SAMPLEINFO dizisine ofset) |
| 0xC5 | 128 sözcük, 2^(n/12)·1024 (n=60'ta 0x3FF, n=84'te 0x1000, 0x8000'de doyuma ulaşır) |
| 0x1CD | periyot tablosu ×8: nota 60'ta 0x1AC0 = 6848, alt uçta 0xFFD5'e, üst uçta 0x0097'ye kırpılmış |
| 0x2CB | 64 girdilik yarım sinüs tablosu, 0..4095..0 |
| 0x34B | 13 sözcük, 2^(k/12)·1024 (arpej) |
| 0x36B | 0x80..0x97 için 24 girdilik satır efekti atlama tablosu |
| 0xDD0 | tık efektlerinin 12 {cmd, handler} çifti |

**MODULE** (0x5C bayt, `ampLoadModule` tarafından ayrılır; çalarken DGROUP 7BE8'de bir kopyası oluşturulur)

| Ofset | Alan |
|---|---|
| +00 | tür (3 = AMF) |
| +01 | dword kullanılan bellek |
| +05 | dword dosya boyutu |
| +09 | name[32] |
| +29 | kanal sayısı |
| +2A | işaretli pan[32] (−63..63, 100 = surround) |
| +4A | enstrüman sayısı |
| +4B | far INSTRUMENT* |
| +4F | sıra (order) sayısı |
| +50 | far ORDER* |
| +54 | word iz sayısı |
| +56 | uzak işaretçi tablosu track[count+1]; indis 0 = NULL |
| +5A | BPM (varsayılan 125) |
| +5B | hız (varsayılan 6) |

**ORDER** (0x82 bayt): `{word rows; void far *track[32]}`. Yükleyici önce iz numaralarını saklar, ardından `ampLinkTracks` bunları uzak işaretçilere dönüştürür.

**INSTRUMENT** (0x41 bayt, v≥10 için diskteki düzenle aynı)

| Ofset | Alan |
|---|---|
| +00 | tür |
| +01 | name[32] |
| +21 | filename[13] |
| +2E | örnek (sample) indisi, uzak örnek işaretçisiyle değiştirilir; 0xFFFF segmenti bir EMS tanıtıcısı demektir |
| +32 | dword uzunluk |
| +36 | word c2spd (hız) |
| +38 | ses düzeyi 0..64 |
| +39 | dword loopStart |
| +3D | dword loopEnd |

**İz verisi** (saklanan her iz için bir tane): 3 baytlık başlık `{word count; byte 0}`, ardından `{row, cmd, arg}` biçiminde `count` girdi. İz konumu 3'ten başlar. Girdilerin anlamları:
- cmd < 0x7F: nota, arg = ses düzeyi (0xFF = yok).
- nota 0: dur.
- nota 0x7F: geçerli notayı koru.
- cmd 0x80: enstrüman (arg).
- cmd 0x81–0x97: efektler.
- row 0xFF: iz sonu.
- dağıtılan dosyalarda row değeri FFh olan 496 girdi komut değil, iz sonu işaretleridir (`FF FF FF`, docs/FORMATS.md "AMF").

**İz durumu:** DGROUP `7C91`'de 32 × 45 bayt.

| Ofset | Alan |
|---|---|
| +00 | uzak iz işaretçisi |
| +04 | konum |
| +06 | bayraklar (bit1 = duraklatılmış) |
| +08 | nota |
| +09 | enstrüman |
| +0A | ses düzeyi |
| +0B | word tık sayacı |
| +0D | word periyot |
| +0F | geçerli komut |
| +10 | parametre |
| +11 | word ton-portamento hedefi |
| +13 | ton-portamento hızı |
| +14 | ton-portamento belleği |
| +15 | portamento belleği |
| +16 | dword arpej frekansı[3] |
| +22 | ? |
| +23 | vibrato konumu |
| +24 | vibrato parametresi |
| +25 | tremolo parametresi |
| +26/+27 | tremolo hızı/derinliği |
| +28 | tremolo konumu |
| +29 | ofset belleği |
| +2C | pan |

**Kanal başına SAMPLEINFO** (DGROUP `8231 + ch·26`'da 26 bayt), `cdiSetSample`'a geçirilir:
- +00 uzak örnek
- +04 uzunluk
- +08 loopStart
- +0C loopEnd
- +10 kip = 1
- +11 word sampleID (segment 0xFFFF olduğunda EMS tanıtıcısı)
- +13 c2spd
- +15 dword geçerli frekans
- +19 ses düzeyi

**AMP genel değişkenleri:**

| Ofset | Anlamı |
|---|---|
| 7C4E | ampInit seçenekleri |
| 7C50 | durum (bit0 çalıyor, bit1 duraklatılmış) |
| 7C51 | çalma seçenekleri (bit0 = döngü) |
| 7C53 / 7C54 / 7C55 | başlangıç / bitiş / geçerli sıra |
| 7C56 | geçerli iz |
| 7C57 | satır |
| 7C59 | son satır |
| 7C5B | tık |
| 7C5C | sıraların uzak işaretçisi |
| 7C60 | enstrümanların uzak işaretçisi |
| 7C64 | hız |
| 7C65 | BPM |
| 7C66 | 0x40 |
| 7C67 | eşzamanlama |
| 7C68 | kırma bayrağı |
| 7C6D | kanal sayısı |
| 7C6F[32] | pan |
| 7C8F | geçerli desendeki satır sayısı |
| 7C44 | ampGetTrackData'nın döndürdüğü 10 baytlık TRACKDATA |
| 7BE4 | AMP'nin özel ISR yığınının tepesi (eski SS:SP 7BE6/7BE4'te saklanır) |
| 8571[32] | iz başına ses düzeyi (varsayılan 0x40) |
| 8591 | AMP ilklendirildi bayrağı |
| 859E / 859F | yoklama / kesme yeniden giriş kilitleri |
| 85A0 | tık Hz |
| 85A2 | son tıkın dword zamanı |
| 85A6 | MCPSampleRealAddress'e uzak işaretçi (sürücüden) |
| 85AA | eski dosyalar için varsayılan panlar: −63, 63, 63, −63 ×4 |

### 5. Diskteki AMF düzeni (`ampLoadAMF`'nin okuduğu biçimiyle)

Sihirli değer `"AMF"` ve ardından gelen bir sürüm baytıdır. Yükleyici 1, 8, 9 ve 10–14 sürümlerini kabul eder, diğerleri için −3 döndürür. `extracted/MUZIK` içindeki 15 dosyanın hepsi sürüm 14'tür.

1. **Başlık** (v14'te 75 bayt): `"AMF" ver`, `name[32]`, `insCount u8`, `orderCount u8`, `trackCount u16`.
   - v≥9: `channels u8`, ardından `pan[16]` (v≥12 için pan[32]). v<11 için dosyadan okunan panların **üzerine** DGROUP 85AA'daki varsayılan tablo yazılır.
   - v≥13: `BPM u8`, `speed u8`.
   - `ampLoadModule` içinde v1 ve v8 dosyaları varsayılan panlarla 4 kanala zorlanır.
2. **Sıralar** (orderCount adet): `rows u16` (yalnızca v≥14; aksi hâlde rows = 64), ardından `channels × u16 trackRef`.
3. **Enstrümanlar**, yukarıdaki düzende insCount × 65 bayt. 10'dan önceki sürümler 59 bayt kullanır: length, loopStart ve loopEnd dword alanları u16'dır ve loopEnd 0xFFFF döngü yok demektir.
4. **İz eşlemi:** trackCount × u16; her mantıksal iz için saklanan izin numarasını verir.
5. **Saklanan izler:** `count u16`, `type u8`, ardından count×3 bayt. Sürüm 1'de 3 ek bayt vardır. Her iz için count·3+6 bayt ayrılır.
6. **Örnek verisi:** ham 8 bit, örnek indisi sırasıyla, dönüşüm yapılmadan. Her örnek uzunluk + 16 baytlık bir tampon alır. DDS fonksiyonları "EMSAlloc" ve "EMSCopyTo" varsa 0x400 bayttan büyük örnekler EMS'e gider; örnek işaretçisi bu durumda 0xFFFF:handle olur.

Dağıtılan müzik 80, 81, 82, 83, 84, 86, 88, 89, 8C, 8D, 94 ve 95 efektlerini kullanır (tempo 120 ve 144). 0x85 ya da 0x87'yi hiç kullanmaz.

### 6. asm2599 (veri sınıfı bir segmentte elle yazılmış 386 kodu)

Bu **bir karıştırıcı ya da açıcı (decompressor) değildir**. 386 komutları kullanan bir grafik çekirdekleri kümesidir: 32 bitlik yazmaçlar, REP STOSD/MOVSD, ENTER/LEAVE ve FS/GS segment yazmaçları için LFS/LGS.

**Kod segmentine yazılan veri.** CS:0000–0017 ayrılmış sıfır baytlardır. Ölçekli blit yordamları (0018 ve 0144) 16.16 x/y adım değerlerini, y biriktiricisini, satır sayacını ve satır başına DI düzeltmesini `CS:` önekleriyle buraya saklar.
- Bu, kod segmentine yazılan veridir. **Hiçbir komut baytı yamalanmaz**, yani katı anlamda kendini değiştiren kod değildir. C'de bunlar yerel değişkenlere dönüşür.

**Giriş noktaları.** Altısı doğrudan çağrılır (0018, 028F, 02AF, 0440, 04AD, 0500; 0440'ın çağıranı bulunamadı). Üçü daha, uzak fonksiyon işaretçisi olarak (`PUSH 0x2599 / PUSH off`, 102 nokta) SYSTEMS.L blit dağıtıcısı `0AC6:0430`'a geçirilir:
- `02D0`: opak dikdörtgen kopyalama (60 nokta).
- `0341`: saydam anahtar renkli dikdörtgen kopyalama (36 nokta).
- `03C4`: aynısı, yatay aynalanmış (6 nokta).

Bu üçünün argüman çerçevesi `(src far, sx, sy, w, h, srcPitch, dst far, dx, dy, dstPitch[, key])` biçimindedir. Başarıda 0xFFFF, `y·pitch+x` 16 biti taşarsa 0 döndürürler.

`0144`, `0234` ve `0440`'a başvuru yoktur.

**Harmanlama tablosu** (uzak işaretçi olarak geçirilir; 0440, 04AD ve 0500 kullanır). Sırasız a<b renk çiftleri için 32640 baytlık üçgen bir tablodur:
`idx = a·(509−a)/2 − 1 + b`. İki renk eşit olduğunda sonuç o renktir.

**`0500`** 3B görünümü 2× büyütür:
- Kaynak: 0xCB ofsetinden başlayan, satır aralığı (pitch) 100 bayt olan bir tampon.
- 1. geçiş çift ekran satırlarını yazar: 4,6,…,194, x = 6..190. Kaynak pikseller çift x'e gider; tek x ise blend(left, right) olur.
- 2. geçiş tek satırları dikey ve çapraz harmanlamayla (dört komşu) yazar.
- 0xFE kaynak değeri "ekranda zaten olan pikseli koru" demektir.
- Doğrudan A000h'ye yazar.

**Hatalar:**
- 0018 ve 0144'ün hata çıkışları `LEAVE` olmadan `RETF` yapar ve yığından dengesiz bir yazmaç kümesi çeker; bu yüzden çökerler. Bu yollara yalnızca 16 bitlik ofset taşmasında girilir.
- 03C4'ün hata yolu SI ve DS'i yanlış sırayla çeker; bu da DS'i bozar.
- Başarıda 0018, girişte AX'te ne varsa onu döndürür.

### 7. Zamanlama: müziği ne sürüyor (aslına uygunluk (fidelity) açısından kritik)

**Timer Service (seg10EF).** INT 08'e kanca atar ve PIT kanal 0'ı kip 3'te yeniden programlar (`out 43h,36h`).
- **Ana bölen, herhangi bir yuvanın kaydettiği en küçük hız değeridir.**
- DGROUP `85CE`'de 14 baytlık 16 yuva vardır: `{far isr; word rate; word step; word acc; byte active; byte ?}`.
- Yuva 0, hızı 0xFFFF olan özgün BIOS işleyicisidir.
- step = ((master<<16)|0xFFFF)/rate; ana hızı belirleyen yuva içinse 0xFFFF.
- Her IRQ0'da işleyici EOI gönderir, STI yürütür ve her etkin yuva için `acc += step` yapar; elde (carry) oluşursa yuvanın yordamını bir kesme gibi (PUSHF+CALLF) çağırır.
- En hızlı yuvanın adımı 0x10000 değil 0xFFFF olduğundan, bu yuva her 65536 IRQ'da bir çağrıyı atlar. Bu, 100 Hz'de yaklaşık 11 dakikada bir demektir.
- BIOS saati yaklaşık 1193182/65535 Hz'de zincirlenir.

**Oyunun kaydettiği hızlar:**
- 0x2E9B = 11931 → 100.007 Hz, iki kez kullanılır:
  - `1145:0096` (DSMI zamanı += 100, ovl559A tarafından kaydedilir).
  - NSES `0D42:0120`; `ampInterrupt`'ı çağırır ve 73DB'de kendi yeniden giriş bayrağı vardır.
- İsteğe bağlı olarak 0x7485 = 29829 → 40.0 Hz, yapılandırma sözcüğü e7f0'ın 0x80 biti ayarlıysa: NSES `0D42:0093` EMSSaveState, `cdiPoll`, EMSRestoreState çalıştırır. Bu, IRQ kullanmayan sürücüler için yoklama yoludur.
- Dolayısıyla PIT yaklaşık 100 Hz'de çalışır.

**DSMI zamanı** 0.1 ms birimindedir ve her 100 Hz çağrısında 100 artırılır.

**AMP temposu** (çıkış örnekleme saatine bağlı değildir):
- tickHz = ⌊BPM·50/125⌋ (tam sayı bölmesi).
- tickLen = ⌊10000/tickHz⌋, 0.1 ms biriminde.
- `ampPoll` zamanın anlık görüntüsünü alır (LockDSMITime), ardından `(now − last) − k·tickLen > 0` olduğu sürece `ampTick` çalıştırır; karşılaştırma **katı** büyüktürdür ve her seferinde `last += tickLen` ilerletilir.
- Bu nedenle tıklar yalnızca 10 ms'lik IRQ sınırlarında, titremeyle (jitter) gerçekleşir. 125 BPM'de: 50 Hz, her ikinci IRQ'da bir tık.
- Kırpma tempoda duyulur:

| BPM | tickHz | tickLen (0.1 ms) | Gerçek tık hızı | Kesin hız |
|---|---|---|---|---|
| 120 | 48 | 208 | 48.08 Hz | 48 Hz |
| 144 | 57 | 175 | 57.14 Hz | 57.6 Hz |

- Taşıma (port) bu tam sayı aritmetiğini ve 100 Hz nicemlemesini yeniden üretmelidir (ya da aynı IRQ çizelgesini benzetmelidir).

**Karıştırıcı.** **MCP yazılım karıştırıcısı IST.EXE içinde değildir.** Yüklenen sürücünün içindedir: SDISB16, SDIPAS16 ve SDIWSS16, `mcpOpenDDS` ve DMA koduyla birlikte "MCP v3.5 - (C) 1992,1995 Otto Chrons" içerir. SDIGUS16, GUS donanımında karıştırır. EXE onunla yalnızca CDI ve DDS üzerinden konuşur (`MCPSampleRealAddress`, `SDIInitDevice/StartOutput/StopOutput`, `MCPDisableVirtualSamples`). Karıştırma hızı, ara değerleme (interpolation), ses düzeyi tabloları ve 8 bitlik işaret işleme tamamen .DSD içindedir. **Seste aslına uygunluk, `SDISB16.DSD`'nin tersine mühendisliğini gerektirir** (ya da referans hangi sürücüyse onun). SES.CFG, SDISB16.DSD'yi seçer.

**AMP sayısal ayrıntıları:**
- Nota başlangıcı frekansı = c2spd·semitone[note] >> 10; 2^(n/12) tablosu kullanılır.
- Kaydırmalar (slide) ve vibrato frekansı c2spd·6848/period olarak yeniden hesaplar; period 100..0xD5FF aralığına kırpılır.
- İki yol farklı yuvarlar.
- CDI'ye gönderilen ses düzeyi = vol·trackVol >> 6 (0..64).
- Vibrato: period ± (depth·sin)>>7. Tablo indisi (pos>>2)&31, pos += (param>>2)&0x3C.
- Tremolo (efekt 0x87; OpenMPT bunu tremor olarak ele alır) ses düzeyini ± (depth·sin)>>10 kadar değiştirir.
- Arpej, önceden hesaplanmış 3 frekans arasında tick%3 ile döner.
- Desen kırma (pattern break) parametresini yok sayar ve her zaman 0. satıra gider.
- Geriye doğru konum atlaması, döngü seçeneği ayarlı değilse yok sayılır.

### 8. Diğer aslına uygunluk notları

- **Kendini değiştiren kod (RTL):** `15C9:0306`, bir x87 varsa çağıranın `CD 34..3D` baytlarını `9B D8..DF` olarak yeniden yazar (FixFPU bunları zaten geri getirir). `15C9:0598` yığında kurduğu bir komutu yürütür.
- AMP satır efekti tablosu: indis 5 (efekt 0x85) tek başına duran bir `RETF`'e (0EF6:03BD) işaret eder; bu çökmeye yol açar. `ampDoCommand` indis 0x18'i (efekt 0x98) kabul eder; bu da tablonun ötesindeki koda (0x00C8) okur. İkisi de dağıtılan AMF dosyalarında geçmez.
- `dsmiMemCmp`, tek fark son bayttaysa "eşit" bildirir (JECXZ tuhaflığı).
- `dsmiStrCmp` düz bir uyuşmazlık için hiçbir zaman negatif değer döndürmez.
- AMP, bir IRQ0 işleyicisi içinde özel bir yığında (DGROUP:7BE4) çalışır. Genel durumuna hem kesme anında hem de ana akıştan (ampPlayModule, ampStopModule, ampSetTempo ve diğerleri) CLI olmadan dokunulur. Yeniden giriş yalnızca bayraklarla korunur.
- `ampPlayRange`, modül türü 2 için yapılan bir sınamanın iki dalında da 7C66=0x40 ayarlar (ölü kod).

### 9. Açık sorular

- Aynı IRQ içinde DSMI zamanı yuvası AMP yuvasından önce mi çalışır? Bu, ovl559A ile ovl55CC arasındaki kayıt sırasına bağlıdır ve tık evresini bir IRQ kadar değiştirir.
- Örneklerin işaretli olup olmadığı ve karıştırıcının nasıl ara değerleme yaptığı: yanıt .DSD'den çıkarılmalı.
- Yazılım öykünücüsünün seg1351'deki INT 34h–3Dh giriş ofsetleri (RTL başlangıç kodu tarafından kurulur) henüz adlandırılmadı.
- seg08D0: hangi modüle ait olduğunu doğrulayın (kalıcı 3DQUICK.L mi, ayrı bir çokgen modülü mü). Geri derledikten (decompile) sonra 000C'ye kesin bir ad verin.
- `EMSLockRegion` (1154:07F4) ve `EMSHeapAvail` (1154:0C4D): kesin anlamları.

## English

Every function listed in `lib.tsv`. All DGROUP offsets are in segment 16E0.

### 1. What each segment is

| Seg | Size | What it is | Origin |
|---|---|---|---|
| 0DF2 | 0xAC0 | **DDS**, the dynamic driver system: name→function and name→resource registries, plus the loader for the `.DSD` drivers | DSMI (Otto Chrons) |
| 0DDF | 0x130 | Small pascal string and memory helpers that DDS uses (strcpy, strcmp, strlen, memcpy, memcmp, memset, abs) | DSMI |
| 0E9E | 0x105 | `MemAlloc/MemCalloc/MemRealloc/MemFree`. These wrap Borland farmalloc, farcalloc, farrealloc and farfree, and DDS exports them by name | DSMI |
| 0EAE | 0x48A | **CDI**, the channel distributor: `cdiInit`, `cdiRegister` and 26 thin dispatch wrappers | DSMI |
| 0EF6 | 0x1659 | **AMP 2.00** module player. Data tables sit at 0x000A–0x039A ("AMP 2.00 - (C) 1992,1994 Otto Chrons" and "MCPSampleRealAddress"). **The MCP mixer is not in this segment** | DSMI |
| 105B | 0x945 | AMP's AMF loader: `ampLoadModule`, `ampLoadAMF`, `ampLinkTracks` | DSMI |
| 10EF | 0x2C3 | **Timer Service v2.0**, "(C) 1993-1995 Otto Chrons": hooks INT 08 and reprograms the PIT | DSMI |
| 111B | 0x19D | `ampFreeModule` | DSMI |
| 1134 | 0x114 | Low-level EMS 4.0 calls (INT 67h) | DSMI |
| 1145 | 0xF1 | DSMI time base: `GetDSMITime`, `LockDSMITime`, `UnlockDSMITime` and the 100 Hz incrementer | DSMI |
| 1154 | 0xD7A | EMS heap: `EMSAlloc/EMSFree/EMSCopyTo/EMSCopyFrom/EMSCopy/EMSLock/EMSSaveState/EMSRestoreState`, exported through DDS. **MEMMAN (seg09D4) also uses it**, so the game's EMS memory goes through DSMI's EMS heap | DSMI |
| 2599 | 0x675 | Hand-written 386 graphics kernels: scaled, transparent and mirrored blits, 32-bit memset and memcpy, blend-table blending and a 2× view upscaler. See §6 | author (ALTDUZEY asm) |
| 08D0 | 0x1046 | 3D polygon code: builds wall quads, rotates them, clips them and rasterises them. All calls come from ovl2700 `F_2700_8197/8896`. Probably the resident part of 3DQUICK.L (q3d_) | game / engine |
| 1351 | 0x2776 | Borland **8087 software emulator** (EMU). Its state lives at SS:0000 (status, control, emulated stack pointer at SS:[0Ch], 12-byte temp reals). Its INT 34h–3Dh entry points are reached through vectors, not calls | Borland RTL |
| 15C9 | 0x5C8 | Borland **real-x87 support** (FP87). 0x0000–0x0033 hold 80-bit constants (π/4, π/2 …). 0x0034–0x02EF are x87 transcendental kernels (fptan, fpatan, f2xm1, fyl2x, each prefixed by 9B). Also the INT 34h–3Dh **fixup handler** and the x87 exception handler. The "bosch.cmp" hit is a false string-ref: `MOV DX,0x269B` is code bytes in `fp87_reexec_on_stack` | Borland RTL |

Who calls what: overlay `ovl55CC` (`T:ALTDUZEY\OSES.L`, which another group owns) is the sound initialisation. It runs DDSRegisterFunction(MCPDisableVirtualSamples, GetEnvString, SetVector), cdiInit, DDSLoadDriver(SES.CFG name at e85c), then DDSAccessFunction(SDIInitDevice, SDIStartOutput, SDIStopOutput), ampInit, tsAddRoutine and cdiSetupChannels. `ovl559A` sets up the DSMI time base.

### 2. DDS (seg0DF2)

Globals:
- `e776` (word): next driver handle, starts at 1.
- `e778`: magic 0xBEEF once initialised.
- `e77a`: resource list sentinel (far ptr).
- `e77e`: function list sentinel.
- `e782`: driver list sentinel.

All three lists are circular singly-linked lists closed by their sentinel node.

The DDS functions are pascal far calls. They return 0 on success, otherwise an error code: 1 = NULL argument, 2 = DDS not initialised or no memory, 3 = cannot open file, 4 = bad header, 5 = driver init failed, 6 = unknown handle, 7 = duplicate name, 8 = driver id already loaded, 9 = function not found, 10 = resource not found, 11 = driver refused to unload, 12 = out of memory.

Node layouts:
- function node (12 bytes): `{char far *name; void far *fn; node far *next}`.
- resource node (16 bytes): `{name; void far *ptr; long size; next @0C}`.
- driver node (24 bytes): `{word loaded(1=file,0=static); word deviceId; long handle; void far *entry; void far *imageAligned; void far *imageRaw; next @14}`.

**.DSD file format.** It is an MZ executable with "DS" (0x5344) as the magic. The loader uses these header fields:

| Offset | Field | Use |
|---|---|---|
| 0x02 | bytes in last page | image size |
| 0x04 | pages | image size |
| 0x06 | relocation count | relocations |
| 0x08 | header paragraphs | image start |
| 0x0A | min extra paragraphs | allocation |
| 0x14 | CS:IP | entry point |
| 0x18 | relocation table offset | relocations |
| 0x0C | device id | GUS16=0xC8, SB16=0x64, WSS16=0x65, PAS16=0x66 |
| 0x12 | version | 0x0100 |
| 0x1A | flags | bit0 must be 1 |

Load steps:
1. Image size = (pages−1)·512 + lastbytes − hdr·16 + 0x20.
2. Allocate size + minalloc·16 + 0x20 and align it to a paragraph.
3. Read the relocations, read the image and add the load segment at each relocation.
4. Call `entry(long cmd=1, DDSAccessFunction, long 0)`. DX:AX comes back with AX = error and DX = driver/device id.

Unload calls `entry(2,0,0)`.

### 3. CDI (seg0EAE)

Globals:
- `7694`: magic 0xDA.
- `7696[256]`: channel → device index.
- `7796 + n·0x68`: copies of the registered CDIDEVICE tables. There is room for 8.
- `7AD6`: device count.

`cdiRegister` gives each device 32 channels: device n owns channels n·32 … n·32+31.

CDIDEVICE has 26 far function pointers. Their order is confirmed by the `gus*` symbol names inside SDIGUS16.DSD:

`00 setSample, 04 playSample, 08 setVolume, 0C setFrequency(rate), 10 setLinearRate, 14 setPosition, 18 setPanning, 1C setMasterVolume, 20 mute, 24 unmute, 28 stopChannel, 2C muteAll, 30 unmuteAll, 34 download, 38 unload, 3C unloadAll, 40 getVolume, 44 getFrequency, 48 getPosition, 4C getPanning, 50 getSample, 54 setupChannels, 58 getChannelStatus, 5C poll, 60 setEffect, 64 getStatus`.

Each wrapper selects the device from the channel argument and forwards the remaining arguments. The calls are pascal with 32-bit arguments.

### 4. AMP player (seg0EF6) – data structures

**Code-segment tables (CS:)**

| Offset | Content |
|---|---|
| 0x45 | track·45 (offset into the track array) |
| 0x85 | track·26 (offset into the SAMPLEINFO array) |
| 0xC5 | 128 words, 2^(n/12)·1024 (0x3FF at n=60, 0x1000 at n=84, saturates at 0x8000) |
| 0x1CD | period table ×8: 0x1AC0 = 6848 at note 60, clamped to 0xFFD5 at the low end, 0x0097 at the top |
| 0x2CB | 64-entry half-sine table, 0..4095..0 |
| 0x34B | 13 words, 2^(k/12)·1024 (arpeggio) |
| 0x36B | 24-entry row-effect jump table for 0x80..0x97 |
| 0xDD0 | 12 {cmd, handler} pairs of tick effects |

**MODULE** (0x5C bytes, allocated by `ampLoadModule`; a copy is made at DGROUP 7BE8 while playing)

| Offset | Field |
|---|---|
| +00 | type (3 = AMF) |
| +01 | dword memory used |
| +05 | dword file size |
| +09 | name[32] |
| +29 | channel count |
| +2A | signed pan[32] (−63..63, 100 = surround) |
| +4A | instrument count |
| +4B | far INSTRUMENT* |
| +4F | order count |
| +50 | far ORDER* |
| +54 | word track count |
| +56 | far ptr table track[count+1]; index 0 = NULL |
| +5A | BPM (default 125) |
| +5B | speed (default 6) |

**ORDER** (0x82 bytes): `{word rows; void far *track[32]}`. The loader first stores track numbers, then `ampLinkTracks` turns them into far pointers.

**INSTRUMENT** (0x41 bytes, the same layout as on disk for v≥10)

| Offset | Field |
|---|---|
| +00 | type |
| +01 | name[32] |
| +21 | filename[13] |
| +2E | sample index, replaced by the far sample pointer; segment 0xFFFF means an EMS handle |
| +32 | dword length |
| +36 | word c2spd (rate) |
| +38 | volume 0..64 |
| +39 | dword loopStart |
| +3D | dword loopEnd |

**Track data** (one per stored track): a 3-byte header `{word count; byte 0}`, then `count` entries of `{row, cmd, arg}`. The track position starts at 3. Entry meanings:
- cmd < 0x7F: note, with arg = volume (0xFF = none).
- note 0: stop.
- note 0x7F: keep the current note.
- cmd 0x80: instrument (arg).
- cmd 0x81–0x97: effects.
- row 0xFF: end of track.
- the 496 entries with row FFh in the shipped files are the end-of-track markers (`FF FF FF`, docs/FORMATS.md "AMF"), not commands.

**Track state:** 32 × 45 bytes at DGROUP `7C91`.

| Offset | Field |
|---|---|
| +00 | far track ptr |
| +04 | pos |
| +06 | flags (bit1 = paused) |
| +08 | note |
| +09 | instrument |
| +0A | volume |
| +0B | word tick counter |
| +0D | word period |
| +0F | current cmd |
| +10 | param |
| +11 | word tone-porta target |
| +13 | tone-porta speed |
| +14 | tone-porta memory |
| +15 | porta memory |
| +16 | dword arpeggio freq[3] |
| +22 | ? |
| +23 | vibrato position |
| +24 | vibrato param |
| +25 | tremolo param |
| +26/+27 | tremolo speed/depth |
| +28 | tremolo position |
| +29 | offset memory |
| +2C | pan |

**Per-channel SAMPLEINFO** (26 bytes at DGROUP `8231 + ch·26`), passed to `cdiSetSample`:
- +00 far sample
- +04 length
- +08 loopStart
- +0C loopEnd
- +10 mode = 1
- +11 word sampleID (the EMS handle when the segment is 0xFFFF)
- +13 c2spd
- +15 dword current frequency
- +19 volume

**AMP globals:**

| Offset | Meaning |
|---|---|
| 7C4E | ampInit options |
| 7C50 | status (bit0 playing, bit1 paused) |
| 7C51 | play options (bit0 = loop) |
| 7C53 / 7C54 / 7C55 | start / end / current order |
| 7C56 | current track |
| 7C57 | row |
| 7C59 | last row |
| 7C5B | tick |
| 7C5C | orders far ptr |
| 7C60 | instruments far ptr |
| 7C64 | speed |
| 7C65 | BPM |
| 7C66 | 0x40 |
| 7C67 | sync |
| 7C68 | break flag |
| 7C6D | channel count |
| 7C6F[32] | pan |
| 7C8F | rows in the current pattern |
| 7C44 | 10-byte TRACKDATA returned by ampGetTrackData |
| 7BE4 | top of AMP's private ISR stack (old SS:SP saved at 7BE6/7BE4) |
| 8571[32] | per-track volume (default 0x40) |
| 8591 | AMP initialised flag |
| 859E / 859F | poll / interrupt re-entry locks |
| 85A0 | tick Hz |
| 85A2 | dword time of the last tick |
| 85A6 | far ptr to MCPSampleRealAddress (from the driver) |
| 85AA | default pans for old files: −63, 63, 63, −63 ×4 |

### 5. AMF on-disk layout (as `ampLoadAMF` reads it)

The magic is `"AMF"` followed by a version byte. The loader accepts versions 1, 8, 9 and 10–14 and returns −3 for anything else. All 15 files in `extracted/MUZIK` are version 14.

1. **Header** (75 bytes at v14): `"AMF" ver`, `name[32]`, `insCount u8`, `orderCount u8`, `trackCount u16`.
   - v≥9: `channels u8`, then `pan[16]` (pan[32] for v≥12). For v<11 the pans read from the file are **overwritten** by the default table at DGROUP 85AA.
   - v≥13: `BPM u8`, `speed u8`.
   - In `ampLoadModule`, v1 and v8 files are forced to 4 channels with the default pans.
2. **Orders** (orderCount of them): `rows u16` (v≥14 only; otherwise rows = 64), then `channels × u16 trackRef`.
3. **Instruments**, insCount × 65 bytes in the layout above. Versions before 10 use 59 bytes: the dword fields length, loopStart and loopEnd are u16, and loopEnd 0xFFFF means no loop.
4. **Track map:** trackCount × u16, giving the stored-track number for each logical track.
5. **Stored tracks:** `count u16`, `type u8`, then count×3 bytes. Version 1 has 3 extra bytes. Each track is allocated count·3+6 bytes.
6. **Sample data:** raw 8-bit, in sample-index order, with no conversion. Each sample gets a buffer of length + 16 bytes. Samples over 0x400 bytes go to EMS when the DDS functions "EMSAlloc" and "EMSCopyTo" exist; the sample pointer then becomes 0xFFFF:handle.

The shipped music uses effects 80, 81, 82, 83, 84, 86, 88, 89, 8C, 8D, 94 and 95 (tempo 120 and 144). It never uses 0x85 or 0x87.

### 6. asm2599 (hand-written 386 code in a data-class segment)

This is **not a mixer or a decompressor**. It is a set of graphics kernels that use 386 instructions: 32-bit registers, REP STOSD/MOVSD, ENTER/LEAVE, and LFS/LGS for the FS/GS segment registers.

**Data written into the code segment.** CS:0000–0017 are reserved zero bytes. The scaled blitters (0018 and 0144) store 16.16 x/y step values, the y accumulator, the row counter and the per-row DI adjust there, through `CS:` overrides.
- This is data written into the code segment. **No instruction bytes are patched**, so it is not self-modifying in the strict sense. In C these become locals.

**Entry points.** Six are called directly (0018, 028F, 02AF, 0440, 04AD, 0500; 0440 has no caller found). Three more are passed as far function pointers (`PUSH 0x2599 / PUSH off`, 102 sites) to the SYSTEMS.L blit dispatcher `0AC6:0430`:
- `02D0`: opaque rectangle copy (60 sites).
- `0341`: rectangle copy with a transparent key (36 sites).
- `03C4`: the same, horizontally mirrored (6 sites).

The argument frame for these three is `(src far, sx, sy, w, h, srcPitch, dst far, dx, dy, dstPitch[, key])`. They return 0xFFFF on success and 0 if `y·pitch+x` overflows 16 bits.

`0144`, `0234` and `0440` are unreferenced.

**Blend table** (passed as a far pointer, used by 0440, 04AD and 0500). It is a triangular table of 32640 bytes for unordered colour pairs a<b:
`idx = a·(509−a)/2 − 1 + b`. When the two colours are equal, the result is that colour.

**`0500`** upscales the 3D view 2×:
- Source: a 100-byte-pitch buffer starting at offset 0xCB.
- Pass 1 writes even screen rows 4,6,…,194, x = 6..190. Source pixels go to even x, and odd x is blend(left, right).
- Pass 2 writes the odd rows by blending vertically and diagonally (four neighbours).
- A source value of 0xFE means "keep the pixel already on screen".
- It writes straight to A000h.

**Bugs:**
- The error exits of 0018 and 0144 `RETF` without `LEAVE`, and they pop an unbalanced set of registers, so they would crash. They are only reached on 16-bit offset overflow.
- The error path of 03C4 pops SI and DS in the wrong order, which corrupts DS.
- On success, 0018 returns whatever AX held on entry.

### 7. Timing: what drives the music (fidelity-critical)

**Timer Service (seg10EF).** It hooks INT 08 and reprograms PIT channel 0 in mode 3 (`out 43h,36h`).
- **The master divisor is the smallest rate any slot has registered.**
- There are 16 slots of 14 bytes at DGROUP `85CE`: `{far isr; word rate; word step; word acc; byte active; byte ?}`.
- Slot 0 is the original BIOS handler with rate 0xFFFF.
- step = ((master<<16)|0xFFFF)/rate, or 0xFFFF for the slot that sets the master rate.
- On every IRQ0 the handler sends EOI, executes STI, and for each active slot does `acc += step`; on carry it calls the slot's routine as an interrupt (PUSHF+CALLF).
- Because the fastest slot's step is 0xFFFF and not 0x10000, that slot skips one call every 65536 IRQs. That is about every 11 minutes at 100 Hz.
- The BIOS clock is chained at roughly 1193182/65535 Hz.

**Rates the game registers:**
- 0x2E9B = 11931 → 100.007 Hz, used twice:
  - `1145:0096` (DSMI time += 100, registered by ovl559A).
  - NSES `0D42:0120`, which calls `ampInterrupt` and has its own re-entry flag at 73DB.
- Optionally 0x7485 = 29829 → 40.0 Hz, when bit 0x80 of config word e7f0 is set: NSES `0D42:0093` runs EMSSaveState, `cdiPoll`, EMSRestoreState. This is the poll path for drivers that do not use IRQs.
- So the PIT runs at about 100 Hz.

**DSMI time** is in 0.1 ms units and is incremented by 100 on each 100 Hz call.

**AMP tempo**, which is not tied to the output sample clock:
- tickHz = ⌊BPM·50/125⌋ (integer division).
- tickLen = ⌊10000/tickHz⌋ in 0.1 ms units.
- `ampPoll` takes a snapshot of the time (LockDSMITime) and then runs `ampTick` while `(now − last) − k·tickLen > 0`, using a **strict** greater-than, advancing `last += tickLen` each time.
- Ticks therefore fire only on 10 ms IRQ boundaries, with jitter. At 125 BPM: 50 Hz, a tick every second IRQ.
- The truncation is audible in tempo:

| BPM | tickHz | tickLen (0.1 ms) | Real tick rate | Exact rate |
|---|---|---|---|---|
| 120 | 48 | 208 | 48.08 Hz | 48 Hz |
| 144 | 57 | 175 | 57.14 Hz | 57.6 Hz |

- The port has to reproduce this integer arithmetic and the 100 Hz quantisation (or simulate the same IRQ schedule).

**Mixer.** **The MCP software mixer is not in IST.EXE.** It is inside the loaded driver: SDISB16, SDIPAS16 and SDIWSS16 contain "MCP v3.5 - (C) 1992,1995 Otto Chrons" with `mcpOpenDDS` and DMA code. SDIGUS16 mixes on the GUS hardware. The EXE only talks to it through CDI and DDS (`MCPSampleRealAddress`, `SDIInitDevice/StartOutput/StopOutput`, `MCPDisableVirtualSamples`). Mixing rate, interpolation, volume tables and 8-bit sign handling all live in the .DSD. **Audio fidelity requires reverse-engineering `SDISB16.DSD`** (or whichever driver is the reference). SES.CFG selects SDISB16.DSD.

**AMP numeric details:**
- Note-on frequency = c2spd·semitone[note] >> 10, using the 2^(n/12) table.
- Slides and vibrato recompute frequency = c2spd·6848/period, with period clamped to 100..0xD5FF.
- The two paths round differently.
- Volume sent to CDI = vol·trackVol >> 6 (0..64).
- Vibrato: period ± (depth·sin)>>7. The table index is (pos>>2)&31, with pos += (param>>2)&0x3C.
- Tremolo (effect 0x87, which OpenMPT treats as tremor) changes volume by ± (depth·sin)>>10.
- Arpeggio cycles through the 3 precomputed frequencies by tick%3.
- Pattern break ignores its parameter and always goes to row 0.
- Position jump backwards is ignored unless the loop option is set.

### 8. Other fidelity notes

- **Self-modifying code (RTL):** `15C9:0306` rewrites the caller's `CD 34..3D` into `9B D8..DF` when an x87 is present (FixFPU already restores these). `15C9:0598` executes an instruction it builds on the stack.
- AMP row-effect table: index 5 (effect 0x85) points at a lone `RETF` (0EF6:03BD), which would crash. `ampDoCommand` accepts index 0x18 (effect 0x98), which reads past the table into code (0x00C8). Neither appears in the shipped AMF files.
- `dsmiMemCmp` reports "equal" when the only difference is in the last byte (the JECXZ quirk).
- `dsmiStrCmp` never returns a negative value for a plain mismatch.
- AMP runs on a private stack (DGROUP:7BE4) inside an IRQ0 handler. Its global state is touched both at interrupt time and from the main line (ampPlayModule, ampStopModule, ampSetTempo and others) with no CLI. Re-entry is guarded only by flags.
- `ampPlayRange` sets 7C66=0x40 on both branches of a test for module type 2 (dead code).

### 9. Open questions

- Within the same IRQ, does the DSMI-time slot run before the AMP slot? It depends on registration order between ovl559A and ovl55CC, and it changes the tick phase by one IRQ.
- Sample signedness and how the mixer interpolates: answer from the .DSD.
- The software emulator's INT 34h–3Dh entry offsets in seg1351 (installed by the RTL start-up code) are not named yet.
- seg08D0: confirm which module it belongs to (resident 3DQUICK.L or a separate polygon module). Name 000C precisely after decompiling it.
- `EMSLockRegion` (1154:07F4) and `EMSHeapAvail` (1154:0C4D): exact semantics.
