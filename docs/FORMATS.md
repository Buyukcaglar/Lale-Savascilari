# Dosya biçimleri / File formats

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Çok baytlı tüm değerler küçük uçludur (little-endian). Her bölüm durumunu belirtir: **doğrulandı**
(gidiş-dönüş (round-trip) testinden bire bir geçer ya da kodla tamamen açıklanır), **kısmi** veya
**yapılacak**.

### FLiB arşivi (`*.CKD`) – doğrulandı

```
+0x00 char[4]  "FLiB"
+0x04 u32      kayıt sayısı
+0x08 u32      yük (payload) boyutu (bilgi amaçlı)
+0x0C kayıtlar, art arda:
      +0x00 char[4]  "FiLE"
      +0x04 char[32] ad, NUL ile doldurulmuş
      +0x24 char[16] ad yine (8.3)
      +0x34 u8       bayraklar (her zaman 0 = sıkıştırmasız saklanmış)
      +0x35 u32      verinin mutlak ofseti (= kayıt ofseti + 0x41)
      +0x39 u32      saklanan boyut
      +0x3D u32      özgün boyut (her zaman = saklanan boyut)
      +0x41 veri
kuyruk: tüm kayıt başlıklarının birebir kopyası (sayı × 0x41 bayt), yani oyunun
        (dosya boyutu − sayı×0x41) konumundan okuyabildiği bir dizin.
```

Araç: `tools/flib.py list|extract`. Kuyruk, satır içi başlıklarla karşılaştırılarak denetlenir.

**Oyun nasıl okur** (ovl569A/ovl5646): yalnızca sondaki dizini okur. Başlığın `+0x08` alanı
dizinin ofsetidir; bu her CKD için geçerlidir. Arama, arşivler açılış sırasıyla (0gfl, muzik,
sounds, frpos, ikon, ivir, savas, 3d) dolaşılarak yapılan doğrusal, büyük/küçük harf duyarsız
32 karakterlik bir ad karşılaştırmasıdır; bulunamazsa düz bir DOS dosyasına başvurulur.

### IST.EXE bindirme (overlay) bloğu (FBOV) – doğrulandı

`tools/exeload.py` içinde belgelenmiştir.

### CMP görüntüsü – doğrulandı (çözücü `tools/cmpview.py` içinde)

```
u16 genişlik, u16 yükseklik
EOF'a kadar RLE akışı:
  c >  0x80 : yineleme - sonraki baytın (c - 0x80) kopyası
  c <= 0x80 : düz      - sonraki c bayt (c = 0: işlem yok)
```
Pikseller 8 bitlik palet dizinleridir, satır öncelikli (row-major). Çözücüler: FILEMAN
`fm_cmp_decode` 5447:043B (< 64 KB) ve `fm_cmp_decode_huge` 5447:0000 (EMS penceresi / huge
işaretçiler).

**Aslına uygunluk (fidelity) notu:** 262 dosyanın 11'i `width*height` bayttan fazlasına açılır
(+1 bayt, IVIR/SCW.CMP için +320). Özgün program bu baytları görüntünün ötesine, MEMMAN bloğunun
artık alanına (slack) yazar. Port, belleği MEMMAN'ın yuvarlamasıyla ayırmalı ve taşmayı korumalıdır:
3D/GOTY0, GOTY1, VIEWPC; IVIR/ACILISA, ACILISB, IREM62, ISTB, RAKS, RPZ, SCW; SAVAS/IROSP1.

### GFL/GHD video – araçla doğrulandı (`tools/gflview.py`, oynatıcı `src/game/gfl.c`)

`0GFL.CKD` içinde 35 video (`extracted/0GFL/`), her biri bir `<name>.GHD` + `<name>.GFL` çifti.
`gflview.py check extracted/0GFL` aşağıdaki her ifadeyi 35 çiftin tümünde doğrular; kodek,
özgün çözücüyle bit bit karşılaştırılarak denetlenir (`tests/py/test_gfl.py`).
Tüm sayılar little-endian'dır.

**GHD** = başlık + sesin ilk saniyesi + kare dizini (dosya boyutu = `0x310 + abytes + 8*nframes`):

| ofs | boyut | alan |
|---|---|---|
| 0x000 | 3 | `"GFL"`. Oynatıcı yalnızca 3 *işaretli* baytın toplamının 0xD9 olduğunu denetler ("GFL versiyon hatası.") |
| 0x003 | u16 | nframes |
| 0x005 | u8 | fps – her dosyada 10 |
| 0x006 | u16 | abytes: 1 saniyelik parça başına ses baytı – her dosyada 22050 (8 bit işaretsiz mono, 22050 Hz, 0x80 = sessizlik); 0 = ses yok |
| 0x008 | u32 | her dosyada 100000, hiç okunmaz |
| 0x00C | 4 | 0 |
| 0x010 | 0x300 | başlık paleti, 256 × RGB, 6 bitlik VGA DAC değerleri |
| 0x310 | abytes | 0. saniyenin sesi |
| 0x310+abytes | 8·nframes | kare dizini |

Dizin kaydı (8 bayt, dolgusuz/packed): `u8 type, u16 vsize, u32 offset, u8 flags`.

* `type` 0: fark karesi (delta frame). 2: sesin bir sonraki saniyesini taşıyan kare – her
  `fps`'inci kare (0, 10, 20, …); videosu genellikle tam bir resimdir ama her zaman değil (SEQ5'te
  548 baytlık 2 tipi kareler var). 1: yalnızca palet içeren kare (video çözülmez; `vsize` 768 der
  ama ardından video baytı gelmez) – yalnızca SEQ5'in sonundaki kararma (37 kare). Oynatıcı kare
  atlarken type ≠ 0 olan her kareyi yeniden eşitleme noktası ("anahtar kare", key frame) sayar.
* `offset`: karenin yükünün GFL içindeki ofseti. `flags & 0x80` ise 0x300 baytlık bir palet hemen
  **önünde**, `offset - 0x300` konumunda durur.
* `flags`: bit 7 = palet parçası var. Bit 0–6 = müzik eşitlemesi: `n = (flags & 0x7F) >> 1`,
  bit 0 ayarlıysa işareti çevrilir; oynatıcı AMP `ampBreakPattern(n)`'i, ardından n∓1 … 0'a kadar
  çağırır (yani ses açıkken gösterilen her kare için bir kez `ampBreakPattern(0)`). Dağıtılan
  her dosyada 0x00 ya da 0x80'dir.

**GFL** = kare sırasıyla yükler: `[palette 0x300 if flags & 0x80] [audio abytes if
type == 2] [video vsize bytes, none for type 1]`, bitişik olarak:
`offset[i+1] = offset[i] + (type==2 ? abytes : 0) + (type==1 ? 0 : vsize) + (flags[i+1] & 0x80 ? 0x300 : 0)`,
ve son kare dosyanın sonunda biter (SEQ5'in son, yalnızca paletli karesi için
`offset` = dosya boyutu). 35 videonun 28'i palet parçası taşır (semt filmlerinin çoğunda 31'er,
SEQ4'te 20, SEQ5'te 58, SON'da 73); FUN, INTRO, SEQ1–3, UMR-D-IN ve UMR-D-OT'de hiç yoktur.

**Video kodeği** (`gfl_decode_frame` 5313:0390). Önceki kareyi tutan kalıcı, 320×200 8 bitlik bir
arabelleği (64000 bayt) günceller; bu arabellek hiçbir zaman temizlenmez. Her kod baytı b
için:

| b | işlem |
|---|---|
| `1nnnnnnn` | düz (literal): sonraki `(b & 0x7F) + 1` baytı kopyala |
| `01ffnnnn` | `L + 1` piksel atla |
| `00ffnnnn` | `L + 3` pikseli sonraki baytla doldur |

`L`, ff = 00, 01, 10, 11 için sırasıyla n, 2n + 0x10, 4n + 0x30, 8n + 0x70'tir; dolayısıyla
atlamalar 1–16, 17–47 adım 2, 49–109 adım 4, 113–233 adım 8, doldurmalar 3–18, 19–49/2,
51–111/4, 115–235/8'dir. Her koddan sonra çözücü `src < start + vsize` ve `dst < 64000` olduğu
sürece devam eder (do-while: vsize 0 için bile bir kod çözülür; bir dizi sonu aşabilir). Her
dosyanın her karesinde akış tam olarak tüketilir ve tam olarak 64000. pikselde biter.

**Oynatma** (ayrıntılar `src/game/gfl.c` ve `analysis/names/gameB.md` içinde): sırası gelen kare =
`(clock_ms - start) * fps / 1000`; palet parçaları kare gösterildiğinde ayarlanır, parçası olmayan
bir kare başlık paletini geri yükler (yalnızca bir parça paleti ayarlanmışsa); 2 tipi bir k
karesinin sesi, döngüyle çalınan 2 × abytes'lık bir örneğin `(k / fps + 1) & 1` yarısına gider.
Geciken kareler: atlanan karelerden sonra sırası gelen bir fark karesi gösterilmez ve akış bir
sonraki type ≠ 0 karesine atlar.

### Envanter notlarındaki biçimler – şimdi nerede oldukları

İlk envanter (`analysis/names/gameA.md`, `gameB.md`, `lib.md`) bunları listelemişti; artık her
birinin burada, geri derlenmiş (decompiled) koddan çıkarılmış bir bölümü var:

| Biçim | Bölüm |
|---|---|
| SAKLA###.SAV | "SAKLAnnn.SAV (kayıtlı oyun)" – doğrulandı |
| Harita bankaları ("AmBk", 15 baytlık hücreler) | "FRPOS olay betikleri PCn / PCSn" – doğrulandı (hücreler, olay anahtarları); haritanın 3..6. duvar takımı baytları: aşağıda "KOR" |
| FRPOS betikleri PCn/PCSn | "FRPOS olay betikleri PCn / PCSn" – doğrulandı |
| Savaşçı / şablon tabloları | "ISOSYS savaş durumu (DGROUP)" – kısmen doğrulandı |
| AMF modülleri | "AMF – DSMI modülü" – doğrulandı |
| DSD sürücü kabı ("DS") | "DSD – DSMI sürücüsü" – doğrulandı |

Dosya biçimi olmayan ve burada belgelenmeyenler: semt tablosu 16E0:2E31 (18 × 1Dh: "pcN" harita
adına, ilgi hâlindeki (genitive) ada, +08..+14'te gündüz-giriş/gece-giriş/gündüz-çıkış/gece-çıkış
dört GFL adına ve +18'de ufuk resmine uzak (far) işaretçiler, ardından +1C'de bir bayt; bkz.
`src/game/s3d.h` içindeki `S3D_DISTRICT_TAB`) ve üç DGROUP tablosunun alanları: canavar tipleri
1287 + 2Eh·t (23 word (16 bitlik sözcük); savaşın bunları şablonlara nasıl kopyaladığı yalnızca
ISOSYS bölümünde), silahlar 0E7F + 0Eh·k (7 word; word 0 = karşısında +1 isabet ve +2 hasar
kazandırdığı canavar tipi, `iso_combat.c`) ve vecizeler 007E + 16h·s (11 parametre word'ü,
`iso_spell.c` içindeki `VECIZE_PARAM`). Alanlarının çoğunun anlamı yalnızca
`analysis/names/gameB.md` içinde tahmin edilmiştir ve denetlenmemiştir.

### TRDOS.DAT – doğrulandı (`o559A_load_trdos_font` 559A:01F3, tests/py/test_fatal.py)

4096 bayt = 256 glif × 16 satır, piksel başına 1 bit, MSB = en soldaki: 8×16 kod sayfası 857
(Türkçe DOS) metin kipi yazı tipi (örn. 98h glifi İ'dir). IST.EXE'nin yanında duran düz bir DOS
dosyasıdır, `fopen("TRDOS.DAT", "rb")` ile açılır (CKD arşivleri üzerinden değil).
`o559A_load_trdos_font` 1000h baytı malloc ile ayrılmış bir arabelleğe okur ve INT 10h
AX=1100h ile VGA karakter üretecine yükler (BH=10h glif başına bayt, BL=0 blok 0, DX=0'dan
başlayarak CX=100h glif, ES:BP = arabellek), sonra arabelleği serbest bırakır. Başlangıçta
`ist_main`, metin kipi 3'e döndükten sonra da `o559A_fatal` tarafından çağrılır; böylece çıkış ve
hata iletileri Türkçe harfleri gösterir. Eksik dosya yalnızca "Turkce Dos Fontu bulunamadi."
yazdırır; kısa bir dosya, yazı tipinin geri kalanında arabelleğin eski baytlarını bırakır.

### PAL – doğrulandı (veri tarafı)

768 bayt = 256 × (R, G, B), 6 bitlik VGA DAC değerleri (0–63). Dört adet 48 baytlık dosya
16 renklik kısmi paletler tutar.

### PUR / SND – doğrulandı (yükleyici ve oynatıcılar; DEMOLAR'daki çağıranlar DOSBox-X'e karşı: hikâye slayt gösterisinde para00.snd, docs/VERIFY.md play8; yeni oyun girişinde buun.pur, play3/playq)

Başlıksız, işaretsiz 8 bit mono PCM (sessizlik = 0x80; MCP karıştırıcısı 0 baytını −128 olarak
okur). SOUNDS.CKD 196 `.PUR` dosyası (efektler, konuşmalar) ve `PARA00.SND`..`PARA09.SND`
(anlatım, 125–489 KB) içerir. Dosyadaki hiçbir şey örnekleme hızını vermez: her çağıran bir hız
geçirir.

**Yükleme** (`oses_load_sample` 55CC:01F3, gerçek dosyalarla tests/py/test_sound.py): dosya n
örnek yuvasına (sample slot) gider (0..30h; kayıt DE34 + 17h·n = MEMMAN referansı, +4'ten
itibaren SAMPLEINFO); buradaki çağıranların hepsi EMS'e (tip 1), en çok FF00h baytlık parçalar
hâlinde yükler. flags bit 1 ile geri kalanı n+1, n+2 … yuvalarında (30h'e kadar) devam eder;
sonuncusu dışında her biri ses tablosunda 200h ile işaretlenir; bu bit yoksa yalnızca ilk parça
yüklenir. flags bit 0 her parçayı ters çevrilmiş olarak saklar (tablo bayrağı 100h).

**Ses tablosu** 16E0:22DB + 11h·n, n = 0..32 (INTRFACE tarafından; `src/engine/intrface.h` içinde
`g_snd_name`/`g_snd_rate`): `char name[13]; u16 rate; u16 flags`. `ui_load_voice(n, rate, …)`
(05DC:192D) n yuvasını istek üzerine yükler ve son iki kanalda çalar; `iso_play_sfx`,
3DSYS, kapı animasyonu (yuva = duvar takımı + 0Dh) ve FRPOS op 12 (yuva 9) tablodaki hızı geçirir.

| n | ad | hız | bayraklar (hex) | n | ad | hız | bayraklar (hex) |
|---|---|---|---|---|---|---|---|
| 0 | kufurbuf | 21000 | 03 | 16 | kapi1.pur | 11000 | 15 |
| 1 | adim1.pur | 4000 | 37 | 17 | kapi4.pur | 22000 | 15 |
| 2 | olum.pur | 44100 | 23 | 18 | kapi5.pur | 11000 | 15 |
| 3 | vurus.pur | 8000 | 13 | 19 | kapi6.pur | 11000 | 15 |
| 4 | vurama.pur | 7500 | 13 | 20 | kapi7.pur | 10000 | 15 |
| 5 | iska.pur | 6000 | 13 | 21 | merdalt.pur | 32000 | 04 |
| 6 | adim2.pur | 11000 | 37 | 22 | merdust.pur | 32000 | 04 |
| 7 | adim3.pur | 40000 | 37 | 23 | kanat.pur | 44000 | 33 |
| 8 | uzmen.pur | 20000 | 13 | 24 | surt.pur | 30000 | 33 |
| 9 | bhasar.pur | 34000 | 07 | 25–28 | sw1..sw4.pur | 11000 | 23 |
| 10 | bosch.pur | 21000 | 07 | 29–32 | fw1..fw4.pur | 44100 | 23 |
| 11 | adim5.pur | 40000 | 37 |  |  |  |  |
| 12 | adim8.pur | 44000 | 37 |  |  |  |  |
| 13 | viyk.pur | 8500 | 33 |  |  |  |  |
| 14 | kapi1.pur | 11000 | 15 |  |  |  |  |
| 15 | kapi2.pur | 11000 | 15 |  |  |  |  |

Değerler IST.EXE'nin DGROUP'undan okunmuştur (33..48 kayıtları boştur). Bayraklar: 1 hiç serbest
bırakılmaz; 2 / 4 `ui_init_voices`'ın konuşma kipleri için yuvayı seçer (kip 0, 4'ü olmayan
yuvaları, kip 1 2'si olmayanları, kip 3 ikisi de olanları yükler); 8 devam parçalarını da çal ve
bekle; 10h rastgele perde, `rate + (rand()·(rate/5) >> 15) − rate/10` (±%10; kaydırılmış çarpım
16 bite kesilir); 20h seslendirilmiş: savaşta
`iso_play_sfx` (ovl4595:214E) hızı konuşanın ses sınıfına göre ölçekler; 100h/200h çalışma
zamanında ayarlanır (yukarıda).

**Ses sınıfı hızı** `iso_sfx_rate(who, base)` (ovl4595:255B, tests/py/test_iso.py):
`r · base / 7500` (u32, FFFFh ile sınırlı); r, 16E0:1E8D + who (canavar tipi) bayt tablosundan:
sınıf 0/2 6000, 1 7500, 3 7000, 4 6500, 5 4500, 6 20000, 7 8500, 9 44000, diğerleri 22000; ekip
(party) (who > 40) 7500. Vecize sesleri (`vecizeNN.pur`, `fecizeNN.pur`, `cevapNN.pur`,
`fevapNN.pur`) 21h yuvasına yüklenir ve `iso_sfx_rate(speaker, 44100)` hızında çalınır
(`iso_speak_vecize`, tests/py/test_iso_d.py test_sound_speak); `ui_play_voice` devam
parçalarını aynı hızda çalar.

**Diğerleri** (henüz testlerle kapsanmıyor): `PARA*.SND` - `demo_story_scene` (3C2D:1108) her slayt
için `paraNN.snd`'yi flags 2 ile 21h yuvasına yükler (EMS parçaları 21h.. yuvalarında) ve
4E52h = 20050 Hz'de çalar (`ui_load_voice(21h, 20050, 0, 0)`, ardından `ui_play_voice`); son
hane (adın 5. baytı) her slaytta bir artırılır ve hiç sıfırlanmaz. `buun.pur` (yeni oyun girişi,
`demo_load_sounds`) döngü sonu 46E3h ile 2Fh yuvasına gider ve 2134h = 8500 Hz'de çalınır. Akışlı
oynatıcı `snd_load_sound` (0D42:01F0, 30h yuvasında 60000 baytlık halka arabellek) listelemede
hiçbir yerden çağrılmaz.

### RES – doğrulandı

Düz metin, CRLF ile ayrılmış dosya adları. Bunlar bir arşivin önceden yüklediği kaynakların
listeleridir (manifest) ("EMS statik bloklar"), örn. `3D.RES`, `3DEM.RES`, `SAVDEM.RES`, `IDEM.RES`.

### THANX.DOC / DEBUG.LOG

cp857 metin: emeği geçenler (credits) ve ALTDUZEY'den kalmış bir geliştirici heap/hata ayıklama
günlüğü.

### DSD – DSMI sürücüsü ("DS" dosyası) – doğrulandı (`DDSGetDriverInfo`/`DDSLoadDriver`, src/dsmi/dds.c)

`SDISB16.DSD`, `SDIGUS16.DSD`, `SDIPAS16.DSD`, `SDIWSS16.DSD`: imzası `DS` olarak değiştirilmiş ve
iki başlık alanı başka amaçla kullanılan bir DOS MZ yürütülebilir dosyası. `DDSLoadDriver`
(0DF2:0611) tarafından far heap'e yüklenir ve bir EXE gibi yeniden konumlandırılır (relocation);
bir DOS programı değildir. 28 baytlık başlık; aksi belirtilmedikçe tüm alanlar little-endian u16:

| Ofs | MZ adı | DDS için anlamı |
|---|---|---|
| 00 | e_magic | `"DS"` (5344h); başka bir değer: hata 4 |
| 02 | e_cblp | son 512 baytlık sayfadaki bayt sayısı |
| 04 | e_cp | sayfa sayısı |
| 06 | e_crlc | yeniden konumlandırma sayısı (0 yüklemeyi başarısız kılar: farcalloc(0) = NULL, hata 12) |
| 08 | e_cparhdr | paragraf cinsinden başlık boyutu (= yükleme modülünün dosya ofseti / 16) |
| 0A | e_minalloc | görüntünün ardından ayrılan ek paragraflar |
| 0C | e_maxalloc | **aygıt kimliği** (SB16 64h, WSS 65h, PAS 66h, GUS C8h); `DDSGetDriverInfo` word 0 |
| 0E, 10 | e_ss, e_sp | 0 (kullanılmaz) |
| 12 | e_csum | **sürüm**, dördünde de 0100h; `DDSGetDriverInfo` word 2 |
| 14 | e_ip, e_cs (u32) | yükleme segmentine göre giriş noktası CS:IP (dördünde de 0000:0000) |
| 18 | e_lfarlc | yeniden konumlandırma tablosunun dosya ofseti (20h) |
| 1A | e_ovno | **bayraklar**: bit 0 ayarlı olmalı ("yüklenebilir"), yoksa hata 4; `DDSGetDriverInfo` word 1 |

Yükleme (özgündeki gibi 16 bit aritmetikle):
`image = ((e_cp - 1) * 512 + e_cblp - e_cparhdr * 16 + 32) & 0xFFFF` bayt,
`e_cparhdr * 16` ofsetinden okunur (yükleme modülünden 32 fazla - okuma yalnızca kısa kalır);
blok `image + e_minalloc * 16 + 32` bayttır ve görüntü bloğun ilk paragraf sınırından başlar.
Yeniden konumlandırmalar `e_crlc` × {u16 off, u16 seg} biçimindedir;
`image + ((seg * 16 + off) & 0xFFFF)` konumundaki word'e yükleme segmenti eklenir.

Giriş protokolü (far pascal `long entry(long cmd, void far *arg, long arg2)`, sonuç DX:AX):
`arg = DDSAccessFunction` ile `cmd 1` - sürücü ihtiyaç duyduğu DSMI hizmetlerini adlarıyla
arar; AX = 0 tamam (yoksa DDS hatası 5), DX = aygıt kimliği (zaten yüklü bir kimlik: hata 8).
`cmd 2` (boşaltma, `arg = 0`): sıfırdan farklı = reddedildi (DDS 11 döndürür ama yine de boşaltır).

| Dosya | Boyut | e_cp/e_cblp | reloc | başlık paragrafı | minalloc | kimlik |
|---|---|---|---|---|---|---|
| SDISB16.DSD | 29581 | 3Ah/18Dh | 13Ah | 51h | 12h | 64h |
| SDIWSS16.DSD | 25107 | 32h/13h | BCh | 31h | 12h | 65h |
| SDIPAS16.DSD | 21341 | 2Ah/15Dh | 88h | 24h | 11h | 66h |
| SDIGUS16.DSD | 13147 | 1Ah/15Bh | AEh | 2Eh | 2Fh | C8h |

#### DSD içeriği (SDISB16.DSD) – doğrulandı (`tools/dsdload.py`, `dsd_main`'i Unicorn'da çalıştırır)

İki segment: görüntü +0'da kod (ofset 0'da `dsd_main` 147Ch'ye `JMP FAR`) ve görüntü +3D30h'de veri
segmenti 03D3h; 314 yeniden konumlandırmanın tümü bu iki segment değerinden birini yamalar.
Assembler **yordam adlarını** kodda bırakmıştır: her yordamın önünde ASCII adı ve bir uzunluk
baytı bulunur (`...mcpOpenDDS` `0A`, ardından kod gelir); `tools/dsdload.py` bunları listeler
(mcpOpenDDS, scanLong, detectDevices, cmdSB, resetDSP, SB16IRQHandler, initDevice, startOutput,
getBufPos, stopOutput, closeDevice, dsd_main, DMAInit…DMAStatus). MCP karıştırıcısının (görüntü
1675h–36EFh) adları yoktur.

`dsd_main(1)` DDSAccessFunction üzerinden şunları içe aktarır: DDSAccessResource,
DDSRegisterResource, DDSRegisterFunction, DDSRemoveFunction, DDSRemoveResource, MemAlloc,
MemCalloc, MemRealloc, MemFree, cdiRegister, SetVector, GetEnvString (sonuç d:31FE'deki bir
bayrağı seçer), ardından mcpOpenDDS aracılığıyla GetDSMITime, MCPSampleRealAddress,
MCPEnable/DisableVirtualSamples ve SDIGetBufferPosition. Dışa aktardıkları: SDIDetectDevices
(01DE), SDIQueryDeviceCount (03F6), SDIQueryDevice (0406), SDIInitDevice (08F6),
SDIStartOutput (105C), SDIGetBufferPosition (1382), SDIStopOutput (13A0), SDICloseDevice
(140A) ve `MCPOutputBuffer` kaynağı (d:087C, 12h bayt: u16 seg, u32 off, u32
linear, u16 size, u16 size, u32 rate, u8 flags). SDIInitDevice, `cdiRegister`'ı d:088F'deki CDI
tablosuyla çağırır (26 uzak işaretçi, bkz. docs/DSMI_MCP.md). `dsd_main(2)` SDI işlevlerini
kaldırır ve üçüncü argümanını döndürür; başka her komut 1 döndürür.

#### SES.CFG – doğrulandı (oses_init 55CC:0000, SDISB16 initDevice)

107 bayt: `char[13]` sürücü dosya adı (`SDISB16.DSD`), DGROUP E7EC'ye kopyalanıp SDIInitDevice'a
geçirilen 5Ch baytlık **SDI yapılandırma** kaydı, ardından DGROUP 8DDA'daki `u16` (2).
SDI yapılandırması, u32 alanlar: +00 kart tipi (1 SB, 2 SB Pro, 6 SB16; diğerleri: başlatma
başarısız olur), +04 bayraklar (bit 7: IST.EXE kendi 40 Hz'lik cdiPoll zamanlayıcı yordamını
ekler), +08 G/Ç portu, +0C DMA kanalı, +14 IRQ, +10/+18..+20 kurulum programından kalmış eski uzak
işaretçiler, +40 yükseltme (0 yok, 1 otomatik, 2 düşük, 3 orta, 4 yüksek), +44 stereo/kip seçeneği
(0 = SB Pro/SB16 için stereo; SB: hız dizini), +48 hız dizini (SB16: 44100, 32000, 22050,
16000), +58 çağıranın sağladığı çıkış arabelleğine uzak işaretçi (0). Dağıtılan dosya: 220h'de
SB16, DMA 5, IRQ 7, yükseltme yok, stereo, 22050 Hz → 16 bit stereo 22050 Hz, 8016 baytlık
halka arabellek.

### GRP.DAT / GRPHAZIR.DAT (ekip dosyası) – doğrulandı (`src/game/chargen.c`, tests/py/test_chargen.py)

Karakter oluşturmanın sonunda `cg_save_party` (39E5:182E) tarafından yazılır, `cg_load_party`
(39E5:1932) tarafından okunur; GRPHAZIR.DAT, "çabuk başla"nın hazır ekibidir.
664 bayt, başlık yok, little-endian, 1..4 numaralı üyeler (DGROUP'un 0. kaydı saklanmaz):

| dosya ofs | boyut | içerik |
|---|---|---|
| 74·(i-1) | 37 | ad, NUL ile sonlanan (PartyName +00, DGROUP 9C26+94h·i) |
| 74·(i-1)+37 | 37 | vecize (aphorism) bayrakları '1'/'0' ×36 + NUL (PartyName +6F); "1000…0" olarak yazılır |
| 296+92·(i-1) | 92 | PartyStats kaydı olduğu gibi (DGROUP 99A6+5Ch·i), düzeni `src/game/chargen.h` içinde |

Oluşturmada kullanılan istatistik word'leri: +02 ırk 1..8, +04 cinsiyet 1/2, +0A..+20 12
özellik, +22, +24 boy, +26 kilo, +28, +2C ırka özgü, +38 = +0A ve +3A (sağlık) = +0C yazmadan
hemen önce ayarlanır, +42 portre 1..16.  Yüklemede ırk adı (ad kaydının +4A'sı) 16E0:2638'deki
11 baytlık tablodan doldurulur.  Oyunun hiç ayarlamadığı baytlar DGROUP'ta nasılsa öyle yazılır
(yeni oyunda sıfır).

### KOR – koordinat tabloları – doğrulandı (`tools/fmtcheck.py kor`: 59 dosyanın tümü)

Birbiriyle ilgisiz iki düzen aynı uzantıyı paylaşır ("Kordinat dosyası"). CKD araması bulamazsa
düz bir DOS dosyasına başvurur; `orig/BOSCH.KOR` bu yolla bulunur.

#### 3D/*.KOR – duvar takımı doku dikdörtgenleri (`q3d_load_kor` 2700:1E06, tests/py/test_q3dview.py test_load_kor)

Bir semte girerken `s3d_load_district`, dört harita baytı 3..6'nın (16E0:9F10 + 2i'de tutulur;
0 = yok; ad tabloları 9 = merdust/merdalt'a kadar gider) her birinden duvar takımı numarası N'yi
alır ve `duvarN.kor`'u i KOR yuvasına, `kapiN.kor`'u 4 + i yuvasına yükler (16E0:B3E8 + 0Eh·yuva).
Her çağrıdan önce yuvayı duvarlar için sürüm 1, n 6, {hdr0, w, h} = {0, 200, 200}, kapılar için
{0EBh, 200, 200} olarak önceden ayarlar. Dosyanın bulunmaması olağan durumdur: yuva o zaman
varsayılan parça tablosuyla çizilir (16E0:B72A, 37 × 12h bayt).

| ofs | boyut | alan |
|---|---|---|
| 0 | i16 | sürüm; 1'den başka her değer ölümcül hatadır (satır 1F7h, "Kordinat dosyasi versiyonu desteklenmiyor") |
| 2 | u16 | n = ardından gelen başlık baytları; denetlenmeden yuvanın +4'ünden itibaren okunur (n > 6 yuvanın parça işaretçisinin üzerine yazar) |
| 4 | n | i16 hdr0, i16 w, i16 h: w × h = kayıtların hazırlandığı görünüm boyutu |
| 4+n | 37 × 14 | her duvar parçası i için bir kayıt: i16 tx, ty, tw, th, dx, dy, flags |

Kayıtlar yalnızca `filelength − 4 > n` ise okunur; o zaman yuva, varsayılan tablonun bir kopyasını
alır; bu kopyada i parçası kaynak dikdörtgeni (tx, ty, tw, th), w × h = tw × th boyutunu, konumuna
eklenen (dx, dy) ofsetini ve `flags`'i (bit 0 aynalanmış, bit 1 ölçeklenmiş) alır ve görünüme göre
kırpılır. Görünüm (düşük ayrıntıda 100×100, 200×200) başlığın w × h'sinden farklıysa her parçada
bayrak 2 ayarlanır, boyut ve ofset de varsayılan parçanın w/tw ve h/th oranıyla ölçeklenir
(tuhaflıklar (quirks): bkz. `q3d_load_kor`). Başlıktan sonradan yalnızca hdr0 kullanılır:
hdr0 = 0 olan bir yuvanın duvar parçaları `q3d_draw_strip_a` ile, aksi hâlde `q3d_draw_strip_b`
ile çizilir (kuzeye ya da batıya bakarken yan parçalar dokularını 0EBh piksel daha sağdan alır;
2/3 yanlarında izleyiciye bakan duvarlar karşı parçanın dikdörtgenini aynalanmış olarak ödünç
alır); kapı animasyonunun 2/3 bakış yönü için aynalaması da hdr0 ≠ 0'ı sınar
(`src/game/q3dview_anim.c` içinde `door_mirror`).

Dağıtılan dosyalar; hepsi sürüm 1, n 6 ve başlık {0, 200, 200}:

| dosya | boyut | içerik |
|---|---|---|
| DUVAR4, DUVAR8 | 528 | başlık + 37 kayıt (74 parça: flags 0 ×41, 1 ×16, 2 ×17) |
| KAPI4, KAPI5, KAPI6, KAPI8 | 10 | yalnızca başlık: yuva varsayılan tabloyu korur, ama hdr0 kapı ön ayarı 0EBh yerine 0 olur |

#### SAVAS/*.KOR, BOSCH.KOR – sprite kare kutuları (`iso_draw_sprite` ovl4595:0387, tests/py/test_iso.py, tests/py/test_chargen.py)

72 bayt = 6 kayıt × 6 i16: `x1, y1, x2, y2` (karenin sprite sayfasındaki (sprite sheet)
`<name>.CMP` sınırlar dâhil kutusu), `ax, ay` (çapa, anchor). Sayfa pikseli (ax, ay), birim konumu
(x, y) için (x − 4, y − 8)'e çizilir; aynalanmış kareler bunu korur (kutu onun etrafında
çevrilir). Birim karesi f = 1..8 (bakış yönü 1..4, +4 = saldırı; ölüm karesi +100 yalnızca konumu
hesaplar), f = 1..3 için `f − 1`, f = 5..7 için `f − 2` kaydına, f = 4 / 8 için aynalanmış 1 / 4
kayıtlarına eşlenir. HP çubuğu her iki eksende de kutu merkezi − 13'e (0Dh) konur.

ISOSYS (`iso_load_battle_gfx` ovl4595:0570) bir savaşın yedi sprite'ını (ekip yuvaları 0Bh..0Eh
portre adlarından, ya da olay bayrağı 8Dh ayarlı ve 8Fh temizken dördü için de `karafa`; canavar
grupları 0Fh..11h 16E0:2185 + 9·tip kısa adlarından, o ad boşsa `bosch`) ve her birinin `.KOR`'unu
1F8h baytlık bir bloğa (16E0:E337) 48h·(yuva − 0Bh) konumuna yükler; CHARGEN
(`cg_load_char_graphics` 39E5:011F) 48h baytlık bir bloğa bir tane yükler. Yükleyici dosyaları
bütün olarak okur: daha büyük bir `.KOR` sonraki sprite'ın kaydına taşar. Dağıtılan: SAVAS.CKD
içinde 52 dosya ve `orig/BOSCH.KOR` (resim `IVIR/BOSCH.CMP`), hepsi 72 bayt, her kutu kendi
CMP'sinin içinde. 3. kaydın (saldırı, bakış 1) çapası 19 dosyada kutusunun dışında kalır
(BOLONEZ, CEHALET, …). `.KOR`'u olmayan SAVAS resimleri sprite değildir: ANAKES,
DUVARIC1..9/A/B, TABLETUR, YERICON.

### RIX – karışım tablosu – doğrulandı (src/engine/blit386.c içinde `blend_lookup`, tests/py/test_blit386.py, tests/py/test_graphics.py test_rix; `tools/fmtcheck.py rix`)

32640 bayt = 256·255/2, başlık yok: farklı her renk çifti a ≠ b için, %50 karışımlarının rengi.
Paketlenmiş üst üçgen, satır satır: lo = min(a, b), hi = max(a, b) ile kayıt
`lo·(509 − lo)/2 − 1 + hi` konumundadır (16 bit); yani lo satırı (lo, lo+1) … (lo, 255)'i tutar:
(0, 1) 0'da, (0, 255) 254'te, (1, 2) 255'te, (254, 255) 32639'da. blend(a, a) = a, tabloya
bakılmadan.

Kullananlar (elle yazılmış 386 kodu, 2599): `asm_tint_rect` 2599:0440 (bir dikdörtgenin c'den
farklı her p pikseli blend(p, c) olur; `gr_draw_strip` 0C7F:0A51 "RenkFiltre", tablonun 7F80h
bayttan azı eşlenmişse ölümcül hata 70h), `asm_blend_span` 2599:04AD (`gr_blend3` 0C7F:08FC,
`demo_show_picture`'ın çapraz geçişleri) ve `asm_upscale_view2x` 2599:0500 (iki katına
çıkarılmış düşük ayrıntılı 3D görünümün aradaki pikselleri). `sys_load_rix` 0AC6:1AA0 dosyayı
C001h baytlık bir EMS bloğuna yükler (16E0:E52F; ad E570'te tutulur ve aynı adın ikinci kez
yüklenmesi hiçbir şey yapmaz). Palet değişikliklerinden sonra (3DSYS, ISOSYS ve FRPOS'taki
çağıranlar) `sys_build_aa_table` 0AC6:1A13, 0. satırı ((0, i) çiftleri) 16E0:E5EF hedef
paletinden yeniden yazar.

| dosya | yükleyen |
|---|---|
| XD.RIX | `s3d_load_district`; bittiğinde `demo_story_scene` |
| XDINTRO.RIX | başlangıçta `ist_main`, `demo_intro_menu` (acilisa.pal'den sonra) |
| XDEMO.RIX | `demo_story_scene` |
| XDSON.RIX | `demo_ending` |

İçerik (paletlere karşı denetlendi, kodun ihtiyacı yok): bir kayıt, mutlak farkların toplamına
göre ((Ra + Rb) >> 1, (Ga + Gb) >> 1, (Ba + Bb) >> 1)'e en yakın palet rengidir; eşitlikte
ilki. XDSON.RIX bu kurala SOOON.PAL üzerinden 32640 kaydın tümünde uyar, XD.RIX ise PAL3D.PAL
(= orig/SONPAL.PAL) üzerinden 255'i dışında hepsinde; bu 255 kaydın hepsi bir rengi 254 ya da 255
ile eşler. XDINTRO.RIX ve XDEMO.RIX, ACILISA.PAL / DEMO1.PAL ile yalnızca 15232 / 9923 kayıtta
uyuşur: hazırlandıkları paletler belirlenmemiştir. `sys_build_aa_table` aynı ortalamayı ve
uzaklığı kullanır (`sys_find_color` 0AC6:195E) ama son minimumu kazandırır.

### AMF – DSMI modülü – doğrulandı (src/dsmi/amfload.c içinde `ampLoadAMF` 105B:008A, tests/py/test_amp.py; `tools/fmtcheck.py amf`)

MUZIK.CKD'nin müziği: 15 modül, hepsi sürüm 14. `ampLoadModule` (105B:07F3) "AMF"yi denetler,
adın 20 baytını kopyalar, v1/v8 için varsayılan pan değerleriyle 4 kanalı zorlar, ardından
`ampLoadAMF` dosyanın başından okur. Yükleyicinin okuduğu biçimiyle düzen (v14 ofsetleri):

| ofs | boyut | alan |
|---|---|---|
| 00 | 4 | `"AMF"` + sürüm baytı: 1, 8, 9, 10..14 kabul edilir, yoksa −3 |
| 04 | 32 | şarkı adı |
| 24 | u8 | enstrüman sayısı |
| 25 | u8 | sıra (order) sayısı (MODULE "patternCount") |
| 26 | u16 | mantıksal iz (track) sayısı |
| 28 | u8 | kanallar (v ≥ 9) |
| 29 | 32 × i8 | kanal panı −63..63, 100 = surround (v ≥ 12; v 9..11: 16 bayt; v11'in altında 16E0:85AA varsayılan tablosuyla değiştirilir) |
| 49 | u8, u8 | BPM, hız (v ≥ 13; değilse 125, 6) |
| 4B | | sıralar: sıra sayısı × { u16 satır (v14; değilse 64), kanallar × u16 iz referansı } |
| | | enstrümanlar: sayı × 65 bayt (v < 10: 59) |
| | | iz haritası: iz sayısı × u16 saklanan iz numarası, 1 tabanlı |
| | | saklanan izler: max(harita) × { u16 count, u8 type, count × {u8 row, u8 cmd, u8 arg} } (v1: her biri 3 bayt fazla) |
| | | örnekler: ham 8 bit, örnek numarasına göre; ardından hiçbir şey gelmez |

Bir iz referansı r, r mantıksal izini gösterir (1 tabanlı, 0 = yok); j mantıksal izi, haritanın
belirttiği saklanan izi çalar (birkaçı aynı izi paylaşabilir; count 0 = veri yok). Enstrüman
(v ≥ 10):

| ofs | boyut | alan |
|---|---|---|
| 00 | u8 | tip |
| 01 | 32 | ad |
| 21 | 13 | dosya adı |
| 2E | u32 | örnek numarası, 1 tabanlı (0 = yok) |
| 32 | u32 | uzunluk (yükleyici alt word'ü kullanır) |
| 36 | u16 | C notasının hızı (c2spd) |
| 38 | u8 | ses düzeyi 0..64 |
| 39 | u32, u32 | döngü başı, döngü sonu (0 = döngü yok) |

v < 10: +32'den itibaren u16 uzunluk, u16 hız, u8 ses düzeyi, u16 döngü başı, u16 döngü sonu
(FFFFh: döngü yok). k örneğinin uzunluğu, örnek sayısını ilk kez k'ye çıkaran enstrümandan
alınır; her örnek uzunluk + 16 baytlık bir blok alır ve DDS'in "EMSAlloc"/"EMSCopyTo" dışa
aktarımlarıyla 400h bayttan büyük bir örnek EMS'e taşınır.

İz kayıtları `ampReadTrackRow` (0EF6:0955) tarafından okunur: row ≤ geçerli satır olan tüm
kayıtlar; row FFh izi bitirir. cmd < 80h bir notadır (0 = durdur, 7Fh = notayı koru), arg = ses
düzeyi (FFh = koru); cmd 80h..97h komutlardır (80h enstrüman, 81h hız, 82h ses kaydırma (volume
slide), 83h ses düzeyi, 84h portamento, 86h ton portamentosu, 88h arpej, 89h vibrato, 8Ch desen
kırma (pattern break), 8Dh konum atlama (position jump), 94h nota kesme, 95h tempo, …; bkz.
`src/dsmi/amp.c`).

Dağıtılan dosyalar: başlık 75 bayt, her birinde 31 enstrüman, bunların 3..13'ü kullanılır (tip 1,
enstrüman sırasıyla örnek numaraları 1, 2, 3 …; aradaki kullanılmayanlar tip 0, numara 0, uzunluk
0); hız 8368 (89 enstrüman) ya da 8252 (2); 6 kanal (ISTANBUL.AMF 8) −63, 63, 63, −63, … olarak
pan edilmiş, geri kalanı 0; başlık temposu 125/6 (95h efekti 11 yerde 120 ya da 144 ayarlar); her
sıra 64 satır; 6..89 saklanan iz üzerinde 6..108 mantıksal iz, hepsi tip 0, 496'sı `FF FF FF` ile
biten ve 4'ü boş. Kullanılan komutlar: 80h, 81h, 82h, 83h, 84h, 86h, 88h, 89h, 8Ch, 8Dh, 94h,
95h. Her dosya tam olarak son örneğinden sonra biter.

### ISOSYS savaş durumu (DGROUP) – kısmen doğrulandı (`src/game/iso*.c`, tests/py/test_iso.py, test_iso_b/c/d.py)

İzometrik savaş (ovl4595 ISOSYS.L) tüm durumunu DGROUP'ta tutar; kendine ait bir savaş
dosyasından hiçbir şey okunmaz (savaş alanı 3D haritadan, canavar grubu kayıtlarından ve
GRP.DAT'ın ekip kayıtlarından kurulur).  Tüm word'ler little-endian i16'dır.

| nerede | ne |
|---|---|
| A08A + 28h·c, c = 1..28 | birim: +00 hp, +02 vecize puanı, +04 bakış yönü 1..4 (+4 saldırı karesi, +100 ölüm karesi), +06 şablon g, +08 x, +0A y, +0C son hedef, +0E taraf (1 ekip/müttefikler, 2 düşmanlar), +10 büyüleyen, +12 isabet, +14 kaçınma, +16..+1E beş durum yuvası (süre·100 + kimlik), +20 vuruşta vecize, +22 sersemlik turları, +24 vuruldu, +26 ?. Birim 1..24 canavarlar (grup (c-1)/8), 25..28 ekip |
| 9F18 + 2Eh·g | şablon (23 word). g 1..4 ekip üyeleri, 5..7 canavar grupları.  w1, w3, w9, w11, w16: öldürme değeri; w2 hasar (yüzler·100 + taban: random(h+1) + taban); w5 zırh; w7/w8 saldırı 1/2'nin AP maliyeti (> 100 ise −100); w9 < 10 ve w10 < 0: sersemletilebilir |
| 1287 + 2Eh·t | canavar tipi t (23 word). Savaş kurulumu (`iso_battle.c`), i grubunun 4 + i şablonunu (tip 5BDA + 6i'de; 100'ün üstü: müttefik grup, 100 çıkarılır) tipinin 2..19 word'leriyle doldurur, word 1 = w1 + zorluk (seçenek BB39)·w0, ardından word 3'e 1 ekler; boş bir grubun 1..19 word'leri 0 olur |
| 5BC2 + 6·g | g şablonunun grup tipi (ayak sesleri; avcı (slayer) silahları) |
| C2CE + 38h·x + 2·y | 28×28 doluluk ızgarası: birim numarası ya da 0 |
| C8EE + 38h·x + 2·y | arazi ızgarası: 0 açık, 1..9 zemin karoları, ≥ 10 duvarlar (resim 2616[t], üst kısım t+6); 6, 8 ve ≥ 10 adımı engeller |
| CF0E + 36h·x + 2·y | mini harita renkleri, 27×27 |
| 99A6 + 5Ch·p | ekip kaydı (GRP.DAT): +06 alıştırma, +08 deneyim, +0C..+1A iso_xp_progress ile artan istatistikler, +3A+2k k elindeki silah |
| 9902 + 22h·p | gelişim işaretleri: her istatistiğin son artışındaki sayaç değerleri |
| 9B7C + 24h·p | ırk word'leri: +16 gözde düşman grubu, +18 nefret edilen grup, +02..+12 gelişim düzelticileri |
| 2632 + 2·side | "tarafın yaşayan bir birimi var"; bir taraf yok olduğunda 632E = 1 |
| D4C0 + 2·i / D4FA + 2·c | inisiyatif sırası / zar sonuçları |
| D54A, D54C | tur, sırası gelenin AP'si; D5DE sırası gelen, D56E/D56C saldıran/savunan, D576/D574 canlandırılan birim / tür (1 dönüş, 2 ölüm, 3/4 vuruş, 5 konuşma, 6 ıska) |
| D57A/D578/D57C | hedef imleci x, y, açık; D580/D57E görünüm başlangıcı (0..20) |
| 6393 + 0Dh·i | ıskada gösterilen on ünlem ("random(10)+1" → 631A, A510'a kopyalanır) |

### SAKLAnnn.SAV (kayıtlı oyun) – doğrulandı (`src/game/s3d_save.c`, tests/py/test_s3d_save.py; orig/SAKLA001.SAV)

1524 bayt; s3d_write_save (303F:3DD0) tarafından 22 ham DGROUP bloğu olarak yazılır, s3d_read_save
(303F:3F95) tarafından aynı sırayla geri okunur. Little-endian word'ler; ekip kayıtları yukarıdaki
GRP.DAT'taki gibi.

| ofset | boyut | içerik (DGROUP) |
|---|---|---|
| 0 | 60 | kaydetme menüsünde yazılan açıklama, NUL ile sonlanan; NUL'dan sonraki baytlar çağıranın arabelleğinde ne varsa odur (yükleyici 60 baytın hepsini atlar) |
| 60 | 2 | sonraki bloğun uzunluğu = 0020h (yükleyici bu kadar bayt okur) |
| 62 | 32 | B9CA: 256 FRPOS olay bayrağı (n bayrağı = n/8 baytı, n%8 biti) |
| 94 | 460 | 99A6: 5 × 5Ch karakter istatistik kaydı (0. kayıt kullanılmaz) |
| 554 | 740 | 9C26: 5 × 94h ad / karakter kaydı |
| 1294 | 170 | 98FC: 5 × 22h karakter başına tablo |
| 1464 | 7 | BB34: seçenekler (animasyonlar, savaş hızı, oyun müziği, savaş müziği, ses efektleri, zorluk, büyük görünüm) |
| 1471 | 2 | 3D5E: durum bitleri (yazılır; yükleyici atlar ve yeniden hesaplar) |
| 1473 | 2+2+1 | B9F3 x, B9F1 y, B9F0 bakış yönü (0 kuzey, 1 doğu, ...) |
| 1478 | 2 | BB6B: semt 0..17 |
| 1480 | 2 | BB4D: müzik parçası |
| 1482 | 8 | BB6D: 4 word, BB73 = para |
| 1490 | 2+2+2 | 3D64 dakika, 3D66 saat, 3D68 gün (0..359) |
| 1496 | 11 | BBB7 |
| 1507 | 11 | BBAC |
| 1518 | 2+2 | BB8A, BB88: şehir haritası işaretçisi x, y |
| 1522 | 2 | BBAB: doktor ücreti baytı, word olarak yazılır (ikinci bayt BBAC[0]'ı yineler). Eksiksiz bir kayıt yüklenirken ücretini korur (yeniden zar atma random(100)+101 yalnızca son okuma eksik kaldığında çalışır: play5 ücreti 106 olan bir kaydı yükler ve özgün program 106 ister, docs/VERIFY.md); bir semte varıldığında yeni bir ücret belirlenir |

Okuduktan sonra yükleyici "s" + ad (16E0:269B + 0Dh × kayıt word'ü +42h) portrelerini 47h-4Ah
resimlerine yükler, semti yükler ve 3D görünüme girer; görünüm boyutu büyük görünüm seçeneğinden
gelir.

### FRPOS olay betikleri PCn / PCSn – doğrulandı (`src/game/frpos*.c`, `tools/frposdis.py`, tests/py/test_frpos.py)

Her n semtinin (1..18) FRPOS.CKD içinde bir **PCn** haritası ve bir **PCSn** betik bankası
vardır. İkisi de AMOS bankasıdır: 20 baytlık başlık `"AmBk"`, BE16 banka numarası, BE16
bayraklar, BE32 `80000000h | length`, `"Data    "`.
3DSYS harita dosyasının tamamını bir EMS bloğunda (kayıt işaretçisi 16E0:E33F), hücrelere, yani
dosya baytı 2Bh'ye bir işaretçiyi de 16E0:B9EC'de tutar (s3d_load_district harita başlığını -
genişlik, yükseklik, başlangıç ... - okumak için önce 14h baytını, sonra 2Bh'yi kilitler, 303F
s3d_district.c); betik dosyası 16E0:E34F'nin EMS bloğudur. Aşağıdaki tüm ofsetler dosya
ofsetleridir (başlık dâhil); yorumlayıcının PC'si 16E0:BB98 word'üdür.

**Harita hücreleri.** (x, y) hücresi, dosya baytı 2Bh + (y·width + x)·15'te 15 bayttır (14h baytı
genişlik, 15h yükseklik). 11..14 baytları olay anahtarıdır: her bayt (≤ 20h, yoksa ölümcül 51h)
10'un altındaysa başına 0 eklenerek `%d` ile yazdırılır ve 8 karakter verir.
"00000000" = olay yok. Sabit anahtarlar (yalnızca tetikleyici 2): "31313127" doktor (BBAB ücreti
karşılığında sağlığı −5 ya da daha düşük olan bir üye sağlık 1 alır; başka herkes için "ölü
değil, başkasını seç"; para yetmezse: "bu para yetmez!"), "31313128" yemek (yaşayan her üye için
random(5)+2, açlık word'ü 11h'yi temizler), "31313129" silah dükkânı (eşya değiş tokuşu kipi 1,
simge 2Bh), "31313130" mal dükkânı (kip 2, simge 2Ch), "31313131" 3D5E'nin 2. bitini ayarlar
(başka her anahtar onu temizler). Aksi hâlde olay numarası, 8 bit aritmetikte
`(b13 − m27)·32 − sign(b13 − m27) + b14 − m28`'dir; m27/m28 = harita dosyasının 27h/28h baytları.
fr_run_event(cell, trigger), 3DSYS tarafından tetikleyici 1 (girerken), 3 (çıkarken), 2
(arama/kullanma) ile çağrılır.

**Betik bankası.** 14h'de: BE16 olay sayısı (olay ≥ sayı ölümcül 53h/54h), ardından olaylar, her
biri `[BE16 len][body]`. Gövde: BE16 komut bütçesi (→ BB93: çalışabilecek komut sayısı; her
komutta ve atlamalarda azaltılır, bölümler arasında paylaşılır), tetikleyici 2 bölümünün,
tetikleyici 3 bölümünün ve bir kuyruğun BE16 ofsetleri (gövde başından); tetikleyici 1 bölümü
gövde + 8'de başlar. Bir bölüm `[12 + trigger][flags → BB92]` ve komutlarından oluşur (yanlış bir
ilk bayt "..baslik:%d Tarz:%d.." yazdırır, ardından ölümcül 56h). Kuyruk her zaman `04 00`'dır.
Dizgiler `[len][len bytes]` biçimindedir; her bayt 10 eksik saklanır (metin 0ACh+10 = 0B6h,
ileti yazı tipinin satır sonudur). Yorumlayıcı `BB93-- != 0` olduğu ve hiçbir komut onu
durdurmadığı sürece komut çalıştırır; sonunda paneli geri yükler ve s3d_redraw_view(B9C8)'i
çağırır. Yalnızca bir ışınlanmadan (op 11) sonra 1 döndürür. ACC = 16E0:BB9E (aynı zamanda
ui_dialog_2198'in evet/hayır sonucu), WHO = BB9C; 256 bayrak B9CA'nın bitleridir (n bayrağı: n/8
baytı, n%8 biti; .SAV içinde saklanır).

| op | işlenenler | etki |
|---|---|---|
| 1 | t, a, n | t=1: a bayrağı temizse n komut atla; t=2: ACC ≠ a ise n atla; başka t ölümcül 57h |
| 2 | str | paneli temizle, BBAA=0, ileti; sonraki işlem kodu (opcode) 5 değilse satır·100+140 bekle |
| 3 | b | görünümde portre/resim "ikon<b>.cmp" (+ .pal); B9C8 = 1 |
| 4 | – | paneli temizle, dur |
| 5 | l1, l2, s1, s2 | evet/hayır sorusu (ui_dialog_2198) → ACC (0 = ilki) |
| 6 | n | n komut atla |
| 7 | f | f bayrağını ayarla |
| 8 | x, t1, c1, t2, c2, t3, c3 | 5BDA/5BDC + 6i'deki 1..3 düşman grubuyla (tip, sayı) savaş; hepsi sıfır = üç rastgele grup (tip harita baytı 1Ch + random(8), sayı random(8)+1); ISOSYS'ten sonra: semt yeniden yüklenir, A0B0/9F46 savaş tabloları temizlenir; kaybedildiyse (632E = 2) ya da kimse yaşamıyorsa → oyun biter, dur, hareketi iptal et |
| 9 | a, b, c, W1, W2, W3 | hazine: BBA2/4/6 para birimleri (BB6F/BB71/BB73'e eklenir), BB57.. içinde eşyalar (BE16 word'ler), eşya değiş tokuşu ekranı kip 0 |
| 10 | s1, s2 | iki iletiden biri (random(2) ? s1 : s2); op 2 gibi bekle (satır·100+100) |
| 11 | d, BE16 pos, f | ışınlanma: d < 100: d−1 semti (değişirse müzik ve geçişlerle yeniden yüklenir), y = pos / width, x = pos % width − 1, bakış f % 4; d = 100: semtten ayrılma iletişim kutusu; ardından yeniden çiz, B9C8 = 1, hareketi iptal et, dur, 1 döndür |
| 12 | who, w, p | istatistik word'ü w := w·p/100 (16 bit), 1..4 üyeleri için (who 0) ya da WHO için (who 5, ayrıca BA89+11h·WHO = 1); w = 1Dh (sağlık) için sağlık + 5 üzerinden hesaplanır; sağlıkta p < 100 ise 9 numaralı sesi çalar |
| 13, 14, 15 | b | hiçbir şey (hiç kullanılmaz) |
| 16 | – | hareketi iptal et (BBA8 = 1), dur |
| 17 | c, str | c üyesi metni söyler (0 = random(4)+1, 5 = WHO; WHO > 4 ölümcül 58h) |
| 18 | – | ACC = 3D5E bit 0 ? 0 : 1 |
| 19 | b | film: 3D5C = 1 iken CD denetimi (Esc vazgeçer), kararma (b = 2 müziği korur), gfl_play(16E0:2D6E + 13·b), semt yeniden yüklenir |
| 20 | n | hata ayıklama printf + getch, ACC = random(n)+1 (hiç kullanılmaz) |
| 21 | 6 × str | altı seçenekli menü (fr_menu: tıklama ya da ilk harf) → ACC 1..6 (hiç kullanılmaz) |
| 22 | w, kind, v | WHO := w word'ü v'den küçük (kind 0), büyük (1), eşit (diğer) olan ilk üye; yoksa: dur |
| 23 | kind, w | WHO := w word'ü en düşük (kind 0) / en yüksek olan üye (fr_find_character) |
| 24 | – | oyuncu yaşayan bir üye seçer (F1-F4 / portre) → WHO |
| 25 | m | geçişlerle m müziği (BB4D); m = 1 ayrıca 3D5E'yi 20h'ye ve gün ışığı geçişini sıfırlar; BB36/BB38 müzik seçeneklerine uyar |
| 26 | m | m = FFh: önce tüm para 0; sonra m ≤ BB73 ise: m öde, ACC = 1, değilse ACC = 0 |
| 27 | 40 × str | hikâye sahnesi 11 (demo_story_scene), harita bloğunu serbest bırakır; 40 dizgi okunmadan atlanır |
| 28 | – | bir vecize öğret (fr_teach_aphorism) |
| 29 | x, BE16 item | `item` eşya word'ünü her envanterden (ve onu gösteren el yuvalarından) kaldır |
| 30 | l, n, str | parola: oyuncu panelde en çok l karakter yazar; str'ye eşit değilse (ikisi de küçük harfe çevrilmiş, aksanları atılmış) n atla |
| 31 | n | hata ayıklama printf + getch, n·50 bekle (hiç kullanılmaz) |
| 32 | – | ekibi önceki hücresine geri koy (BB4F/BB51), yeniden çiz, dur |
| 33 | – | semtten ayrılma iletişim kutusu (s3d_leave_district), dur |

Diğer işlem kodları ölümcül 5Dh'dir. fr_skip_commands komutların üzerinden yukarıdaki işlenen
(operand) boyutlarıyla geçer (op 30 sayı baytından sonra `l + 1` bayt atlar: `n` baytı ve
dizgi); orada bilinmeyen bir işlem kodu ölümcül 61h/62h'dir. 16E0:5C0B > 0 ise (oyunda 0) her
komut "Komut no:%d" yazdırır ve bir tuş bekler; > 1 bayrak/ACC/atlama izleri ekler. Dağıtılan 18
betiğin sayımı (266 olay): 13, 14, 15, 20, 21 ve 31 dışında her işlem kodu kullanılır.

**Eşya değiş tokuşu ekranı** (fr_item_exchange, kip 0 hazine / 1 silahlar / 2 mallar). Kip 1/2'nin
dükkân stoğu (10 yuva, BB57.. word'leri) semt başına BBB8../BBAD.. baytlarında tutulur; BBB7 /
BBAC baytı onun fiyat tabanıdır (ilk ziyarette rastgele: 4..11 eşya, kimlikler sırasıyla
random(31)+1 ve random(10)+21h). Satın alma: 2·price(id) + taban − takas değeri; burada price
16E0:1041 + id bayt tablosudur ve değiştirilen envanter eşyasının takas değeri
2·price + 5 − random(10) − 1'dir (en az 1); satma (S, bölge 28) 2·price + 5 − random(10) +
100·(item/100) − 1 (tuş) ya da + 1 (fare) öder. B / bölge 27 diğer para birimlerini değiştirir:
BB6F·10 + random(9) ve BB71·5 + random(2) kIvrIk.

## English

All multi-byte values are little-endian. Each section notes the status: **verified**
(round-trips or is fully explained by code), **partial**, or **todo**.

### FLiB archive (`*.CKD`) – verified

```
+0x00 char[4]  "FLiB"
+0x04 u32      entry count
+0x08 u32      payload size (informational)
+0x0C entries, back to back:
      +0x00 char[4]  "FiLE"
      +0x04 char[32] name, NUL padded
      +0x24 char[16] name again (8.3)
      +0x34 u8       flags (always 0 = stored)
      +0x35 u32      absolute offset of the data (= entry offset + 0x41)
      +0x39 u32      stored size
      +0x3D u32      original size (always = stored size)
      +0x41 data
trailer: a verbatim copy of all entry headers (count × 0x41 bytes), i.e. a directory
         the game can read from (file size − count×0x41).
```

Tool: `tools/flib.py list|extract`. The trailer is checked against the inline headers.

**How the game reads it** (ovl569A/ovl5646): it reads only the trailing directory. The
header's `+0x08` field is the directory offset, which holds for every CKD. Lookup is a linear,
case-insensitive 32-character name compare across the archives in open order (0gfl, muzik,
sounds, frpos, ikon, ivir, savas, 3d), falling back to a plain DOS file.

### IST.EXE overlay block (FBOV) – verified

Documented in `tools/exeload.py`.

### CMP image – verified (decoder in `tools/cmpview.py`)

```
u16 width, u16 height
RLE stream up to EOF:
  c >  0x80 : run      - (c - 0x80) copies of the next byte
  c <= 0x80 : literal  - the next c bytes (c = 0: no-op)
```
Pixels are 8-bit palette indices, row-major. Decoders: FILEMAN `fm_cmp_decode` 5447:043B
(< 64 KB) and `fm_cmp_decode_huge` 5447:0000 (EMS window / huge pointers).

**Fidelity note:** 11 of 262 files decode to more than `width*height` bytes (+1 byte, or +320
for IVIR/SCW.CMP). The original writes those bytes past the image, into the slack of the
MEMMAN block. The port must allocate with MEMMAN's rounding and keep the overrun:
3D/GOTY0, GOTY1, VIEWPC; IVIR/ACILISA, ACILISB, IREM62, ISTB, RAKS, RPZ, SCW; SAVAS/IROSP1.

### GFL/GHD video – tool-verified (`tools/gflview.py`, player `src/game/gfl.c`)

35 videos in `0GFL.CKD` (`extracted/0GFL/`), each a pair `<name>.GHD` + `<name>.GFL`.
`gflview.py check extracted/0GFL` verifies every statement below on all 35 pairs; the
codec is checked bit for bit against the original decoder (`tests/py/test_gfl.py`).
All numbers little-endian.

**GHD** = header + first second of audio + frame index (file size = `0x310 + abytes + 8*nframes`):

| off | size | field |
|---|---|---|
| 0x000 | 3 | `"GFL"`. The player only checks that the 3 *signed* bytes sum to 0xD9 ("GFL versiyon hatası.") |
| 0x003 | u16 | nframes |
| 0x005 | u8 | fps – 10 in every file |
| 0x006 | u16 | abytes: audio bytes per 1-second chunk – 22050 in every file (8-bit unsigned mono, 22050 Hz, 0x80 = silence); 0 = no audio |
| 0x008 | u32 | 100000 in every file, never read |
| 0x00C | 4 | 0 |
| 0x010 | 0x300 | header palette, 256 × RGB, 6-bit VGA DAC values |
| 0x310 | abytes | audio of second 0 |
| 0x310+abytes | 8·nframes | frame index |

Index entry (8 bytes, packed): `u8 type, u16 vsize, u32 offset, u8 flags`.

* `type` 0: delta frame. 2: frame that carries the next second of audio – every `fps`-th
  frame (0, 10, 20, …); its video is usually a full picture, but not always (SEQ5 has
  2-type frames of 548 bytes). 1: palette-only frame (no video is decoded; `vsize` says 768
  but no video bytes follow) – only the fade-out at the end of SEQ5 (37 frames). The player
  treats every type ≠ 0 as a resync point ("key frame") when it drops frames.
* `offset`: GFL offset of the frame's payload. If `flags & 0x80` a 0x300-byte palette sits
  right **before** it, at `offset - 0x300`.
* `flags`: bit 7 = palette chunk present. Bits 0–6 = music sync: `n = (flags & 0x7F) >> 1`,
  negated if bit 0; the player calls AMP `ampBreakPattern(n)`, then n∓1 … down to 0
  (so `ampBreakPattern(0)` once for every shown frame when sound is on). Every shipped
  file has 0x00 or 0x80.

**GFL** = the payloads in frame order: `[palette 0x300 if flags & 0x80] [audio abytes if
type == 2] [video vsize bytes, none for type 1]`, contiguous:
`offset[i+1] = offset[i] + (type==2 ? abytes : 0) + (type==1 ? 0 : vsize) + (flags[i+1] & 0x80 ? 0x300 : 0)`,
and the last frame ends at the end of the file (for SEQ5's last, palette-only frame
`offset` = file size). 28 of the 35 videos carry palette chunks (31 each in most district
movies, 20 in SEQ4, 58 in SEQ5, 73 in SON); FUN, INTRO, SEQ1–3, UMR-D-IN and UMR-D-OT have none.

**Video codec** (`gfl_decode_frame` 5313:0390). It updates a persistent 320×200 8-bit
buffer (64000 bytes) that holds the previous frame; nothing ever clears it. For each code
byte b:

| b | action |
|---|---|
| `1nnnnnnn` | literal: copy the next `(b & 0x7F) + 1` bytes |
| `01ffnnnn` | skip `L + 1` pixels |
| `00ffnnnn` | fill `L + 3` pixels with the next byte |

`L` = n, 2n + 0x10, 4n + 0x30, 8n + 0x70 for ff = 00, 01, 10, 11, so skips are 1–16,
17–47 step 2, 49–109 step 4, 113–233 step 8 and fills 3–18, 19–49/2, 51–111/4, 115–235/8.
After each code the decoder continues while `src < start + vsize` and `dst < 64000`
(do-while: one code is decoded even for vsize 0; a run can overshoot the end). In every
frame of every file the stream is consumed exactly and ends exactly at pixel 64000.

**Playback** (details in `src/game/gfl.c` and `analysis/names/gameB.md`): frame due =
`(clock_ms - start) * fps / 1000`; palette chunks are set when the frame is shown, a frame
without one restores the header palette (only if a chunk palette was set); the audio of a
type-2 frame k goes to half `(k / fps + 1) & 1` of a looping 2 × abytes sample. Late frames:
a delta frame due after skipped frames is not shown, and streaming jumps to the next
type ≠ 0 frame.

### Formats from the inventory notes – where they are now

The first inventory (`analysis/names/gameA.md`, `gameB.md`, `lib.md`) listed these; each now has
a section here, built from the decompiled code:

| Format | Section |
|---|---|
| SAKLA###.SAV | "SAKLAnnn.SAV (saved game)" – verified |
| Map banks ("AmBk", 15-byte cells) | "FRPOS event scripts PCn / PCSn" – verified (cells, event keys); wall-set bytes 3..6 of the map: "KOR" below |
| FRPOS scripts PCn/PCSn | "FRPOS event scripts PCn / PCSn" – verified |
| Combatant / template tables | "ISOSYS battle state (DGROUP)" – partly verified |
| AMF modules | "AMF – DSMI module" – verified |
| DSD driver container ("DS") | "DSD – DSMI driver" – verified |

Not file formats, and not documented here: the district table 16E0:2E31 (18 × 1Dh: far
pointers to the map name "pcN", the genitive name, four GFL names day-in/night-in/day-out/night-out
at +08..+14, the horizon picture at +18, then a byte at +1C; see `S3D_DISTRICT_TAB` in
`src/game/s3d.h`), and the fields of three DGROUP tables: the monster types 1287 + 2Eh·t
(23 words; only how the battle copies them into the templates is in the ISOSYS section), the
weapons 0E7F + 0Eh·k (7 words; word 0 = the monster type it gets +1 to hit and +2 damage against, `iso_combat.c`) and the
vecizes 007E + 16h·s (11 parameter words, `VECIZE_PARAM` in `iso_spell.c`). The meaning of most
of their fields is only guessed in `analysis/names/gameB.md` and has not been checked.

### TRDOS.DAT – verified (`o559A_load_trdos_font` 559A:01F3, tests/py/test_fatal.py)

4096 bytes = 256 glyphs × 16 rows, 1 bit per pixel, MSB = leftmost: the 8×16 code page 857
(Turkish DOS) text-mode font (e.g. glyph 98h is İ). A plain DOS file next to IST.EXE, opened with
`fopen("TRDOS.DAT", "rb")` (not through the CKD archives). `o559A_load_trdos_font` reads 1000h
bytes into a malloc'd buffer and loads them into the VGA character generator with INT 10h
AX=1100h (BH=10h bytes per glyph, BL=0 block 0, CX=100h glyphs from DX=0, ES:BP = buffer), then
frees the buffer. Called by `ist_main` at start-up and by `o559A_fatal` after switching back to
text mode 3, so the exit and error messages show Turkish letters. A missing file only prints
"Turkce Dos Fontu bulunamadi."; a short file leaves the buffer's old bytes in the rest of the font.

### PAL – verified (data side)

768 bytes = 256 × (R, G, B), 6-bit VGA DAC values (0–63). Four 48-byte files hold
16-colour partial palettes.

### PUR / SND – verified (loader and players; the DEMOLAR callers against DOSBox-X: para00.snd in the story slideshow, docs/VERIFY.md play8; buun.pur in the new-game intro, play3/playq)

Headerless unsigned 8-bit mono PCM (silence = 0x80; the MCP mixer reads byte 0 as −128).
SOUNDS.CKD holds 196 `.PUR` files (effects, voices) and `PARA00.SND`..`PARA09.SND` (narration,
125–489 KB). Nothing in a file gives its rate: every caller passes one.

**Loading** (`oses_load_sample` 55CC:01F3, tests/py/test_sound.py on real files): the file goes
into sample slot n (0..30h; record DE34 + 17h·n = MEMMAN reference, SAMPLEINFO from +4); the
callers here all load into EMS (type 1), in pieces of at most FF00h bytes. With
flags bit 1 the rest continues in slots n+1, n+2 … (up to 30h), each marked 200h in the sound
table but the last; without it only the first piece is loaded. Flags bit 0 stores each piece
reversed (table flag 100h).

**Sound table** 16E0:22DB + 11h·n, n = 0..32 (from INTRFACE's side; `g_snd_name`/`g_snd_rate`
in `src/engine/intrface.h`): `char name[13]; u16 rate; u16 flags`. `ui_load_voice(n, rate, …)`
(05DC:192D) loads slot n on demand and plays it on the last two channels; `iso_play_sfx`,
3DSYS, the door animation (slot = wall set + 0Dh) and FRPOS op 12 (slot 9) pass the table rate.

| n | name | rate | flags (hex) | n | name | rate | flags (hex) |
|---|---|---|---|---|---|---|---|
| 0 | kufurbuf | 21000 | 03 | 16 | kapi1.pur | 11000 | 15 |
| 1 | adim1.pur | 4000 | 37 | 17 | kapi4.pur | 22000 | 15 |
| 2 | olum.pur | 44100 | 23 | 18 | kapi5.pur | 11000 | 15 |
| 3 | vurus.pur | 8000 | 13 | 19 | kapi6.pur | 11000 | 15 |
| 4 | vurama.pur | 7500 | 13 | 20 | kapi7.pur | 10000 | 15 |
| 5 | iska.pur | 6000 | 13 | 21 | merdalt.pur | 32000 | 04 |
| 6 | adim2.pur | 11000 | 37 | 22 | merdust.pur | 32000 | 04 |
| 7 | adim3.pur | 40000 | 37 | 23 | kanat.pur | 44000 | 33 |
| 8 | uzmen.pur | 20000 | 13 | 24 | surt.pur | 30000 | 33 |
| 9 | bhasar.pur | 34000 | 07 | 25–28 | sw1..sw4.pur | 11000 | 23 |
| 10 | bosch.pur | 21000 | 07 | 29–32 | fw1..fw4.pur | 44100 | 23 |
| 11 | adim5.pur | 40000 | 37 |  |  |  |  |
| 12 | adim8.pur | 44000 | 37 |  |  |  |  |
| 13 | viyk.pur | 8500 | 33 |  |  |  |  |
| 14 | kapi1.pur | 11000 | 15 |  |  |  |  |
| 15 | kapi2.pur | 11000 | 15 |  |  |  |  |

Values read from IST.EXE's DGROUP (records 33..48 are empty). Flags: 1 never freed;
2 / 4 select the slot for the voice modes of `ui_init_voices` (mode 0 loads the slots without 4,
mode 1 those without 2, mode 3 those with both); 8 play the continuation pieces too and wait;
10h random pitch, `rate + (rand()·(rate/5) >> 15) − rate/10` (±10 %; the shifted product is
cut to 16 bits); 20h voiced: in battle
`iso_play_sfx` (ovl4595:214E) scales the rate by the speaker's voice class; 100h/200h set at
run time (above).

**Voice class rate** `iso_sfx_rate(who, base)` (ovl4595:255B, tests/py/test_iso.py):
`r · base / 7500` (u32, capped at FFFFh), r from the byte table 16E0:1E8D + who (monster type):
class 0/2 6000, 1 7500, 3 7000, 4 6500, 5 4500, 6 20000, 7 8500, 9 44000, others 22000; the party
(who > 40) 7500. The vecize voices (`vecizeNN.pur`, `fecizeNN.pur`, `cevapNN.pur`,
`fevapNN.pur`) are loaded into slot 21h and played at `iso_sfx_rate(speaker, 44100)`
(`iso_speak_vecize`, tests/py/test_iso_d.py test_sound_speak); `ui_play_voice` plays the
continuation pieces at the same rate.

**Others** (not covered by tests yet): `PARA*.SND` - `demo_story_scene` (3C2D:1108) loads
`paraNN.snd` for each slide into slot 21h with flags 2 (EMS pieces in slots 21h..) and plays it at
4E52h = 20050 Hz (`ui_load_voice(21h, 20050, 0, 0)`, then `ui_play_voice`); the last digit
(byte 5 of the name) is incremented per slide and never reset. `buun.pur` (new-game intro, `demo_load_sounds`) goes into
slot 2Fh with loop end 46E3h and plays at 2134h = 8500 Hz. The streaming player `snd_load_sound`
(0D42:01F0, 60000-byte ring in slot 30h) has no caller in the listing.

### RES – verified

Plain text, CRLF-separated file names. These are manifests of the resources an archive
preloads ("EMS statik bloklar"), e.g. `3D.RES`, `3DEM.RES`, `SAVDEM.RES`, `IDEM.RES`.

### THANX.DOC / DEBUG.LOG

cp857 text: credits, and a leftover developer heap/debug log from ALTDUZEY.

### DSD – DSMI driver ("DS" file) – verified (`DDSGetDriverInfo`/`DDSLoadDriver`, src/dsmi/dds.c)

`SDISB16.DSD`, `SDIGUS16.DSD`, `SDIPAS16.DSD`, `SDIWSS16.DSD`: a DOS MZ executable with the
signature changed to `DS` and two header fields reused. Loaded by `DDSLoadDriver`
(0DF2:0611) into the far heap and relocated like an EXE; it is not a DOS program.
28-byte header, all fields little-endian u16 unless noted:

| Off | MZ name | Meaning for DDS |
|---|---|---|
| 00 | e_magic | `"DS"` (5344h); anything else: error 4 |
| 02 | e_cblp | bytes in the last 512-byte page |
| 04 | e_cp | pages |
| 06 | e_crlc | relocation count (0 makes the load fail: farcalloc(0) = NULL, error 12) |
| 08 | e_cparhdr | header size in paragraphs (= file offset of the load module / 16) |
| 0A | e_minalloc | extra paragraphs allocated after the image |
| 0C | e_maxalloc | **device id** (SB16 64h, WSS 65h, PAS 66h, GUS C8h); `DDSGetDriverInfo` word 0 |
| 0E, 10 | e_ss, e_sp | 0 (unused) |
| 12 | e_csum | **version**, 0100h in all four; `DDSGetDriverInfo` word 2 |
| 14 | e_ip, e_cs (u32) | entry point CS:IP relative to the load segment (0000:0000 in all four) |
| 18 | e_lfarlc | file offset of the relocation table (20h) |
| 1A | e_ovno | **flags**: bit 0 must be set ("loadable"), else error 4; `DDSGetDriverInfo` word 1 |

Load (16-bit arithmetic as in the original):
`image = ((e_cp - 1) * 512 + e_cblp - e_cparhdr * 16 + 32) & 0xFFFF` bytes are read from
offset `e_cparhdr * 16` (32 more than the load module - the read is just short);
the block is `image + e_minalloc * 16 + 32` bytes and the image starts at its first
paragraph boundary. Relocations are `e_crlc` × {u16 off, u16 seg}; the word at
`image + ((seg * 16 + off) & 0xFFFF)` gets the load segment added.

Entry protocol (far pascal `long entry(long cmd, void far *arg, long arg2)`, result DX:AX):
`cmd 1` with `arg = DDSAccessFunction` - the driver looks up the DSMI services it needs
by name; AX = 0 ok (else DDS error 5), DX = device id (a loaded id again: error 8).
`cmd 2` (unload, `arg = 0`): non-zero = refused (DDS returns 11 but unloads anyway).

| File | Size | e_cp/e_cblp | relocs | hdr paras | minalloc | id |
|---|---|---|---|---|---|---|
| SDISB16.DSD | 29581 | 3Ah/18Dh | 13Ah | 51h | 12h | 64h |
| SDIWSS16.DSD | 25107 | 32h/13h | BCh | 31h | 12h | 65h |
| SDIPAS16.DSD | 21341 | 2Ah/15Dh | 88h | 24h | 11h | 66h |
| SDIGUS16.DSD | 13147 | 1Ah/15Bh | AEh | 2Eh | 2Fh | C8h |

#### DSD contents (SDISB16.DSD) – verified (`tools/dsdload.py`, runs `dsd_main` in Unicorn)

Two segments: code at image +0 (`JMP FAR` to `dsd_main` 147Ch at offset 0) and data
segment 03D3h at image +3D30h; all 314 relocations patch one of these two segment
values. The assembler left the **procedure names** in the code: each routine is
preceded by its ASCII name and a length byte (`...mcpOpenDDS` `0A`, code follows);
`tools/dsdload.py` lists them (mcpOpenDDS, scanLong, detectDevices, cmdSB, resetDSP,
SB16IRQHandler, initDevice, startOutput, getBufPos, stopOutput, closeDevice, dsd_main,
DMAInit…DMAStatus). The MCP mixer (image 1675h–36EFh) has no names.

`dsd_main(1)` imports through DDSAccessFunction: DDSAccessResource, DDSRegisterResource,
DDSRegisterFunction, DDSRemoveFunction, DDSRemoveResource, MemAlloc, MemCalloc,
MemRealloc, MemFree, cdiRegister, SetVector, GetEnvString (the result picks a flag at
d:31FE), then via mcpOpenDDS GetDSMITime, MCPSampleRealAddress,
MCPEnable/DisableVirtualSamples and SDIGetBufferPosition. It exports SDIDetectDevices
(01DE), SDIQueryDeviceCount (03F6), SDIQueryDevice (0406), SDIInitDevice (08F6),
SDIStartOutput (105C), SDIGetBufferPosition (1382), SDIStopOutput (13A0), SDICloseDevice
(140A) and the resource `MCPOutputBuffer` (d:087C, 12h bytes: u16 seg, u32 off, u32
linear, u16 size, u16 size, u32 rate, u8 flags). SDIInitDevice calls `cdiRegister` with
the CDI table at d:088F (26 far pointers, see docs/DSMI_MCP.md). `dsd_main(2)` removes
the SDI functions and returns its third argument; any other command returns 1.

#### SES.CFG – verified (oses_init 55CC:0000, SDISB16 initDevice)

107 bytes: `char[13]` driver file name (`SDISB16.DSD`), the 5Ch-byte **SDI config** record
copied to DGROUP E7EC and passed to SDIInitDevice, then `u16` at DGROUP 8DDA (2).
SDI config, u32 fields: +00 card type (1 SB, 2 SB Pro, 6 SB16; others: init fails),
+04 flags (bit 7: IST.EXE adds its 40 Hz cdiPoll timer routine), +08 I/O port, +0C DMA
channel, +14 IRQ, +10/+18..+20 stale far pointers of the setup program, +40
amplification (0 none, 1 auto, 2 low, 3 medium, 4 high), +44 stereo/mode option
(0 = stereo for SB Pro/SB16; SB: rate index), +48 rate index (SB16: 44100, 32000, 22050,
16000), +58 far pointer to a caller-supplied output buffer (0). Shipped file: SB16 at
220h, DMA 5, IRQ 7, no amplification, stereo, 22050 Hz → 16-bit stereo 22050 Hz, ring
of 8016 bytes.

### GRP.DAT / GRPHAZIR.DAT (party file) – verified (`src/game/chargen.c`, tests/py/test_chargen.py)

Written by `cg_save_party` (39E5:182E) at the end of character generation, read by
`cg_load_party` (39E5:1932); GRPHAZIR.DAT is the ready-made party of "çabuk başla".
664 bytes, no header, little-endian, members 1..4 (record 0 of DGROUP is not stored):

| file off | size | content |
|---|---|---|
| 74·(i-1) | 37 | name, NUL-terminated (PartyName +00, DGROUP 9C26+94h·i) |
| 74·(i-1)+37 | 37 | aphorism flags '1'/'0' ×36 + NUL (PartyName +6F); written as "1000…0" |
| 296+92·(i-1) | 92 | PartyStats record verbatim (DGROUP 99A6+5Ch·i), layout in `src/game/chargen.h` |

Stats words used at creation: +02 race 1..8, +04 gender 1/2, +0A..+20 the 12 attributes,
+22, +24 height, +26 weight, +28, +2C race special, +38 = +0A and +3A (health) = +0C set
just before writing, +42 portrait 1..16.  On load the race name (+4A of the name record)
is filled from the 11-byte table at 16E0:2638.  Bytes the game never set are written as
they are in DGROUP (zero in a new game).

### KOR – coordinate tables – verified (`tools/fmtcheck.py kor`: all 59 files)

Two unrelated layouts share the extension ("Kordinat dosyası"). The CKD lookup falls back to a
plain DOS file, which is how `orig/BOSCH.KOR` is found.

#### 3D/*.KOR – wall-set texture rectangles (`q3d_load_kor` 2700:1E06, tests/py/test_q3dview.py test_load_kor)

On entering a district `s3d_load_district` takes the wall-set number N of each of the four map
bytes 3..6 (kept at 16E0:9F10 + 2i; 0 = none; the name tables go up to 9 = merdust/merdalt) and
loads `duvarN.kor` into KOR slot i and `kapiN.kor` into slot 4 + i (16E0:B3E8 + 0Eh·slot).
Before each call it presets the slot to version 1, n 6, {hdr0, w, h} = {0, 200, 200} for walls
and {0EBh, 200, 200} for doors. A missing file is the normal case: the slot then draws with the
default piece table (16E0:B72A, 37 × 12h bytes).

| off | size | field |
|---|---|---|
| 0 | i16 | version; anything but 1 is fatal (line 1F7h, "Kordinat dosyasi versiyonu desteklenmiyor") |
| 2 | u16 | n = header bytes that follow; read unchecked into the slot from its +4 (n > 6 would overwrite the slot's piece pointer) |
| 4 | n | i16 hdr0, i16 w, i16 h: w × h = the view size the records were made for |
| 4+n | 37 × 14 | one record per wall piece i: i16 tx, ty, tw, th, dx, dy, flags |

The records are read only if `filelength − 4 > n`; then the slot gets a copy of the default
table in which piece i takes the source rectangle (tx, ty, tw, th), the size w × h = tw × th, the
offset (dx, dy) added to its position and `flags` (bit 0 mirrored, bit 1 scaled), clipped to the
view. If the view (100×100 low detail, 200×200) differs from the header's w × h, flag 2 is set on
every piece and size and offset are scaled by the default piece's w/tw and h/th (quirks: see
`q3d_load_kor`). Of the header only hdr0 is used later: wall pieces of a slot with hdr0 = 0 are
drawn with `q3d_draw_strip_a`, otherwise with `q3d_draw_strip_b` (side pieces take their texture
0EBh pixels further right when facing north or west; walls on sides 2/3 facing the viewer borrow
the opposite piece's rectangle, mirrored); the door animation's mirroring for facing 2/3 also
tests hdr0 ≠ 0 (`door_mirror` in `src/game/q3dview_anim.c`).

Shipped files, all with version 1, n 6 and header {0, 200, 200}:

| file | size | content |
|---|---|---|
| DUVAR4, DUVAR8 | 528 | header + 37 records (74 pieces: flags 0 ×41, 1 ×16, 2 ×17) |
| KAPI4, KAPI5, KAPI6, KAPI8 | 10 | header only: the slot keeps the default table, but hdr0 becomes 0 instead of the door preset 0EBh |

#### SAVAS/*.KOR, BOSCH.KOR – sprite frame boxes (`iso_draw_sprite` ovl4595:0387, tests/py/test_iso.py, tests/py/test_chargen.py)

72 bytes = 6 records × 6 i16: `x1, y1, x2, y2` (inclusive box of the frame in the sprite sheet
`<name>.CMP`), `ax, ay` (anchor). The sheet pixel (ax, ay) is drawn at (x − 4, y − 8) for the
unit position (x, y); mirrored frames keep that (the box is flipped around it). Unit frame
f = 1..8 (facing 1..4, +4 = attack; the death frame +100 only computes the position) maps to
record `f − 1` for f = 1..3, `f − 2` for f = 5..7, and records 1 / 4 mirrored for f = 4 / 8.
The HP bar goes at the box centre − 13 (0Dh) on both axes.

ISOSYS (`iso_load_battle_gfx` ovl4595:0570) loads the seven sprites of a battle (party slots
0Bh..0Eh from the portrait names, or `karafa` for all four with event flag 8Dh set and 8Fh
clear; monster groups 0Fh..11h from the short names 16E0:2185 + 9·type, `bosch` when that name
is empty) and each one's `.KOR` into a 1F8h-byte block (16E0:E337) at 48h·(slot − 0Bh); CHARGEN (`cg_load_char_graphics` 39E5:011F) loads one into a
48h-byte block. The loader reads whole files: a larger `.KOR` would overrun the next sprite's
record. Shipped: 52 files in SAVAS.CKD and `orig/BOSCH.KOR` (picture `IVIR/BOSCH.CMP`), all 72
bytes, every box inside its CMP. The anchor of record 3 (attack, facing 1) lies outside its box
in 19 of them (BOLONEZ, CEHALET, …). SAVAS pictures without a `.KOR` are not sprites: ANAKES,
DUVARIC1..9/A/B, TABLETUR, YERICON.

### RIX – blend table – verified (`blend_lookup` in src/engine/blit386.c, tests/py/test_blit386.py, tests/py/test_graphics.py test_rix; `tools/fmtcheck.py rix`)

32640 bytes = 256·255/2, no header: for every pair of different colours a ≠ b, the colour of
their 50 % mix. Packed upper triangle, row by row: with lo = min(a, b), hi = max(a, b) the entry
is at `lo·(509 − lo)/2 − 1 + hi` (16-bit), so row lo holds (lo, lo+1) … (lo, 255): (0, 1) at 0,
(0, 255) at 254, (1, 2) at 255, (254, 255) at 32639. blend(a, a) = a without a lookup.

Users (hand-written 386 code, 2599): `asm_tint_rect` 2599:0440 (every pixel p ≠ c of a
rectangle becomes blend(p, c); `gr_draw_strip` 0C7F:0A51 "RenkFiltre", fatal 70h when less than
7F80h bytes of the table are mapped), `asm_blend_span` 2599:04AD (`gr_blend3` 0C7F:08FC, the
cross-fades of `demo_show_picture`) and `asm_upscale_view2x` 2599:0500 (the in-between pixels of
the doubled low-detail 3D view). `sys_load_rix` 0AC6:1AA0 loads the file into an EMS block of
C001h bytes (16E0:E52F; the name is kept at E570 and a second load of the same name does
nothing). After palette changes (callers in 3DSYS, ISOSYS and FRPOS) `sys_build_aa_table`
0AC6:1A13 rewrites row 0 (pairs (0, i)) from the target palette 16E0:E5EF.

| file | loaded by |
|---|---|
| XD.RIX | `s3d_load_district`; `demo_story_scene` when it ends |
| XDINTRO.RIX | `ist_main` at start-up, `demo_intro_menu` (after acilisa.pal) |
| XDEMO.RIX | `demo_story_scene` |
| XDSON.RIX | `demo_ending` |

Content (checked against the palettes, not needed by the code): an entry is the palette colour
nearest to ((Ra + Rb) >> 1, (Ga + Gb) >> 1, (Ba + Bb) >> 1) by the sum of absolute differences,
the first one on a tie. XDSON.RIX follows that rule over SOOON.PAL in all 32640 entries, XD.RIX
over PAL3D.PAL (= orig/SONPAL.PAL) in all but 255, which all pair a colour with 254 or 255.
XDINTRO.RIX and XDEMO.RIX match ACILISA.PAL / DEMO1.PAL in only 15232 / 9923 entries: the palettes
they were made from are not identified. `sys_build_aa_table` uses the same average and distance
(`sys_find_color` 0AC6:195E) but lets the last minimum win.

### AMF – DSMI module – verified (`ampLoadAMF` 105B:008A in src/dsmi/amfload.c, tests/py/test_amp.py; `tools/fmtcheck.py amf`)

The music of MUZIK.CKD: 15 modules, all version 14. `ampLoadModule` (105B:07F3) checks "AMF",
copies 20 bytes of the name, forces 4 channels with the default pans for v1/v8, then
`ampLoadAMF` reads from the start of the file. Layout as the loader reads it (v14 offsets):

| off | size | field |
|---|---|---|
| 00 | 4 | `"AMF"` + version byte: 1, 8, 9, 10..14 accepted, else −3 |
| 04 | 32 | song name |
| 24 | u8 | instrument count |
| 25 | u8 | order count (MODULE "patternCount") |
| 26 | u16 | logical track count |
| 28 | u8 | channels (v ≥ 9) |
| 29 | 32 × i8 | channel pan −63..63, 100 = surround (v ≥ 12; v 9..11: 16 bytes, and below v11 replaced by the default table 16E0:85AA) |
| 49 | u8, u8 | BPM, speed (v ≥ 13; else 125, 6) |
| 4B | | orders: order count × { u16 rows (v14; else 64), channels × u16 track ref } |
| | | instruments: count × 65 bytes (v < 10: 59) |
| | | track map: track count × u16 stored-track number, 1-based |
| | | stored tracks: max(map) × { u16 count, u8 type, count × {u8 row, u8 cmd, u8 arg} } (v1: 3 more bytes each) |
| | | samples: raw 8-bit, by sample number; nothing follows |

A track ref r points at logical track r (1-based, 0 = none); logical track j plays the stored
track the map names (several may share one; count 0 = no data). Instrument (v ≥ 10):

| off | size | field |
|---|---|---|
| 00 | u8 | type |
| 01 | 32 | name |
| 21 | 13 | file name |
| 2E | u32 | sample number, 1-based (0 = none) |
| 32 | u32 | length (the loader uses the low word) |
| 36 | u16 | rate of C (c2spd) |
| 38 | u8 | volume 0..64 |
| 39 | u32, u32 | loop start, loop end (0 = no loop) |

v < 10: u16 length, u16 rate, u8 volume, u16 loop start, u16 loop end (FFFFh: no loop) from +32.
Sample k's length is taken from the instrument that first brings the sample count up to k; each
sample gets a block of length + 16 bytes, and with DDS's "EMSAlloc"/"EMSCopyTo" exports a sample
over 400h bytes moves to EMS.

Track entries are read by `ampReadTrackRow` (0EF6:0955): all entries with row ≤ the current
row; row FFh ends the track. cmd < 80h is a note (0 = stop, 7Fh = keep the note) with arg = volume
(FFh = keep); cmd 80h..97h are commands (80h instrument, 81h speed, 82h volume slide, 83h volume,
84h portamento, 86h tone portamento, 88h arpeggio, 89h vibrato, 8Ch pattern break, 8Dh position
jump, 94h note cut, 95h tempo, …; see `src/dsmi/amp.c`).

Shipped files: header 75 bytes, 31 instruments each, of which 3..13 are used (type 1, sample
numbers 1, 2, 3 … in instrument order, unused ones in between with type 0, number 0, length 0);
rate 8368 (89 instruments) or 8252 (2); 6 channels (ISTANBUL.AMF 8) panned −63, 63, 63, −63, …,
the rest 0; header tempo 125/6 (effect 95h sets 120 or 144 in 11 places); every order 64 rows;
6..108 logical over 6..89 stored tracks, all of type 0, 496 ending in `FF FF FF` and 4 empty.
Commands used: 80h, 81h, 82h, 83h, 84h, 86h, 88h, 89h, 8Ch, 8Dh, 94h, 95h. Each file ends
exactly after its last sample.

### ISOSYS battle state (DGROUP) – partly verified (`src/game/iso*.c`, tests/py/test_iso.py, test_iso_b/c/d.py)

The isometric battle (ovl4595 ISOSYS.L) keeps its whole state in DGROUP; nothing is
read from a battle file of its own (the battlefield is built from the 3D map, the monster
group records and GRP.DAT's party records).  All words are little-endian i16.

| where | what |
|---|---|
| A08A + 28h·c, c = 1..28 | unit: +00 hp, +02 vecize points, +04 facing 1..4 (+4 attack frame, +100 death frame), +06 template g, +08 x, +0A y, +0C last target, +0E side (1 party/allies, 2 enemies), +10 charmer, +12 to-hit, +14 dodge, +16..+1E five status slots (duration·100 + id), +20 on-hit vecize, +22 stun turns, +24 was hit, +26 ?. Units 1..24 monsters (group (c-1)/8), 25..28 the party |
| 9F18 + 2Eh·g | template (23 words). g 1..4 party members, 5..7 monster groups.  w1, w3, w9, w11, w16: kill value; w2 damage (hundreds·100 + base: random(h+1) + base); w5 armour; w7/w8 AP cost of attack 1/2 (−100 when > 100); w9 < 10 and w10 < 0: can be stunned |
| 1287 + 2Eh·t | monster type t (23 words). The battle set-up (`iso_battle.c`) fills template 4 + i of group i (type at 5BDA + 6i; above 100: an allied group, 100 is subtracted) with words 2..19 of its type, word 1 = w1 + difficulty (option BB39)·w0, then adds 1 to word 3; an empty group gets words 1..19 = 0 |
| 5BC2 + 6·g | group type of template g (footstep sounds; slayer weapons) |
| C2CE + 38h·x + 2·y | occupancy grid 28×28: unit number or 0 |
| C8EE + 38h·x + 2·y | terrain grid: 0 open, 1..9 floor tiles, ≥ 10 walls (picture 2616[t], top t+6); 6 and 8 and ≥ 10 block a step |
| CF0E + 36h·x + 2·y | mini-map colours, 27×27 |
| 99A6 + 5Ch·p | party record (GRP.DAT): +06 practice, +08 experience, +0C..+1A stats grown by iso_xp_progress, +3A+2k weapon in hand k |
| 9902 + 22h·p | growth marks: counter values at the last growth of each stat |
| 9B7C + 24h·p | race words: +16 favourite enemy group, +18 hated group, +02..+12 growth modifiers |
| 2632 + 2·side | "side has a living unit"; 632E = 1 when one side is gone |
| D4C0 + 2·i / D4FA + 2·c | initiative order / rolls |
| D54A, D54C | round, AP of the actor; D5DE actor, D56E/D56C attacker/defender, D576/D574 animated unit / kind (1 turn, 2 death, 3/4 hit, 5 talk, 6 miss) |
| D57A/D578/D57C | target cursor x, y, on; D580/D57E view origin (0..20) |
| 6393 + 0Dh·i | ten exclamations shown on a miss ("random(10)+1" → 631A, copied to A510) |

### SAKLAnnn.SAV (saved game) – verified (`src/game/s3d_save.c`, tests/py/test_s3d_save.py; orig/SAKLA001.SAV)

1524 bytes, written by s3d_write_save (303F:3DD0) as 22 raw DGROUP blocks, read back in the same
order by s3d_read_save (303F:3F95). Little-endian words; party records as in GRP.DAT above.

| offset | size | content (DGROUP) |
|---|---|---|
| 0 | 60 | description typed in the save menu, NUL-terminated; the bytes after the NUL are whatever the caller's buffer held (the loader skips all 60) |
| 60 | 2 | length of the next block = 0020h (the loader reads that many bytes) |
| 62 | 32 | B9CA: 256 FRPOS event flags (flag n = byte n/8, bit n%8) |
| 94 | 460 | 99A6: 5 × 5Ch character stat records (record 0 unused) |
| 554 | 740 | 9C26: 5 × 94h name / character records |
| 1294 | 170 | 98FC: 5 × 22h per-character table |
| 1464 | 7 | BB34: options (animations, battle speed, game music, battle music, sound effects, difficulty, big view) |
| 1471 | 2 | 3D5E: state bits (written, skipped by the loader, which recomputes them) |
| 1473 | 2+2+1 | B9F3 x, B9F1 y, B9F0 facing (0 north, 1 east, ...) |
| 1478 | 2 | BB6B: district 0..17 |
| 1480 | 2 | BB4D: music track |
| 1482 | 8 | BB6D: 4 words, BB73 = money |
| 1490 | 2+2+2 | 3D64 minute, 3D66 hour, 3D68 day (0..359) |
| 1496 | 11 | BBB7 |
| 1507 | 11 | BBAC |
| 1518 | 2+2 | BB8A, BB88: city map marker x, y |
| 1522 | 2 | BBAB: the doctor's fee byte written as a word (the second byte repeats BBAC[0]). A complete save keeps its fee on loading (the re-roll random(100)+101 only runs when the last read came up short: play5 loads a save with fee 106 and the original asks 106, docs/VERIFY.md); a new fee is rolled on arriving in a district |

After reading, the loader loads the portraits "s" + name (16E0:269B + 0Dh × record word +42h) into
pictures 47h-4Ah, loads the district and enters the 3D view; the view size comes from the big-view option.

### FRPOS event scripts PCn / PCSn – verified (`src/game/frpos*.c`, `tools/frposdis.py`, tests/py/test_frpos.py)

Every district n (1..18) has a map **PCn** and a script bank **PCSn** in FRPOS.CKD. Both are AMOS
banks: 20-byte header `"AmBk"`, BE16 bank number, BE16 flags, BE32 `80000000h | length`, `"Data    "`.
3DSYS keeps the whole map file in an EMS block (record pointer 16E0:E33F) and a pointer to the
cells, file byte 2Bh, at 16E0:B9EC (s3d_load_district locks byte 14h first to read the map header -
width, height, start ... - and then 2Bh, 303F s3d_district.c); the script file is the EMS block of 16E0:E34F. All offsets below are
file offsets (header included); the interpreter's PC is the word 16E0:BB98.

**Map cells.** Cell (x, y) is 15 bytes at file byte 2Bh + (y·width + x)·15 (byte 14h is the width, 15h the height). Bytes 11..14 are the event
key: each byte (≤ 20h, else fatal 51h) printed `%d` with a leading 0 below 10, giving 8 characters.
"00000000" = no event. Fixed keys (trigger 2 only): "31313127" doctor (for the BBAB fee a member whose health is −5 or lower gets health 1; anybody
else is "not dead, pick another"; too little money: "bu para yetmez!"), "31313128" food (random(5)+2 per living member, clears hunger word 11h),
"31313129" weapon shop (item exchange mode 1, icon 2Bh), "31313130" goods shop (mode 2, icon 2Ch),
"31313131" sets 3D5E bit 2 (any other key clears it). Otherwise the event number is, in 8-bit
arithmetic, `(b13 − m27)·32 − sign(b13 − m27) + b14 − m28` with m27/m28 = map file bytes 27h/28h.
fr_run_event(cell, trigger) is called by 3DSYS with trigger 1 (entering), 3 (leaving), 2 (search/use).

**Script bank.** At 14h: BE16 event count (event ≥ count is fatal 53h/54h), then the events, each
`[BE16 len][body]`. Body: BE16 command budget (→ BB93: commands that may run, decremented per
command and by skips, shared by the sections), BE16 offsets (from the body start) of the trigger 2
section, the trigger 3 section and a trailer; the trigger 1 section starts at body + 8. A section is
`[12 + trigger][flags → BB92]` and its commands (a wrong first byte prints "..baslik:%d Tarz:%d..",
then fatal 56h). The trailer is always `04 00`. Strings are `[len][len bytes]`, each byte stored 10
lower (text 0ACh+10 = 0B6h is the line break of the message font). The interpreter runs commands
while `BB93-- != 0` and no command stopped it; at the end it restores the panel and calls
s3d_redraw_view(B9C8). It returns 1 only after a teleport (op 11). ACC = 16E0:BB9E (also the
yes/no result of ui_dialog_2198), WHO = BB9C, the 256 flags are bits of B9CA (flag n: byte n/8,
bit n%8; saved in the .SAV).

| op | operands | effect |
|---|---|---|
| 1 | t, a, n | t=1: if flag a is clear skip n commands; t=2: if ACC ≠ a skip n; other t fatal 57h |
| 2 | str | clear panel, BBAA=0, message; unless the next opcode is 5, wait lines·100+140 |
| 3 | b | portrait/picture "ikon<b>.cmp" (+ .pal) in the view; B9C8 = 1 |
| 4 | – | clear panel, stop |
| 5 | l1, l2, s1, s2 | yes/no question (ui_dialog_2198) → ACC (0 = first) |
| 6 | n | skip n commands |
| 7 | f | set flag f |
| 8 | x, t1, c1, t2, c2, t3, c3 | battle with enemy groups (type, count) 1..3 at 5BDA/5BDC + 6i; all zero = three random groups (type from map byte 1Ch + random(8), count random(8)+1); after ISOSYS: district reloaded, battle tables A0B0/9F46 cleared; lost (632E = 2) or nobody alive → game over, stop, cancel the move |
| 9 | a, b, c, W1, W2, W3 | treasure: currencies BBA2/4/6 (added to BB6F/BB71/BB73), items in BB57.. (BE16 words), item exchange screen mode 0 |
| 10 | s1, s2 | one of the two messages (random(2) ? s1 : s2); wait as op 2 (lines·100+100) |
| 11 | d, BE16 pos, f | teleport: d < 100: district d−1 (reloaded with music and fades if it changes), y = pos / width, x = pos % width − 1, facing f % 4; d = 100: leave the district dialog; then redraw, B9C8 = 1, cancel the move, stop, return 1 |
| 12 | who, w, p | stat word w := w·p/100 (16-bit) for members 1..4 (who 0) or WHO (who 5, also BA89+11h·WHO = 1); for w = 1Dh (health) computed on health + 5; p < 100 on health plays voice 9 |
| 13, 14, 15 | b | nothing (never used) |
| 16 | – | cancel the move (BBA8 = 1), stop |
| 17 | c, str | member c speaks the text (0 = random(4)+1, 5 = WHO; WHO > 4 fatal 58h) |
| 18 | – | ACC = 3D5E bit 0 ? 0 : 1 |
| 19 | b | film: CD check when 3D5C = 1 (Esc gives up), fade out (b = 2 keeps the music), gfl_play(16E0:2D6E + 13·b), district reloaded |
| 20 | n | debug printf + getch, ACC = random(n)+1 (never used) |
| 21 | 6 × str | menu of six choices (fr_menu: click or first letter) → ACC 1..6 (never used) |
| 22 | w, kind, v | WHO := first member whose word w is < v (kind 0), > v (1), = v (other); none: stop |
| 23 | kind, w | WHO := member with the lowest (kind 0) / highest word w (fr_find_character) |
| 24 | – | the player picks a living member (F1-F4 / portrait) → WHO |
| 25 | m | music m (BB4D) with fades; m = 1 also resets 3D5E to 20h and the daylight fade; respects the music options BB36/BB38 |
| 26 | m | m = FFh: all money 0 first; then if m ≤ BB73: pay m, ACC = 1, else ACC = 0 |
| 27 | 40 × str | story scene 11 (demo_story_scene), frees the map block; the 40 strings are skipped unread |
| 28 | – | teach an aphorism (fr_teach_aphorism) |
| 29 | x, BE16 item | remove item word `item` from every inventory (and the hand slots pointing at it) |
| 30 | l, n, str | password: the player types up to l characters on the panel; unless it equals str (both lower-cased, accents stripped) skip n |
| 31 | n | debug printf + getch, wait n·50 (never used) |
| 32 | – | put the party back on its previous cell (BB4F/BB51), redraw, stop |
| 33 | – | leave-district dialog (s3d_leave_district), stop |

Other opcodes are fatal 5Dh. fr_skip_commands steps over commands with the operand sizes above
(op 30 skips `l + 1` bytes after its count byte: the `n` byte and the string); an unknown opcode there
is fatal 61h/62h. With 16E0:5C0B > 0 (0 in the game) every command prints "Komut no:%d" and waits
for a key; > 1 adds flag/ACC/skip traces. Census of the 18 shipped scripts (266 events): every
opcode except 13, 14, 15, 20, 21 and 31 is used.

**Item exchange screen** (fr_item_exchange, mode 0 treasure / 1 weapons / 2 goods). The shop stock
(10 slots, words BB57..) of mode 1/2 is kept per district in the bytes BBB8../BBAD.., byte BBB7 /
BBAC being its price base (random on the first visit: 4..11 items, ids random(31)+1 resp.
random(10)+21h). Buying: 2·price(id) + base − trade-in, where price is the byte table 16E0:1041 + id
and the trade-in of the inventory item swapped out is 2·price + 5 − random(10) − 1 (at least 1);
selling (S, zone 28) pays 2·price + 5 − random(10) + 100·(item/100) − 1 (key) or + 1 (mouse).
B / zone 27 changes the other currencies: BB6F·10 + random(9) and BB71·5 + random(2) kIvrIk.
