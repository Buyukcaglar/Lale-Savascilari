# gameA – keşif, 3B, karakter yaratma, demolar, FRPOS (envanter) / gameA – exploration, 3D, chargen, demos, FRPOS (inventory)

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Segmentler: ovl2700 (3DQUICK.L), seg122B, ovl303F (3DSYS.L), ovl39E5 (CHARGEN.L), ovl3C2D (DEMOLAR.L),
ovl3FD1 (FRPOS.L). Adlar `gameA.tsv` dosyasındadır (157 fonksiyon).

### 0. KRİTİK: kaplama dökümleri yanlış eşlenmiş (araç hatası)

`build/ist_flat.bin`, `analysis/ghidra_ist/ist_fpu.bin` ve dolayısıyla **ovl303F ile sonraki tüm kaplamalar
(overlay)** için her Ghidra dökümü (listing) ve taslağı yanlış baytlar içeriyor. Yalnızca ovl2700 doğru.

Neden, `tools/exeload.py`: `mem[oseg*16 : oseg*16+len(code)] = code` satırı `len(mem) < oseg*16` iken
çalışıyor. Python dilimi kırpıyor, bu yüzden her kaplama `mem`'in o anki sonuna **ekleniyor**. Kaplamalar
0x27000'den itibaren bayt bayt bitişik paketleniyor; oysa segmentleri paragraf hizalı ve aralıklı atanıyor.
N kaplamasının gerçek kodu düz (flat) görüntüde `seg*16 - shift` adresindedir:

| ovl | 2700 | 303F | 39E5 | 3C2D | 3FD1 | 4595 | 5313 | 5447 | 5511 | 559A | 55CC | 5646 | 569A | 5737 | 5784 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| kayma | 0 | 1A | 31 | 48 | 5F | 79 | 8D | AC | C2 | D5 | F0 | 109 | 119 | 138 | 14A |

`ist_map.json` içindeki geçiş kodu (thunk) hedefleri doğrudur (bunlar saplama (stub) ofsetleridir), bu yüzden
`F_303F_xxxx` adları doğru; ama altlarındaki döküm metni başka bir yerden gelen koddur. Örneğin 3FD1:0000'daki
`ovl3FD1.asm` `MOV ES,BX` ile başlıyor. Düzeltme kayıtları (fixup) paketlenmiş veriye göre uygulandığı için her
bloğu geri taşımak işe yarıyor. Bu envanter için düzeltilmiş bir görüntüyü bu yolla yeniden oluşturdum,
öykünücü-INT→x87 çözümlemesini (INT 34h–3Bh→D8–DF, 3Ch→segment önekli biçim, 3Dh→FWAIT) yeniden çalıştırdım
ve görüntüyü capstone ile, CS: atlama tabloları (jump table) dahil, özyinelemeli iniş (recursive descent)
yöntemiyle ayrıştırdım (disassembly). Düzeltme exeload'da tek satırdır (atamadan önce `mem`'i `oseg*16`'ya
kadar doldurmak), ardından Ghidra hattı (pipeline) yeniden çalıştırılır. **Bu yeniden çalıştırmadan önce
(hangi grup tarafından olursa olsun) yapılmış tüm kaplama analizleri geçersiz sayılmalıdır.**

### 1. Modül rolleri ve oyun döngüsü

* **seg122B, 3DSYS değildir.** Bu, **Borland VROOMM kaplama yöneticisinin çalışma zamanı (runtime)** kodudur:
  INT 3Fh tuzak kancası, EXE'yi yeniden açma/konumlama/okuma, düzeltme kayıtları, tampon boşaltma,
  `_OvrInitEms` (122B:08EF, INT 67h, "EMMXXXX0" IOCTL) ve `_OvrInitExt` (122B:0AB9, XMS INT 2Fh 4300h/4310h,
  INT 15h). main, 122B:08EF'i çağırır. Taşımada (port) bunun yerine hiçbir şey konmaz.
* **main (seg0546:057B)**, `demo_intro_menu(0xB)` (3C2D:054D) çağırır. Giriş bölümü demo.amf eşliğinde
  acilis*/comp4*/istb resimlerini oynatır; menü *yeni oyuna başla / eski oyuna devam et / çabuk başla /
  dos'a çık* seçeneklerini sunar. 1..6 döndürür ve main, 0546:0958'deki CS atlama tablosu üzerinden dallanır:
  * çabuk başla: `cg_load_party("grphazir.dat")`;
  * yeni oyun: `cg_main` (39E5:1AA2), ardından `cg_load_party("grp.dat")`;
  * sonra ekip simgeleri yüklenir, `s3d_load_district(bb6b=0, 1)` ve `s3d_enter_view` çalışır ve oyun
    durumu ilklendirilir: para sözcükleri BB6F/BB71/BB73=10, seçenekler BB34..BB39, BB4D=1.
    Ardından **`s3d_explore_loop` (303F:8524)** çalışır;
  * diğer durumlar: oyun yükleme, jenerik (`demo_credits_exit` 3C2D:217C), çıkış.
* **Keşif döngüsü 303F:8524**, her yinelemede:
  * `kbhit` → `s3d_handle_key` (303F:0FFC, switch @303F:1148). Bu; `s3d_try_move` (303F:0000) ile hareketi,
    karakter ekranı 4F3A'yı, seçenekler 7A71'i, kaydet/yükle 7F2F'yi, kamp 4353'ü, konum raporu 0BDB'yi ve
    hile modunu ("Cit mod", CHEAT.CMP) işler.
  * saat `s3d_advance_clock` 39FD ve durum satırı 4D22.
  * açlık mesajları ("ben acIktIm…", ölüm "aah!! acIktIm demiStim...elveda.").
  * rastgele karşılaşmalar: "zam1/zam2/zam3: m[h][i]…" hata ayıklama dizgeleri **ISOSYS savaşı
    `F_4595_B40A`** çağrısından önce gelir; ardından semt yeniden yüklenir (1927) ve görünüm yeniden kurulur (2521).
    ISOSYS 4595:20E0/2C7B başka denetimler için çağrılır.
  * harita hücresi olayları: `fr_run_event` (3FD1:0000).
  * rastgele hazine: `fr_item_exchange` (3FD1:2F21).
  * otomatik harita (automap) `q3d_draw_automap` (2700:5F32).
  * ekip öldü → `demo_game_over` (3C2D:19F1); bir bayrağa bağlı son → `demo_ending` (3C2D:1B76).
  * DGROUP işaretçileri 16E0:9650 ve 16E0:964C üzerinden dolaylı uzak (far) çağrılar (motor kancaları).
* **Hareket** `s3d_try_move(dx,dy,facing)`: geçerli hücrenin `facing%4` kenarına ait duvar baytını okur ve
  türünü 16E0:9F0E'deki sözcük tablosunda arar (indis = bayt; tür 9 = kapı/geçit). Terk edilen hücre için
  `fr_run_event(cell, 3)`, ardından varılan hücre için `fr_run_event(newcell, 1)` çağırır. `fr_run_event`,
  hareketi iptal etmek için BBA8'i ayarlayabilir. Sonra q3d animasyonları
  (647B/69E2/798C/80D0/8197/8896) ve `s3d_redraw_view` çalışır.
* **Semtler arası yolculuk**: FRPOS op 33 ya da bir tuş → `s3d_leave_district` (303F:765C) "semtten CIkacak
  mIsInIz?" diye sorar → `demo_city_map_travel` (3C2D:2DE2, isthar*.cmp; "trafik tIkalI", "Sehire mi?
  kanalizasyona mI? (s/k)") → GFL videosu `F_5313_1270` (semt giriş/çıkış adları aşağıdaki semt tablosundan
  gelir) → `s3d_load_district`.
* **Video (GFL)**: `F_5313_1270`; 303F:765C, 3C2D:054D/1B76/217C/3790 ve önce bir CD mesajı gösteren
  FRPOS op 19 tarafından çağrılır.
* **3B çizici (ovl2700)**: `q3d_render_view` (3932) beş katman çiziciyi (renderer) çağırır. `q3d_draw_cell_wall`
  (3969) harita hücresi baytları üzerinde çalışır; hata mesajları "Beklenmedik duvar!!!!; Harita offset: %d" ve
  "OrspUm[%d]"dır. Duvar/doku kümeleri, `q3d_load_kor` (1E06) ile yüklenen `.KOR` dosyalarından gelir. Doku
  adları 16E0:2978'deki (ve 16E0:29FA'daki) 13 baytlık dizgelerdir ve harita başlığının [3..6] baytlarıyla
  indislenir.

### 2. Veri yapıları (DGROUP = 16E0)

#### 2.1 Semt tablosu 16E0:2E31: 18 × 29 bayt, indis = BB6B (0..17)

| ofs | tür | anlamı |
|---|---|---|
| +0 | far char* | harita/betik temel adı "pc1".."pc18" (harita = PCn, betik = BB6B+1 ile `pcs%d`) |
| +4 | far char* | semt adının -in hâli (genitive) ("mecidiyekOy'Un"), konum raporunda kullanılır |
| +8,+12,+16,+20 | far char* | GFL adları: gündüz-giriş, gece-giriş, gündüz-çıkış, gece-çıkış ("mec-d-in" …) ya da NULL |
| +24 | far char* | ufuk resmi "ufuk1.cmp"/"ufuk2.cmp" |
| +28 | u8 | bilinmiyor (0, 16, 23, 119, 55, 49, 60 …) |

Semt 0 = otopark (başlangıç), 12/13/15 = kanalizasyon, 14 = Topkapı, 16 = Kız Kulesi.

#### 2.2 Harita dosyası PCn (FRPOS.CKD)

Hem PC hem PCS dosyaları **AMOS bankalarıdır (bank)**: 20 baytlık başlık `"AmBk"`, u16 BE banka numarası,
u16 BE bayraklar, u32 BE (0x80000000 | uzunluk), `"Data    "`. Oyun dosyayı yükler (`F_5447_093A`), eşler
(`F_1154_0688`, EMS) ve 20. bayta işaret eden bir uzak işaretçiyi **16E0:B9EC/B9EE**'de tutar. Tanıtıcı
(handle) 16E0:E33F'te tutulur (PCS: 16E0:E34F).

* Baytlar [0]=genişlik (→B9EB), [1]=yükseklik (→B9EA), [2]→BA86, [3..6]=duvar yuvası başına doku/KOR kümesi
  indisleri, [7]-1=başlangıç x (→B9F3), [8]-1=başlangıç y (→B9F1). Başlangıç bakış yönü B9F0, konumun
  kenarlara göre durumundan türetilir.
* **Hücreler**: her biri 15 bayt, `cell_off = (y*width + x)*15`. Hücre 0, yukarıdaki başlık baytlarıyla çakışır.
  * [2*side] = 0..3 kenarı için duvar türü, 16E0:9F0E'de aranır; 9 = kapı.
  * [2*side+1] = duvar parametresi.
  * [11..14] = olay anahtarı baytları. `fr_run_event` her baytı 2 ondalık basamak olarak 8 karakterlik bir
    anahtara yazar. "00000000" olay yok demektir. "31313127..31" sabit kodlanmış dükkânlar/NPC'lerdir: 27 havalı
    mehtap, 28 yimmekCi (yiyecek), 29 silahCI (silah), 30 gerekli şeyler, 31 bir başkası. Aksi hâlde olay
    numarası, harita tanıtıcısından 0x27/0x28'de okunan iki sözcük (`[bp-0x34]`, `[bp-0x36]`) kullanılarak
    `((b11-b13)<<5) - sgn(b11-b13) + b12 - b14` olur; bu formülü orijinale karşı doğrulayın.

#### 2.3 Ekip (party) (kayıt dosyası açısından kritik)

* **Nitelik (stat) kaydı, 92 bayt (0x5C), dizi 16E0:99A6'da, indis 0..4** (1..4 kullanılır; indis 0 da
  kaydedilir). 46 sözcük; kodun kullandığı alanlar (ofset: başvuru sayısı): 00:83, 02, 04, 06, 08, 0A, 0C, 0E, 10,
  12:27, 14, 16, 18, 1A, 1C, 1E, 20, 22:21, 24, 26, 28, 2A:24, 2C, 36, 38:17, 3A:50, 3C:33, 3E, 40,
  42:63, 44:11, 5A.
  * +02: ırk/sınıf indisi. Adı 16E0:2638'deki 11 baytlık dizgelerdir (1 = "insan", 2 = "öğrenci").
  * +0A/+0C: taban değerler; GRP.DAT yazılırken +38/+3A'ya kopyalanır.
  * +3A: geçerli sağlık: >0 iyi, -5..0 baygın, ≤-5 ölü. FRPOS doktoru bunu 1 yapar.
  * +22: açlık sayacı (yemek yiyince sıfırlanır).
  * +24: random(25)+random(25)+10*chargen[+0]+150.
  * +26: random(5)+random(5)+[+24]+10*chargen[+2]-110-10*([+0A]-1).
  * +42: portre indisi; simge adı 16E0:269B'deki 13 baytlık dizgelerdir, 0x47+i simgesi olarak yüklenir.
  * karakter yaratma (chargen) çalışma tablosu: 16E0:9B74'te karakter başına 36 bayt.
* **Ad kaydı, 148 bayt (0x94), dizi 16E0:9C26'da, indis 0..4**:
  * +00 char[37] ad;
  * +25..+49 örneklerde kullanılmıyor/sıfır;
  * +4A ırk adı (yüklemede 16E0:2638 tablosundan kopyalanır);
  * +6F char[37] bilinen vecize bayrakları, '1'/'0' ×36 + NUL, "1000…0" olarak ilklendirilir.
* Seçenek baytları 16E0:BB34..BB3A, para/yiyecek sözcükleri 16E0:BB6D/BB6F/BB71/**BB73 (para)**.
  Konum B9F3 (x), B9F1 (y), B9F0 (bakış yönü 0..3), semt BB6B, durum bitleri 16E0:3D5E.

#### 2.4 GRP.DAT / GRPHAZIR.DAT (664 bayt), cg_save_party / cg_load_party

i = 1..4 için: `name_rec[i][0..36]` (37 bayt), ardından `name_rec[i][0x6F..0x93]` (37 bayt); toplam 296 bayt.
Sonra i = 1..4 için: `stat_rec[i]` (92 bayt); 368 bayt. Okuma/yazma MEMMAN/FILEMAN dosya çağrılarını kullanır:
`F_569A_0342` (aç, kip 2=okuma 4=yazma), `F_569A_053C`/`F_569A_06F7`
(read/write(long size, far buf, fd)) ve `F_569A_04CA` (kapat).

#### 2.5 Kayıt dosyası `saklaNNN.sav` (1524 bayt), s3d_write_save 303F:3DD0 ve s3d_read_save 303F:3F95

| dosya ofs | boyut | kaynak | notlar |
|---|---|---|---|
| 0 | 60 | çağıranın tamponu | açıklama (`"c1\0…"`; **NUL'dan sonraki baytlar ilklendirilmemiş yığın verisidir** – bayt bayt aynı kayıtlar için bunları koruyun) |
| 60 | 2 | yerel = 0x0020 | sürüm/boyut sözcüğü |
| 62 | 32 | 16E0:B9CA | 256 olay bayrağı (bit n = bayt n/8, bit n%8) |
| 94 | 460 | 16E0:99A6 | 5 × 92 nitelik kaydı |
| 554 | 740 | 16E0:9C26 | 5 × 148 ad kaydı |
| 1294 | 170 | 16E0:98FC | bilinmeyen tablo (örnek: 34 bayt adımla 0x32,0x44,0x36,0x35 sözcükleri; muhtemelen karakter başına veri, 5×34) |
| 1464 | 7 | 16E0:BB34 | seçenekler (örnek 01 05 01 01 01 00 01) |
| 1471 | 2 | 16E0:3D5E | durum bitleri |
| 1473 | 2 | B9F3 | x |
| 1475 | 2 | B9F1 | y |
| 1477 | 1 | B9F0 | bakış yönü |
| 1478 | 2 | BB6B | semt |
| 1480 | 2 | BB4D | (yeni oyunda 1 yapılır; FRPOS op 25 yazar) |
| 1482 | 8 | BB6D | 4 sözcük; BB73 = para |
| 1490 | 2+2+2 | 3D64, 3D66, 3D68 | takvim/saat (örnek 25, 10, 349) |
| 1496 | 11 | BBB7 | |
| 1507 | 11 | BBAC | |
| 1518 | 2 | BB8A | |
| 1520 | 2 | BB88 | (BB88/BB8A = 16E0:4A86 tablosundaki, semt başına 8 bayt olan bir dikdörtgenin orta noktaları) |
| 1522 | 2 | BBAB | **bir bayt değişkenin 2 baytlık yazımı**: BBAC[0]'ı da yeniden saklar. Semt yüklenirken BBAB = random(100)+101 |

orig/SAKLA001.SAV ile birebir eşleşir (1524 bayt).

### 3. FRPOS betikleri (PCSn) ve yorumlayıcı (fr_run_event 3FD1:0000)

* PC = sözcük 16E0:BB98; PCS bankasında 20 baytlık başlığı da içeren ofset. `fr_fetch_byte`
  (4E63) EMS üzerinden okur. Dizgeler `[len u8][len bytes]` biçimindedir ve **bayt başına +10** ile çözülür
  (0x16→' ', 0xAC→'\n').
* PC=0x14'ten itibaren düzen:
  * BE16 olay sayısı;
  * ardından olaylar, her biri `[BE16 len][body]`. e olayı, e tane gövde atlanarak bulunur.
  * Gövde BE16 komut sayısıyla (→BB93) başlar, ardından 2,3,4 tetik türleri için BE16 giriş ofsetleri gelir.
    Tür 1 bu 6 bayttan sonra başlar; t≥2 türü `offset - 2t` ekler.
  * Her bölüm 12+tür değerinde bir baytla (denetlenir: "..baslik:%d Tarz:%d..") ve →BB92 baytıyla başlar.
* Tetik türleri: 1 = hücreye girme, 3 = hücreden çıkma (s3d_try_move), 2 = dükkânlar/sabit kodlanmış
  (arama/kullanma).
* Döngü, `BB93-- != 0` olduğu ve çalışma sürdüğü sürece komutları yürütür. 16E0:5C0B baytı > 0 iken hata
  ayıklama izleri basılır ("Komut no:%d", "Atla:%d").
* İşlenen (operand) boyutları (`fr_skip_commands` 4ECB, tablo @3FD1:505B) ve anlamları (switch @3FD1:0911,
  tablo @3FD1:208C):

| op | işlenenler | anlamı |
|---|---|---|
| 1 | t,a,n | IF: t=1 → a bayrağı temizse n komut atla; t=2 → ACC(BB9E)≠a ise n atla. Diğer t: ölümcül hata |
| 2 | str | metni göster; sonraki op ≠5 ise lines*100+140 tık bekle |
| 3 | b | b portre ikonunu göster; görünümü kirli olarak işaretle (B9C8) |
| 4 | – | mesajı temizle, END |
| 5 | l1,l2,s1,s2 | evet/hayır sorusu → ACC (0 = ilki) |
| 6 | n | n komut atla (ileri GOTO) |
| 7 | f | f olay bayrağını ayarla |
| 8 | 7 bayt | SAVAŞ: düşman grubu 16E0:5BDA.. doldurulur (yerleşim için rand) → `F_4595_B40A`, semt yeniden yüklenir; ekip yok olursa → oyun sonu |
| 9 | 9 bayt | eşya değişimi/dükkân/hazine (BBA2/4/6 baytları, BB57/59/5B sözcükleri) → `fr_item_exchange` |
| 10 | str,str | iki metinden birini rastgele göster |
| 11 | 4 bayt | IŞINLANMA (TELEPORT): semt, x, y, bakış yönü; semti yükler, müzik, yeniden çizim |
| 12 | 3 bayt | bir ekip üyesinin +00 nitelik sözcüğünü değiştir (ekle/çıkar/ata) ve paneli yeniden çiz |
| 13,14,15 | b | işlem yok (işlenen yok sayılır) |
| 16 | – | BBA8=1 yap (hareketi iptal et), END |
| 17 | b,str | karaktere özgü metin ("kom1:%d, adamno:%d") |
| 18 | – | ACC = !(3D5E & 1) |
| 19 | b | GFL videosu oynat (CD istemi), yeniden çiz |
| 20 | n | ACC = random(n) |
| 21 | 6 str | 6 seçenekli menü (`fr_menu`) → ACC |
| 22 | 3 bayt | karakter bul (`fr_find_character`) → BB9C |
| 23 | 2 bayt | karakter bulmanın bir çeşidi → BB9C ("adambul(0,%d,%d)") |
| 24 | – | oyuncudan bir karakter seçmesini iste → BB9C ("bir adam seCiniz…") |
| 25 | b | BB4D/müzik/durum 3D5E ayarla, palet değişiklikleri |
| 26 | b | para öde: ACC = 0 tamam / 1 yetersiz (BB73) |
| 27 | 40 str | hikâye slayt gösterisi `demo_story_scene` |
| 28 | – | vecize öğret (`fr_teach_aphorism`) |
| 29 | 3 bayt | bir üyenin +44/+3A niteliğini değiştir (iyileştirme/hasar) |
| 30 | n,str(n+1) | metin dosyası / çok sayfalı gösterim (5784 yazı tipi) |
| 31 | n | n bekle |
| 32 | – | yeniden çiz, END |
| 33 | – | semtten çıkma diyaloğu, END |

Örnek (PCS1 olay 0, tür 1): `01 01 01 01` (bayrak 1 temizse 1 atla), `04` (son), `02 "dISarIdan
garip hIrIltIlar…"`, `05 04 05 evet hayIr`, `01 02 01 01` (ACC≠1 ise 1 atla), `10` (hareketi iptal et, son),
`21` (semtten çık).

### 4. RNG, kayan nokta, zamanlama

* **RNG**: her çağrı Borland `rand()`'dir (0000:1779, tohum 16E0:94C4/94C6) ve `random(n)` makrosu
  `(int)((long)rand()*n/0x8000)` (LXMUL 0000:1242 + LDIV 0000:12DF) üzerinden kullanılır. Çağrı noktaları
  (sayı):
  * 2700:9078 (2)
  * 303F: 0715 (1), 1927 (1), 2C69 (3), 3F95 (1), 4353 (3), 4A27 (1), 765C (2), 8524 (15)
  * 39E5: 0000, 035D, 0437 (26), 1AA2
  * 3C2D: 054D (3), 1108 (3), 1F32 (3)
  * 3FD1: 0000 (6, op 8/10/20 ve yiyecek fiyatı), 2104, 2B1A (2), 2F21 (10)

  `random(25)+random(25)` gibi iki terimli toplamlarda önce soldaki çağrı çalışır.
* **FP**: neredeyse tamamı ovl2700'dedir:
  * ön hesaplama 0328 (369 x87 işlemi), 0DA8/2413 (213), 15D7/2C42 (216), yardımcılarla birlikte 02B0/0D1D
    (kesen (truncating) kontrol sözcüğüyle ftol 0000:1099);
  * .KOR yükleyici 1E06 (32);
  * döndürmeler 8197/8896 (30/32);
  * gölgelendirme 9078 (14);
  * adım animasyonları (her biri 4–6).

  303F yalnızca 9 işlem kullanır (0000, 07B6). main, başlangıçta 0328, 15D7 ve 2C42 ön hesaplamalarını
  çalıştırır; bu tablolar yeniden hesaplamak yerine bir DOSBox bellek dökümünden çıkarılmaya iyi adaylardır.
* **Zamanlama**: beklemeler `F_05DC_2DCA(ticks)` üzerinden yapılır. Metin beklemeleri lines*100+140'tır. Ana
  döngü saat/açlık için `kbhit`'i ve `cmp ax,0x3E8`/`0x5DC` (1000/1500 tık) zaman karşılaştırmalarını yoklar.
  q3d animasyonları 2700:8FD4 (09D4:0E4A) üzerinden eşzamanlanır.

### 5. Tanımlanan RTL (seg0000), diğer gruplar için

1477 PADD (huge işaretçi toplama), 12DF LDIV ailesi (cx=0..3), 1242 LXMUL, 138D LXLSH, 127A SCOPY, 1502 yığın
SCOPY, 1099 ftol, 1779 rand, 3F58 itoa, 48B4 sprintf, 4253 printf, 49CC/53A3 strlen, 49A3/537A strcpy,
48F7/530B strcat, 4973/534A strcmp, 52A4/4001 memcpy, 4936 strchr, 49EB/53C2 strncpy, 25D6 farmalloc,
24C2 farfree, 29D7 getch, 2E45 kbhit, 1F3F bioskey, 112F/1162 findfirst/findnext. Motor:
559A:0000 ölümcül hata(file,line,msg), 0DB7:022F denetimli uzak bellek bırakma(ptr,line,file),
09D4:0A81/0B22 bir tanıtıcının EMS eşlemesi/eşlemesinin kaldırılması, 05DC:2948 mesaj, 05DC:2198 evet/hayır,
05DC:1EB9 mesajı temizle.

### 6. Açık sorular

* Nitelik kaydı alanlarının çoğunun kesin anlamı (etiketler dizge olarak değil, grafik olarak çiziliyor).
* 16E0:98FC'deki 170 baytlık blok; BBAC/BBB7 (her biri 11 bayt); BB88/BB8A.
* q3d katmanlarının rolleri ve dört animasyon fonksiyonu yalnızca tahmin; 0DA8/2413/624A'nın doğrudan
  çağıranı yok (belki fonksiyon işaretçileri).
* Semt tablosunun [28] baytı; harita başlığının [2] baytı.
* FRPOS op 12, 22, 23, 25, 29 ve 30 için ayrıntılı işlenen çözümlemesi gerekiyor.

## English

Segments: ovl2700 (3DQUICK.L), seg122B, ovl303F (3DSYS.L), ovl39E5 (CHARGEN.L), ovl3C2D (DEMOLAR.L),
ovl3FD1 (FRPOS.L). Names are in `gameA.tsv` (157 functions).

### 0. CRITICAL: the overlay listings are mis-mapped (tooling bug)

`build/ist_flat.bin`, `analysis/ghidra_ist/ist_fpu.bin` and therefore every Ghidra listing/draft for
**ovl303F and all later overlays** contain the wrong bytes. Only ovl2700 is correct.

Cause, `tools/exeload.py`: `mem[oseg*16 : oseg*16+len(code)] = code` runs while `len(mem) < oseg*16`.
Python clamps the slice, so each overlay is **appended** at the current end of `mem`. The overlays end up
packed byte-contiguous from 0x27000, while their segments are assigned paragraph-aligned with a gap.
The true code of overlay N is at flat `seg*16 - shift`:

| ovl | 2700 | 303F | 39E5 | 3C2D | 3FD1 | 4595 | 5313 | 5447 | 5511 | 559A | 55CC | 5646 | 569A | 5737 | 5784 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shift | 0 | 1A | 31 | 48 | 5F | 79 | 8D | AC | C2 | D5 | F0 | 109 | 119 | 138 | 14A |

The thunk targets in `ist_map.json` are correct (they are stub offsets), so the names `F_303F_xxxx` are
right, but the listing text under them is code from somewhere else. For example, `ovl3FD1.asm` at 3FD1:0000
starts with `MOV ES,BX`. Fixups were applied relative to the packed data, so moving each block back works.
For this inventory I rebuilt a corrected image this way, re-ran the emulator-INT→x87 decoding
(INT 34h–3Bh→D8–DF, 3Ch→seg-override form, 3Dh→FWAIT) and disassembled it with capstone using
recursive descent, including CS: jump tables. The fix is one line in exeload (pad `mem` to `oseg*16`
before assigning), then re-run the Ghidra pipeline. **All overlay analysis done before that re-run
(by any group) should be treated as invalid.**

### 1. Module roles and the game loop

* **seg122B is not 3DSYS.** It is the **Borland VROOMM overlay manager runtime**: INT 3Fh trap hook,
  EXE reopen/seek/read, fixups, buffer eviction, `_OvrInitEms` (122B:08EF, INT 67h, "EMMXXXX0" IOCTL) and
  `_OvrInitExt` (122B:0AB9, XMS INT 2Fh 4300h/4310h, INT 15h). main calls 122B:08EF. In the port it is
  replaced by nothing.
* **main (seg0546:057B)** calls `demo_intro_menu(0xB)` (3C2D:054D). The intro plays acilis*/comp4*/istb
  pictures with demo.amf, and the menu offers *yeni oyuna başla / eski oyuna devam et / çabuk başla /
  dos'a çık*. It returns 1..6, and main dispatches through the CS jump table at 0546:0958:
  * quick start: `cg_load_party("grphazir.dat")`;
  * new game: `cg_main` (39E5:1AA2), then `cg_load_party("grp.dat")`;
  * then the party icons are loaded, `s3d_load_district(bb6b=0, 1)` and `s3d_enter_view`, and the
    game state is initialised: money words BB6F/BB71/BB73=10, options BB34..BB39, BB4D=1.
    Then **`s3d_explore_loop` (303F:8524)** runs;
  * other cases: load game, credits (`demo_credits_exit` 3C2D:217C), exit.
* **Exploration loop 303F:8524**, per iteration:
  * `kbhit` → `s3d_handle_key` (303F:0FFC, switch @303F:1148). This handles movement via
    `s3d_try_move` (303F:0000), the character screen 4F3A, options 7A71, save/load 7F2F, camp 4353,
    location report 0BDB and the cheat mode ("Cit mod", CHEAT.CMP).
  * clock `s3d_advance_clock` 39FD and the status line 4D22.
  * hunger messages ("ben acIktIm…", death "aah!! acIktIm demiStim...elveda.").
  * random encounters: the debug strings "zam1/zam2/zam3: m[h][i]…" sit before **ISOSYS battle
    `F_4595_B40A`**; afterwards the district is reloaded (1927) and the view rebuilt (2521).
    ISOSYS 4595:20E0/2C7B are called for other checks.
  * map cell events: `fr_run_event` (3FD1:0000).
  * random treasure: `fr_item_exchange` (3FD1:2F21).
  * automap `q3d_draw_automap` (2700:5F32).
  * party dead → `demo_game_over` (3C2D:19F1); a flag-gated ending → `demo_ending` (3C2D:1B76).
  * indirect far calls through the DGROUP pointers 16E0:9650 and 16E0:964C (engine hooks).
* **Movement** `s3d_try_move(dx,dy,facing)`: it reads the wall byte of the current cell for the side
  `facing%4` and looks up its kind in the word table at 16E0:9F0E (index = byte; kind 9 = door/passage).
  It calls `fr_run_event(cell, 3)` on the cell being left, then `fr_run_event(newcell, 1)` on the
  destination. `fr_run_event` may set BBA8 to cancel the move. Then the q3d animations
  (647B/69E2/798C/80D0/8197/8896) and `s3d_redraw_view` run.
* **District travel**: FRPOS op 33 or a key → `s3d_leave_district` (303F:765C) asks "semtten CIkacak
  mIsInIz?" → `demo_city_map_travel` (3C2D:2DE2, isthar*.cmp; "trafik tIkalI", "Sehire mi?
  kanalizasyona mI? (s/k)") → GFL video `F_5313_1270` (district intro/outro names come from the
  district table below) → `s3d_load_district`.
* **Video (GFL)**: `F_5313_1270` is called from 303F:765C, 3C2D:054D/1B76/217C/3790 and FRPOS op 19,
  which shows a CD message first.
* **3D renderer (ovl2700)**: `q3d_render_view` (3932) calls five layer renderers. `q3d_draw_cell_wall`
  (3969) works on the map cell bytes; its errors are "Beklenmedik duvar!!!!; Harita offset: %d" and
  "OrspUm[%d]". Wall/texture sets come from `.KOR` files loaded by `q3d_load_kor` (1E06). The texture
  names are 13-byte strings at 16E0:2978 (and 16E0:29FA), indexed by the map header bytes [3..6].

### 2. Data structures (DGROUP = 16E0)

#### 2.1 District table 16E0:2E31: 18 × 29 bytes, index = BB6B (0..17)

| off | type | meaning |
|---|---|---|
| +0 | far char* | map/script base name "pc1".."pc18" (map = PCn, script = `pcs%d` with BB6B+1) |
| +4 | far char* | district genitive name ("mecidiyekOy'Un"), used by the location report |
| +8,+12,+16,+20 | far char* | GFL names: day-in, night-in, day-out, night-out ("mec-d-in" …) or NULL |
| +24 | far char* | horizon picture "ufuk1.cmp"/"ufuk2.cmp" |
| +28 | u8 | unknown (0, 16, 23, 119, 55, 49, 60 …) |

District 0 = otopark (start), 12/13/15 = kanalizasyon (sewers), 14 = Topkapı, 16 = Kız Kulesi.

#### 2.2 Map file PCn (FRPOS.CKD)

Both PC and PCS files are **AMOS banks**: a 20-byte header `"AmBk"`, u16 BE bank number, u16 BE flags,
u32 BE (0x80000000 | length), `"Data    "`. The game loads the file (`F_5447_093A`), maps it
(`F_1154_0688`, EMS) and keeps a far pointer to byte 20 in **16E0:B9EC/B9EE**. The handle is kept in
16E0:E33F (PCS: 16E0:E34F).

* Bytes [0]=width (→B9EB), [1]=height (→B9EA), [2]→BA86, [3..6]=texture/KOR set indices per wall
  slot, [7]-1=start x (→B9F3), [8]-1=start y (→B9F1). The start facing B9F0 is derived from the
  position relative to the borders.
* **Cells**: 15 bytes each, `cell_off = (y*width + x)*15`. Cell 0 overlaps the header bytes above.
  * [2*side] = wall type for side 0..3, looked up in 16E0:9F0E; 9 = door.
  * [2*side+1] = wall parameter.
  * [11..14] = event key bytes. `fr_run_event` prints each byte as 2 decimal digits into an
    8-character key. "00000000" means no event. "31313127..31" are hard-coded shops/NPCs: 27 havalı
    mehtap, 28 yimmekCi (food), 29 silahCI (weapons), 30 gerekli şeyler, 31 another. Otherwise the
    event number is `((b11-b13)<<5) - sgn(b11-b13) + b12 - b14`, using two words read from the map
    handle at 0x27/0x28 (`[bp-0x34]`, `[bp-0x36]`); verify this formula against the original.

#### 2.3 Party (save-critical)

* **Stat record, 92 bytes (0x5C), array at 16E0:99A6, index 0..4** (1..4 used; index 0 is saved
  too). 46 words; fields used by the code (offset: refs): 00:83, 02, 04, 06, 08, 0A, 0C, 0E, 10,
  12:27, 14, 16, 18, 1A, 1C, 1E, 20, 22:21, 24, 26, 28, 2A:24, 2C, 36, 38:17, 3A:50, 3C:33, 3E, 40,
  42:63, 44:11, 5A.
  * +02: race/class index. The name is 11-byte strings at 16E0:2638 (1 = "insan", 2 = "öğrenci").
  * +0A/+0C: base values, copied to +38/+3A when GRP.DAT is written.
  * +3A: current health: >0 ok, -5..0 unconscious, ≤-5 dead. The FRPOS doctor sets it to 1.
  * +22: hunger counter (zeroed by eating).
  * +24: random(25)+random(25)+10*chargen[+0]+150.
  * +26: random(5)+random(5)+[+24]+10*chargen[+2]-110-10*([+0A]-1).
  * +42: portrait index; the icon name is 13-byte strings at 16E0:269B, loaded as icon 0x47+i.
  * chargen work table: 36 bytes per character at 16E0:9B74.
* **Name record, 148 bytes (0x94), array at 16E0:9C26, index 0..4**:
  * +00 char[37] name;
  * +25..+49 unused/zero in the samples;
  * +4A race name (copied from the 16E0:2638 table on load);
  * +6F char[37] known-aphorism flags, '1'/'0' ×36 + NUL, initialised to "1000…0".
* Option bytes 16E0:BB34..BB3A, money/food words 16E0:BB6D/BB6F/BB71/**BB73 (money)**.
  Position B9F3 (x), B9F1 (y), B9F0 (facing 0..3), district BB6B, state bits 16E0:3D5E.

#### 2.4 GRP.DAT / GRPHAZIR.DAT (664 bytes), cg_save_party / cg_load_party

For i = 1..4: `name_rec[i][0..36]` (37 bytes), then `name_rec[i][0x6F..0x93]` (37 bytes); 296 bytes in
total. Then for i = 1..4: `stat_rec[i]` (92 bytes); 368 bytes. Reads/writes use the MEMMAN/FILEMAN
file calls `F_569A_0342` (open, mode 2=read 4=write), `F_569A_053C`/`F_569A_06F7`
(read/write(long size, far buf, fd)) and `F_569A_04CA` (close).

#### 2.5 Save file `saklaNNN.sav` (1524 bytes), s3d_write_save 303F:3DD0 and s3d_read_save 303F:3F95

| file off | size | source | notes |
|---|---|---|---|
| 0 | 60 | caller buffer | description (`"c1\0…"`; **bytes after the NUL are uninitialised stack data** – keep them for byte-exact saves) |
| 60 | 2 | local = 0x0020 | version/size word |
| 62 | 32 | 16E0:B9CA | 256 event flags (bit n = byte n/8, bit n%8) |
| 94 | 460 | 16E0:99A6 | 5 × 92 stat records |
| 554 | 740 | 16E0:9C26 | 5 × 148 name records |
| 1294 | 170 | 16E0:98FC | unknown table (sample: words 0x32,0x44,0x36,0x35 at 34-byte stride; probably per-character data, 5×34) |
| 1464 | 7 | 16E0:BB34 | options (sample 01 05 01 01 01 00 01) |
| 1471 | 2 | 16E0:3D5E | state bits |
| 1473 | 2 | B9F3 | x |
| 1475 | 2 | B9F1 | y |
| 1477 | 1 | B9F0 | facing |
| 1478 | 2 | BB6B | district |
| 1480 | 2 | BB4D | (set to 1 at new game; FRPOS op 25 writes it) |
| 1482 | 8 | BB6D | 4 words; BB73 = money |
| 1490 | 2+2+2 | 3D64, 3D66, 3D68 | calendar/clock (sample 25, 10, 349) |
| 1496 | 11 | BBB7 | |
| 1507 | 11 | BBAC | |
| 1518 | 2 | BB8A | |
| 1520 | 2 | BB88 | (BB88/BB8A = midpoints of a rectangle in table 16E0:4A86, 8 bytes per district) |
| 1522 | 2 | BBAB | **a 2-byte write of a byte variable**: it also stores BBAC[0] again. BBAB = random(100)+101 on district load |

Matches orig/SAKLA001.SAV exactly (1524 bytes).

### 3. FRPOS scripts (PCSn) and the interpreter (fr_run_event 3FD1:0000)

* PC = word 16E0:BB98, the offset in the PCS bank including its 20-byte header. `fr_fetch_byte`
  (4E63) reads through EMS. Strings are `[len u8][len bytes]`, decoded by **+10 per byte**
  (0x16→' ', 0xAC→'\n').
* Layout from PC=0x14:
  * BE16 event count;
  * then events, each `[BE16 len][body]`. Event e is found by skipping e bodies.
  * The body starts with BE16 command count (→BB93), then BE16 entry offsets for trigger types 2,3,4.
    Type 1 starts after these 6 bytes; type t≥2 adds `offset - 2t`.
  * Each section starts with a byte = 12+type (checked: "..baslik:%d Tarz:%d..") and a byte →BB92.
* Trigger types: 1 = entering the cell, 3 = leaving it (s3d_try_move), 2 = shops/hard-coded
  (search/use).
* The loop executes commands while `BB93-- != 0` and running. Debug traces are printed when byte
  16E0:5C0B > 0 ("Komut no:%d", "Atla:%d").
* Operand sizes (from `fr_skip_commands` 4ECB, table @3FD1:505B) and semantics (switch @3FD1:0911,
  table @3FD1:208C):

| op | operands | meaning |
|---|---|---|
| 1 | t,a,n | IF: t=1 → if flag a clear skip n cmds; t=2 → if ACC(BB9E)≠a skip n. Other t: fatal |
| 2 | str | show text; if next op ≠5 wait lines*100+140 ticks |
| 3 | b | show portrait ikon b; mark view dirty (B9C8) |
| 4 | – | clear message, END |
| 5 | l1,l2,s1,s2 | yes/no question → ACC (0 = first) |
| 6 | n | skip n commands (GOTO forward) |
| 7 | f | set event flag f |
| 8 | 7 bytes | BATTLE: fills enemy group 16E0:5BDA.. (rand for placement) → `F_4595_B40A`, reload district; party wiped → game over |
| 9 | 9 bytes | item exchange/shop/treasure (BBA2/4/6 bytes, BB57/59/5B words) → `fr_item_exchange` |
| 10 | str,str | show one of two texts at random |
| 11 | 4 bytes | TELEPORT: district, x, y, facing; loads district, music, redraw |
| 12 | 3 bytes | change stat word +00 of a party member (add/sub/set) and redraw panel |
| 13,14,15 | b | no-op (operand ignored) |
| 16 | – | set BBA8=1 (cancel move), END |
| 17 | b,str | character-specific text ("kom1:%d, adamno:%d") |
| 18 | – | ACC = !(3D5E & 1) |
| 19 | b | play GFL video (CD prompt), redraw |
| 20 | n | ACC = random(n) |
| 21 | 6 str | 6-choice menu (`fr_menu`) → ACC |
| 22 | 3 bytes | find character (`fr_find_character`) → BB9C |
| 23 | 2 bytes | find character variant → BB9C ("adambul(0,%d,%d)") |
| 24 | – | ask the player to choose a character → BB9C ("bir adam seCiniz…") |
| 25 | b | set BB4D/music/state 3D5E, palette changes |
| 26 | b | pay money: ACC = 0 ok / 1 not enough (BB73) |
| 27 | 40 str | story slideshow `demo_story_scene` |
| 28 | – | teach aphorism (`fr_teach_aphorism`) |
| 29 | 3 bytes | modify stat +44/+3A (heal/damage) of a member |
| 30 | n,str(n+1) | text file / multi-page display (5784 font) |
| 31 | n | wait n |
| 32 | – | redraw, END |
| 33 | – | leave-district dialog, END |

Example (PCS1 event 0, type 1): `01 01 01 01` (if flag 1 clear skip 1), `04` (end), `02 "dISarIdan
garip hIrIltIlar…"`, `05 04 05 evet hayIr`, `01 02 01 01` (if ACC≠1 skip 1), `10` (cancel move, end),
`21` (leave district).

### 4. RNG, floating point, timing

* **RNG**: every call is Borland `rand()` (0000:1779, seed 16E0:94C4/94C6) used through the
  `random(n)` macro `(int)((long)rand()*n/0x8000)` (LXMUL 0000:1242 + LDIV 0000:12DF). Call sites
  (count):
  * 2700:9078 (2)
  * 303F: 0715 (1), 1927 (1), 2C69 (3), 3F95 (1), 4353 (3), 4A27 (1), 765C (2), 8524 (15)
  * 39E5: 0000, 035D, 0437 (26), 1AA2
  * 3C2D: 054D (3), 1108 (3), 1F32 (3)
  * 3FD1: 0000 (6, ops 8/10/20 and the food price), 2104, 2B1A (2), 2F21 (10)

  For two-term sums like `random(25)+random(25)`, the left call runs first.
* **FP**: almost all of it is in ovl2700:
  * precalc 0328 (369 x87 ops), 0DA8/2413 (213), 15D7/2C42 (216), with helpers 02B0/0D1D (ftol
    0000:1099 with the truncating control word);
  * the .KOR loader 1E06 (32);
  * rotations 8197/8896 (30/32);
  * shading 9078 (14);
  * step animations (4–6 each).

  303F uses only 9 ops (0000, 07B6). main runs the precalcs 0328, 15D7 and 2C42 at start-up; the
  tables are good candidates for extraction from a DOSBox memory dump rather than recomputing.
* **Timing**: waits go through `F_05DC_2DCA(ticks)`. Text waits are lines*100+140. The main loop
  polls `kbhit` and the time comparisons `cmp ax,0x3E8`/`0x5DC` (1000/1500 ticks) for clock/hunger.
  q3d animations sync via 2700:8FD4 (09D4:0E4A).

### 5. RTL identified (seg0000), for other groups

1477 PADD (huge ptr add), 12DF LDIV family (cx=0..3), 1242 LXMUL, 138D LXLSH, 127A SCOPY, 1502 stack
SCOPY, 1099 ftol, 1779 rand, 3F58 itoa, 48B4 sprintf, 4253 printf, 49CC/53A3 strlen, 49A3/537A strcpy,
48F7/530B strcat, 4973/534A strcmp, 52A4/4001 memcpy, 4936 strchr, 49EB/53C2 strncpy, 25D6 farmalloc,
24C2 farfree, 29D7 getch, 2E45 kbhit, 1F3F bioskey, 112F/1162 findfirst/findnext. Engine:
559A:0000 fatal error(file,line,msg), 0DB7:022F checked far free(ptr,line,file),
09D4:0A81/0B22 EMS map/unmap of a handle, 05DC:2948 message, 05DC:2198 yes/no, 05DC:1EB9 clear message.

### 6. Open questions

* Exact semantics of most stat-record fields (labels are drawn as graphics, not strings).
* The 170-byte block at 16E0:98FC; BBAC/BBB7 (11 bytes each); BB88/BB8A.
* q3d layer roles and the four animation functions are only guessed; 0DA8/2413/624A have no direct
  callers (maybe function pointers).
* Byte [28] of the district table; map header [2].
* FRPOS ops 12, 22, 23, 25, 29 and 30 need a detailed operand decode.
