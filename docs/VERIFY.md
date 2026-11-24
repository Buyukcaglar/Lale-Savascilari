# Tüm programın doğrulanması: port ve orijinal (DOSBox-X) / Whole-program verification: port vs. original (DOSBox-X)

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Tek bir girdi betiği (input script) iki programı da yönetir; ikisi de kontrol noktası
(checkpoint) dökümlerini (dump) aynı biçimde yazar; Python araçları bunları karşılaştırır.

### Girdi betikleri

Metin dosyası, satır başına bir olay (`#` yorum satırları), zamanlar **oyunun kendi
saatine** göre ms cinsinden: DGROUP:E74A (NKONTROL, 100 Hz'lik her tikte (tick) +10):

```
seed 1234                    açılışta srand() tohumu (time() yerine)
50000 key 1C0D               BIOS tuş sözcüğü scan<<8 | ASCII
52000 mouse 320 100 1        sürücü koordinatları (320 piksellik kiplerde x 0..639), düğmeler
52300 buttons 0
60000 dump c04_district      kontrol noktası dökümü
dumpevery 250 50000 250 i    her 250 ms'de bir i_<ms> dökümü
70000 quit
```

**Teslim kuralı (iki tarafta da):** oyunun her *girdi okumasında* - `kbhit`
(INT 21h 0Bh), `getch` (INT 21h 01h/07h/08h ve bloklayan okuma beklerken her
uyanışta), `bioskey` 0/1/10h/11h (INT 16h, aynı şekilde) ve fare yoklaması (INT 33h
AX=3) - zamanı saate eşit ya da ondan küçük (<=) olan bütün olaylar dosyadaki sırayla
uygulanır.

Neden: ana sistem (host) zamanı ve kare sayıları iki programda farklıdır (DOSBox-X
kod çözmeye/yüklemeye öykünülmüş zaman (emulated time) harcar, portun sanal zamanı
harcamaz), ama oyunun saati ve girdi okumaları iki tarafta da oyunun görebildiği
şeylerdir.  Oyun girdi beklerken iki taraf da olayı aynı tikten sonraki ilk okumada
görür.  DOSBox-X tarafında her girdi okumasına kesme noktası (breakpoint) koymak bir
seçenek değildir: her hata ayıklayıcı komutu 50 ms tutar (öykünücü bağlantıyı 20 Hz'de
yoklar).

**Okuma sayısıyla eşzamanlama (read-count sync)** (zaman yerine `+n`): *sayılan okuma*,
yukarıdaki işlevlerden birine giriştir (bloklayan bir okumanın uyanışları sayılmaz;
DOSBox-X tarafında DOS'un, oyunun INT 21h çağrısı içinde yaptığı INT 16h çağrıları da
sayılmaz).  `+n <event>`, önceki olayı uygulayan okumadan n sayılan okuma sonra - ya da
bloklayan bir okumanın uyanışında hemen - vadesi gelen olaydır.  Böylece her turda bir
kez yoklayan döngüler, iki olay arasında iki tarafta da aynı sayıda tur atar; chargen
(karakter yaratma) her turda bir kez `random(1000)` çağırır, bu yüzden zarları ancak bu
yolla tutar.  Çapalar (anchor), saatle sınırlı bir döngünün (videolar, girdi yoklayan
solmalar, zamanlı metinler) önce geldiği yerde sayımı yeniden hizalar:

```
150000 sync D5DE 0019        hiçbir şey; yalnızca DGROUP sözcüğü D5DE == 19h iken de vadesi gelir
150000 syncne D5DE 0000      ... D5DE != 0 iken (savaş: ilk aktörün sırası geldi)
+30000 key 4800              çapadan 30000 sayılan okuma sonra
+0 srand 00001000            RTL rastgele durumu DGROUP:94C4 := 1000h (rand()'ı yeniden hizalar)
```

**Saate göreli zamanlama (clock-relative timing)** (`@n`): saat, önceki olayı uygulayan
okumadaki değerinin en az n ms ötesine geçtiğinde vadesi gelir (hook.S tür biti 40h,
sdl3_main.c'de `sc_rel == 2`).  Oyunun sıkı bir yoklama döngüsünde saati beklediği her
yerde gerekir: `ui_wait_click_or_key` (n x 20 ms), mesaj kutusu (kip 0:
(lines*6+20) x 150 ms), büyü ve çarpma animasyonları, palet ve müzik solmaları
(fade).  Orada okumalar CPU hızında koşar - portta ms başına yaklaşık 14-20 okuma,
DOSBox-X'te 37-43 - bu yüzden bir okuma sayısı iki tarafta farklı bir zamana denk gelir
ve böyle bir beklemenin içinde gelen tuşlar atılır.  `mkscript`'in `Script.after(n)`
çağrısı `@` kipine, `reads(n)` yeniden `+` kipine geçer; kip kalıcıdır, bu yüzden
ardından bir okuma aralığı gelen bir saat aralığı, arada bir olay gerektirir (bir
`srand`).

Saate bağlı durumu etkisizleştiren kontrol noktası olayları (iki tarafta da):

```
+0 srand 00001000            RTL rastgele durumu DGROUP:94C4 := 1000h
+0 poke 48E8 03 00 02 00     baytları (hex) DGROUP:48E8.. adresine yazar (bayt/sözcük
                             olaylarına bölünür, hook.S tür 8)
```

Hiçbir BIOS tuş sözcüğünün ifade edemediği şeyler (Ctrl-Break, Ctrl+Alt+Del) için ham
klavye baytları:

```
+0 kbd 1D E0 46 E0 C6 9D     tarama kodu (scan code) baytları, sırayla (bayt başına
                             bir hook.S tür-9 olayı)
```

DOSBox-X tarafı: her bayt 8042'den geçer - 64h portuna D2h komutu ("write keyboard
output buffer", klavye çıkış tamponuna yaz), bayt 60h'ye - ve `hook.S` çıkış tamponu
yeniden boşalana dek kesmeleri açar; böylece IRQ 1 baytı INT 09h zincirine (önce oyunun
`ctl_int09_isr`'ı, sonra BIOS) teslim eder.  Port tarafı: `sc_kbd` baytı önce
`pc_int09`'a (oyunun kancası) sunar, sonra BIOS'u modeller: E0 öneki, kaydırma
bayraklarında (shift flags) Ctrl (1D/9D) ve Alt (38/B8), ve Ctrl + 46 basışını (make)
Ctrl-Break olarak (`pc_ctrl_break`: tuş tamponu boşaltılır, 0000h sözcüğü saklanır,
INT 1Bh).  Diğer baytlar "not modelled" diye günlüğe yazılır.

**`int23` dökümü:** INT 23h çağrıldığında iki taraf da `int23` adlı bir kontrol noktası
döker (DOSBox-X: `hook.S`, IVT 23h'yi döküm yapan bir sarmalayıcıyı gösterir halde
tutar; sarmalayıcı sonra orada önceden bulunan işleyiciye atlar; port:
`pc_set_int23_observer` gözlemcisi, işleyiciden önce `dos_break_check` içinden
çağrılır).  Oyunun işleyicisi DOS'a çıkar, bu yüzden yeniden oynatma o dökümden sonra
durur.

#### Saate bağlı olan ne ve play3 bunu nasıl etkisizleştirir

| Durum | Neden farklı | play3 |
|---|---|---|
| chargen yürüme karesi / göz kırpma 48E8/48EA, göz kırpmanın `random(3)`'ü (150 ms tik) | tikler farklı turlara düşer | her kontrol noktasında `poke 48E8 = 3, 48EA = 2` (yeni göz kırpma yok; sonraki tik kare 0'ı ve normal yüzü çizer), `sync 48E8 0000` (o tik), `srand`, döküm; Space'te `srand` + `poke 48E8 = 0 0 0 0` - zar ve sonraki üyenin ekranı bundan sonra aynıdır |
| 3DSYS portre göz kırpması: saniyede bir `random(20)`; zamanlayıcı ve durum, evresi yükleme sürelerine bağlı yerel değişkenlerdir (DOSBox-X yüklerken öykünülmüş saat harcar, portun sanal zamanı harcamaz) | göz kırpmalar farklı çağrılarda başlar; döngüden çıkılınca yarıda kesilen bir göz kırpma, yüzünü ekranda bırakır | hikâyeden itibaren her 2 s'de bir ve her keşif tuşunda / dökümde *sessiz tohumla* (quiet seed) `srand` (`mkscript.quiet_seed`: sonraki 8 `rand()` değerinin hepsi >= 1639, yani hiçbir `random(20)` 0 olmaz) |
| savaşta konuşan yüz: bir ses çalarken her 150 ms'de `random(2)` (BBC4) | yukarıdaki gibi | tuşlar önceki kontrol noktasından 30000 okuma sonra (ses iki tarafta da bitmiş), her tuşta `srand`, dökümler bir tuştan 10000, TAB'dan 100000 okuma sonra (düşman turunun saate bağlı animasyonları; port ms başına DOSBox-X'ten ~3 kat az okuma yapar) |
| AMP/DSMI zamanlayıcı durumu, DSMI zamanı, GFL kare/başlangıç zamanları, s3d solma zamanı, ses bitiş zamanı 73D1, GFL akışını izleyen EMS eşleme yazmaçları | müzik/videolar yükleme süresine bağlı saat değerlerinde başladı | etkisizleştirilmez: `vcompare.py` bunları yalnızca `--clock` ile, yalnızca adlarıyla listeler (`CLOCK_FIELDS`) |

`vcompare.py` ayrıca *ana sistem alanlarını* (host fields) yok sayar (`HOST_FIELDS`:
portun yerine koyduğu Borland RTL verileri - _8087, atexit sayacı, çıkış kancaları,
conio _video, delay kalibrasyonu/brk/environ, overlay yöneticisi tablosu, harderr
vektörleri - ve cd5737.c'nin yerel bir aynada tuttuğu CD istek bloğu; hiçbirini oyun
kodu okumaz) ve portta segmenti 0 olan uzak işaretçileri (far pointer)
(`farptr_ranges`, `farptr_tail_filter`: yalnızca ofseti tutan kayıtlar).

#### Sonuç (2026-11-21, `play3`)

`python tools/verify/mkscript.py play3 out/verify/scripts/play3.txt;
python -u tools/verify/pair.py out/verify/scripts/play3.txt play3`
(pair.py oyun klasörlerini sıfırlar, iki tarafı da çalıştırır, dökümleri ve oyunun
yazdığı dosyaları karşılaştırır):

* menü, chargen başlangıcı + 4 üye: aynı (ekran, DAC, yazmaçlar, DGROUP); GRP.DAT
  aynı.  `c03_after_chargen` hikâye videosunun (seq1.GFL, 10 fps, kareler saate göre
  gelir) içinde sabit bir okuma sayısında dökülüyordu: port kare 3'ü, orijinal kare
  1'i gösterdi.  Artık videonun kendisine çapalanıyor - `sync D618 0005` (D618 = son
  işlenen kare) - ve aynı (play3'ün önekini paylaşan 2026-11-21 tarihli play4
  koşuları).
* bölge 0: varış, 2 tur, kayıt menüsü, 2. yuvaya kayıt, kapı sorusu, hazine ekranı:
  aynı; SAKLA001.SAV aynı.
* köpek savaşı: başlangıç (ilk aktörün sırası geldi) ve 12 savaş tuşu (UP, UP, ENTER,
  TAB x3, iki tarafın turları): 13 kontrol noktasının hepsinde aynı.
* Yani play3'ün `c03_after_chargen` dışındaki her kontrol noktası yazmaçlarda, DAC'ta,
  ekranda, video belleğinde ve DGROUP'ta (ana sistem alanları hariç; saat alanları
  `--clock` ile listelenir) aynı, ve yazılan iki dosya da eşleşiyor.  (Fare mandalından
  sonraki 2026-11-21 koşusu; önceki koşular yalnızca 3DSYS imleç şeklinde E5E1/E5E3 =
  ana sistem işaretçisi, ve 100000 okumalık aralıktan önce düşman turunun ortasında
  alınan savaş dökümlerinde farklıydı.)
* Dükkân, eşya takası, büyüler, yükleme: aşağıda play4'e bakın.
* Bu değişikliklerden sonra karşılaştırmalı test takımı (differential suite): 219/219
  (out/base).

Yolda bulunan port düzeltmeleri: DSMI sürücü yüklemesi SDISB16.DSD'yi oyunun dosya
katmanı üzerinden açar (512 baytlık stdio tamponu, 6E06/DE30'u farklı kılan
paragraftır, `src/dsmi/dds.c`); kayıt açıklaması tamponu, tamamı (60 bayt) yazılan
ilklendirilmemiş bir yığın (stack) tamponudur - sıfırlanmış bir static olarak
modellendi (`src/game/s3d_menu.c`).

Fare: hook.S, AX=3'te yalnızca oyunun (AX=0/4/7/8) ya da betiğin ayarladığı konumu,
sürücünün sınırladığı (clamp) haliyle bildirir - `autolock = false` ile DOSBox-X'in
fare entegrasyonu aksi halde ana sistem işaretçisi penceresini her geçtiğinde DOS
faresini hareket ettirir (bir koşuda farklı bir 3DSYS imleç şekli E5E3 olarak
görüldü).

DOSBox-X yapılandırması: SB16, SES.CFG'nin dediği gibi IRQ 7'de olmalıdır
(tools/dosbox/ist.conf).  IRQ 5 ile DSMI sürücüsü IRQ 7'nin maskesini kaldırır, kesme
almaz, DMA takılır, her sesin konumu 0'da kalır ve ui_wait_voice (05DC:1D1B: konum +
20h >= örnek sonu, ya da Esc) hiç bitmez ve betiğin tuşlarını yutar; donanımda (ve
portta) 3676 Hz'de 300h baytlık bir ses ~0.21 s sonra biter.  Bu değişiklikten önce
yapılan dökümler sessiz koşmuştur ve karşılaştırılamaz.

#### play4: büyülü savaş, oyun sonu + yükleme, eşya takası, dükkânlar, kamp, kayıt

`python tools/verify/mkscript.py play4 out/verify/scripts/play4.txt;
python -u tools/verify/pair.py out/verify/scripts/play4.txt play4`
(yaklaşık 45 dk; ardından `pair.py --keep` ile `play4load`, aşağıya bakın).

Akış (köpek savaşına kadar play3'ün öneki, sonra):

* **Büyülü ve düşman yapay zekâlı savaş, yenilgiye kadar** (`c12_b00..40`, portta
  planlanmış 42 eylem grubu, `out/verify/h/plan2.py`): Rifki (birim 27) köpek bitişik
  gelince ona bir kez vecize 1 "pis" yapar (`v`, Enter, `o` sonraki düşman, Enter),
  sonra ona çarpar (tuş takımıyla düşmanın üstüne hareket); diğerleri pas geçer (Tab).
  Grubun (party) silahı yoktur, bu yüzden Enter saldırıları yok sayılır (saldırı silah
  gerektirir) ve vecize 1 hasar vermez; köpeğin yapay zekâsı grubu ısırarak öldürür:
  ISOSYS sonucu 632E = 2.
* **Oyun sonu ve yükleme**: "eski bir oyuna devam edecek misiniz?" -> `e` -> yükleme
  menüsü -> Enter, SAKLA001'i yükler (`c13_*`).  Yüklemeye yalnızca ana menüden ve bu
  oyun sonu döngüsünden ulaşılabilir.
* **İkinci savaş, kazanılan**: yine kapı/hazine olayı, sonra her üyenin vecize 1 yaptığı
  (menzil dışı: "desem de duyamaz ki!") ve saldırmayı denediği dört grup; köpek kendi
  turunda kaçar, sonuç 1 (`c14_*`).
* **Eşya takası**: bölge 0'ın (8,1) konumundaki adamla (`c15_*`, `i`, Enter, Enter),
  anahtar olayı (bayrak 2), bölge 0'dan ayrılış (bayrak 81) ve bölge 1'e varış
  (`c16..c19`).
* **Dükkânlar** (bölge 1: eşya (3,7)'de, silah (11,10)'da, yiyecek (13,16)'da): eşya ve
  silah dükkânında her adım dökülür (`c21_goods_*`, `c23_weapons_*`): `b` para
  birimlerini çevirir, tuşlarla alış (`i` dükkân satırı, Enter, Right Right, Enter),
  tuşlarla satış (`i`, Enter, `s`), fareyle alış (dükkân yuvası 3, sonra envanter
  yuvası 4), fareyle satış (envanter yuvası 4, sonra satış alanı), Esc.  Para: 11 ->
  216 (çevirme) -> 78 (tuşla alış) -> 87 (tuşla satış) -> 11 (fareyle alış) -> 50
  (fareyle satış).  Yemek tezgâhı: "evet", 12 ödendi, açlık sıfırlandı (`c24/c25`).
* **Kamp** (`k`, dinlenme, `c26/c27`) ve yeni bir yuvaya **kayıt** (SAKLA002, açıklama
  "cd", `c28/c29`).
* **Doktor**: play4'te yok - tek doktor olayı (anahtar 31313127) bölge 4'te (PC4,
  (8,11)), kısa bir betiğin ulaşamayacağı yerde; aşağıdaki play5 oraya hazırlanmış bir
  kayıttan gider.

Sonuç (2026-11-21): **play4'ün 112 kontrol noktasının hepsi aynı** - yazmaçlar, DAC,
ekran, video belleği ve DGROUP (ana sistem alanları hariç; saat alanları `--clock` ile
listelenir), iki programda da ses açık (SB16 IRQ 7); GRP.DAT ve SAKLA001.SAV aynı.
SAKLA002.SAV 48 baytta, 0x0B-0x3A arasında farklı: 60 baytlık açıklamanın NUL'dan
sonraki kuyruğu; `s3d_save_load_menu` bunu ilklendirilmemiş bir yığın tamponundan
([bp-3CAh]) yazar.  Orijinalde bu kuyruk, oyun sonu döngüsünden farklı bir yığın
derinliğinde çalışan yükleme menüsünün bıraktığı bayat bir DOS find_t kaydıdır
("SAKLA001.SAV", öznitelikler, saat, boyut); portun static tamponu ise önceki
kullanımının metnini tutar.  Bunu yeniden üretmek, orijinal yığının geçmişinin bir
modelini gerektirirdi; o baytları hiçbir oyun kodu okumaz.
`play4load` (`pair.py ... play4load --keep`: ana menü, yeni bir oturumda SAKLA002'yi
yükleme, bir tur) - 4 kontrol noktası, aynı açıklama kuyruğunu tutan yükleme menüsü
liste girdisi A6AB-A6DB dışında aynı; yüklenen oyun (bölge 1, (12,16), 107 para) aynı.
play4 hiçbir port hatası bulmadı: yoldaki her ayrışma betik zamanlamasıydı (betikte
düzeltildi, aşağıya bakın).  Bu değişikliklerden sonra karşılaştırmalı test takımı:
219/219 (out/base).

Zamanlama dersleri (hepsi `mkscript.py`'de):

* Saate bağlı beklemeler (tabela metinleri, dükkân selamlamaları, büyü ve çarpma
  animasyonları, solmalar) `@n` kullanır; 3B yürüyüşlerdeki tuşlar kendi okumalarında
  sessiz bir tohumla yeniden tohumlanır (re-seed) (`rkey`, `rwalk`), uzun beklemeler
  her saniye yeniden tohumlanır (`quiet_wait`); diyaloglar, sonlarında ayarladıkları
  bayraklara çapalanır (`sync B9CA+n`).
* `quiet_wait`, sonraki 64 çekilişi sessiz olan tohumlarla yeniden tohumlar (3B görünüm,
  bir saniye arayla yapılan iki yeniden tohumlama arasında 8'den fazla çekiliş yapar; 8
  ile bir portre göz kırpması yalnızca bir tarafta başladı, c25).
* Bir dükkânın Esc'inden ya da oyun sonu sonucundan sonraki dökümler,
  `s3d_apply_sound_options`'ın 100 ms adımlı solmalarının bitmesini bekler (solmanın
  ortasında DAC, 3D6E, 3D70, 7318, 731C farklıydı).
* Dükkânlar stoklarını selamlamanın hemen ardından `random()` ile belirler: hareketin
  yeniden tohumlaması ile bu zar arasına hiçbir yeniden tohumlama düşmemelidir.
* Bir büyü ya da çarpmadan sonra 12 s'lik saat (`battle_groups`): okuma aralıklarıyla
  DOSBox-X sonraki Tab geldiğinde hâlâ büyü animasyonundaydı.
* İkinci savaşın çapası önce `poke D5DE 0000` gerektirir (D5DE ilk savaşın son
  aktörünü tutar); oyun sonu `sync 632E 0002`'ye çapalanır.
* `vcompare.CLOCK_FIELDS`'e BBC4 (ISOSYS konuşan yüz zamanlayıcısı) ve D534 (yapay zekâ
  kararının saat sözcüğü) eklendi: yalnızca saat damgaları.

#### play5: hazırlanmış bir kayıttan bölge 4 - doktor, falcı, otel, 3DSYS tuşları

```
python tools/verify/mksave.py play5 out/verify/scripts/play5.sav
python tools/verify/mkscript.py play5 out/verify/scripts/play5.txt
python -u tools/verify/pair.py out/verify/scripts/play5.txt play5 --install out/verify/scripts/play5.sav=SAKLA001.SAV
```
(port 25 s, DOSBox-X yaklaşık 10 dk.)

**Kayıt.**  `mksave.py` onu, orijinalin kendi yazdığı bir kayıttan türetir
(`tools/verify/saves/play4_SAKLA002.SAV`, play4'ün SAKLA002'si: bölge 1, grup
ali/ayse/rifki/zeki) ve yalnızca şunları değiştirir: açıklama ("doktor" + NUL'lar),
güneye bakan (11,12) konumu, bölge 3 (PC4), para 206, üye 2-4'ün sağlık sözcükleri
(1, -10, -2) ve `s3d_load_district(first)`'in gerçek bir varışta ayarladığı üç şey
(dükkân stokları BBB7/BBAC temizlenir, şehir haritası işareti BB8A/BB88 = 107/226).
Bayraklar, zaman, müzik parçası, seçenekler ve ücret baytı (106) kalır.
`pair.py --install FILE=NAME` sıfırlamadan sonra onu iki oyun klasörüne de kopyalar.
Hazırlanan kayıt aynı şekilde yüklenir: `d02_loaded` DGROUP'ta, ekranda ve DAC'ta aynı.

Akış (39 kontrol noktası `d00`..`d38`):

* Ana menüden **yükleme** (`d00_menu`, `d01_loadmenu`, `d02_loaded`).
* **Falcı** (10,13), olay 18 - play3/4'ün kullanmadığı işlemler (op): bir soru
  (`d03_gypsy_q`), `pay 5` (op 26: 206 -> 201), dört uzun metin (ilkinde
  `d04_gypsy_text`), müzik 7 ve yeniden 1 (op 25, solmalarla), `findminmax` + `say 5`
  (op 23, WHO ile 17), bayrak 100 (`d05_gypsy_done`).
* **Doktor** (8,11), (9,10)'un tabelası üzerinden (olay 16), dört ziyaret:
  1. ücret sorusu "visite : 106" (`d06_doctor_q`), `h`: reddedildi
     (`d07_refused`);
  2. `e`, "kim servis istiyor ?" (`d09_who`), sonra ölü olmayan üyeler: F1 (sağlık 4,
     `d10`), portre 2'ye bir tıklama (sağlık 1, `d11`), F4 (sağlık -2, `d12`) - her
     seferinde "bu adam OlU deGil. baSkasInI seCin" - ve Esc (`d13_cancelled`, para
     alınmadı);
  3. `e`, portre 3'e bir tıklama (sağlık -10): dört metin, sağlık 1, 201 -> 95
     (`d16_revived`);
  4. 95 < 106 iken `e`: "bu para yetmez!" (`d18_too_poor`).

  Olay yalnızca sağlık <= -5 ("ölü") ile geri kalanı birbirinden ayırır.
* **Otel**: (14,9)'un tabelası (olay 13), sabit anahtarı 31313131 olan oda (12,9)
  (3D5E bit 2: kamptayken karşılaşma yok, `d19_hotel_room`), içinde bir kamp
  (dinlenmenin içinde `d20_camp`, `d21_rested`: sağlık 4/4/7/3), otelci (13,8), olay 11
  (`d22_keeper`).
* Yeni bir yuvaya **kayıt**, SAKLA002 "ef" (`d23_savemenu`, `d24_saved`).
* Başka hiçbir oynanışın (play-through) kullanmadığı **3DSYS tuşları**: bir adım geri
  (`d25`), kapılara karşı `4`/`6` yan adımları (reddedildi, `d26`/`d27`), PgDn/PgUp
  görüş boyutu ve 200x200 görünümde bir dönüş (`d28`..`d30`), karakter ekranı F1 ve
  içinde F3 (`d31`, `d32`, geri `d33`), seçenekler listesi `t` (`d34`, Esc `d35`), `n`:
  şehir haritası har4.cmp (bayrak 82, `d36`), konum metni (`d37`) ve
  `s3d_enter_view` üzerinden geri (`d38`).

Sonuç (2026-11-22): **39 kontrol noktasının hepsi aynı** - yazmaçlar, DAC, ekran,
video belleği ve DGROUP (ana sistem alanları hariç; saat alanları `--clock` ile
listelenir; saatlerin kendisi aynı kontrol noktasında 1 s'ye kadar farklıdır:
DOSBox-X'in öykünülmüş yükleme ve çizim süresi).  GRP.DAT ve SAKLA001.SAV aynı.
SAKLA002.SAV 30 baytta, 0x0C-0x2F arasında farklı: yine açıklamanın NUL'dan sonraki
kuyruğu, orijinalin ilklendirilmemiş yığın tamponu (`s3d_save_load_menu`'nün
[bp-3CAh]'si) - burada ana menüdeki yüklemenin `s3d_read_save` çağrısının o yığın
derinliğinde bıraktıklarını tutar ("sinsan1.cmp", son portre adı, dönüş adresleri);
portun static tamponu orada sıfırdır.  Diğer 1494 bayt eşit.  Port hatası
bulunmadı.

Belgelerdeki iki ifade yanlıştı ve orijinalin davranışına göre düzeltildi: yüklenen bir
kayıt doktor ücretini korur (FORMATS.md yeniden belirlendiğini söylüyordu) ve doktor
sağlığı -5 ya da daha düşük olan üyeleri tedavi eder ("-5'in üstündekileri" değil).

Zamanlama dersleri:

* Doktordan "para yetmez" (too poor) dışındaki her çıkış `fr_redraw` ile biter; bu,
  grubu hâlâ batıya bakar halde (9,11)'e geri koyar: bir Up ile yeniden girilir.  "Para
  yetmez" durumu grubu doktorun hücresinde bırakır.
* Kamp içindeki kontrol noktası ilk önce `k`'den 2 s sonra alınmıştı: port 10:17'deydi,
  orijinal 10:11'de (saatin her 50 ms'si için bir takvim dakikası, *artı* DOSBox-X'te
  öykünülmüş zaman harcayan çizim).  Artık takvim dakikasına çapalanıyor
  (`sync 3D64 0014`): o dakikanın turunun ilk girdi okuması iki tarafta da aynı
  noktadır.
* Uzun diyaloglar sonlarında ayarladıkları şeye çapalanır: falcı için bayrak 100
  (`sync B9D6 0010`) - metinleri portta 53 s, DOSBox-X'te daha uzun sürer - ve doktor
  için diriltilen üyenin sağlık sözcüğü (`sync 9AF4 0001`); tek satırlık tabelalara
  14 s'lik saat verilir.
* `n`'nin iki beklemesi (2000 ve 2400 tur), saate göre gönderilen tuşlarla bitirilir
  (önceki olaydan 8 s ve 4 s sonra).

#### play6: bayrak 99'dan sonra bölge 4 - yan adımlar, seçenekler, sağ düğme, yemek tezgâhı

```
python tools/verify/mksave.py play6 out/verify/scripts/play6.sav
python tools/verify/mkscript.py play6 out/verify/scripts/play6.txt
python -u tools/verify/pair.py out/verify/scripts/play6.txt play6 --install out/verify/scripts/play6.sav=SAKLA001.SAV
```
(port 15 s, DOSBox-X yaklaşık 10 dk; `play6side`, `play6opt1`, `play6opt`,
`play6doctor`, `play6food` bir bölümden sonra durur.)

**Kayıt.**  play5'inki gibi (aynı temel, aynı `patch`), artı bölge 4'ün hikâyesinin,
dükkânları açıldığında geride bıraktıkları.  Yemek tezgâhının (16,9), silah dükkânının
(8,16) ve eşya dükkânının (3,17) kapıları olay 12'yi taşır; bu olayın tetiği 1,
**bayrak 99** temizken "kapalI" der ve hareketi iptal eder.  Bayrak 99 yalnızca tek bir
yerde ayarlanır (18 PCS dosyasının hepsi ve kod tarandı): PCS4'ün olay 10'u, (14,0)
hücresinin tetiği 2 - Yeni Camii'nin mahzeninden çıkan merdivenlerin sahanlığı.  Bayrak
31 ayarlıysa Seyh cehalet'in uçup gidişini gösterir ("Cok geC kalDIk..."), müzik 1'i
çalar ve 99'u ayarlar.  Oraya giden zincir, her bayrak adı verilen olay tarafından
ayarlanır: 29 (olay 1, (0,19): tabIk vesile grubu direnişe götürür; o olmadan cami
"onarım için kapalı"dır), 97 (olay 2, (8..10,5): caminin kapılarında kuşlarla savaş),
30 (olay 3, (9,1): mahzenleri anlatan kuş), 98 (olay 4, (7,0): gizli kapıdaki savaş),
31 (olay 5, (3,3): mahzende Seyh cehalet; olay 6 sonra (14,0)'a ışınlar), 99.
`mksave.py play6` bu altı bayrağı ve müzik parçası 1'i ayarlar (olay 10'un `music 1`'i
`setflag 99`'undan hemen önce gelir); ayrıca kuzeye bakan (10,7) konumu, para 126,
play5'teki gibi sağlık 4 / 1 / -10 / -2, ve açlık sözcükleri 40 / 36 / 30 / 44 (temel
kayıt bir yemeğin hemen ardından yazılmıştı: hepsi 0).  Yeniden üretilmeyenler: olay 1
ve 5'in isteğe bağlı ganimeti (üç silah, bir kese) ve isteğe bağlı savaşların bayrakları
(109, 110).

Akış (46 kontrol noktası `e00`..`e30`):

* **Yükleme** (`e00_menu`, `e01_loadmenu`, `e02_loaded`).
* Caminin önündeki açık sokakta **yan adımlar**: kuzeye bakarken `4` -> (9,7)
  (`e03_side_l`), `6` -> (10,7) (`e04_side_r`); doğuya bir dönüş (`e05_east`), `4` ->
  (10,6) (`e06_side_l_east`), `6` -> (10,7) (`e07_side_r_east`).  Dördü de başarılı.
* **Seçenekler listesi** (`t`, `e08_options`).  Yedi girdi tutar: yürüme / kapı
  animasyonu, savaş hızı (0..9), oyun müziği, savaş müziği, ses efektleri, zorluk,
  görüş boyutu.  Her biri üzerine sol tıklamayla bir kez değiştirilir (`e09_opt1_anim`
  .. `e09_opt7_view`: animasyon kapalı, hız 5 -> 6, oyun müziği kapalı, savaş müziği
  kapalı, efektler kapalı, zorluk 0 -> 1, görüş 200 -> 100) ve sonra işaretçi listenin
  dışına götürülmüşken tuşlarla geri alınır (`e10_opt7_view` .. `e10_opt1_anim`,
  Down/Up + Enter; hız 6 -> 7).  Büyük görünüme geçen girdi 7 animasyon baytını
  ayarlar, bu yüzden girdi 1'deki Enter onu yeniden kapattı ve bir Enter daha onu geri
  getirir (`e10_opt1_anim_on`).  Esc kapatır (`e11_view`); yeniden açıldı
  (`e12_options`) ve sağ düğmeyle kapatıldı (`e13_view`).  Son seçenek baytları
  1 7 1 1 1 0 1.
* **Doktor** (8,11), üç ziyaret: `e` ve "kim servis istiyor ?" sorusunda (`e15_who`)
  **sağ fare düğmesi**: iptal, para alınmadı (`e16_rclick_cancelled`); `e`, F2 (sağlık
  1: "bu adam OlU deGil", `e18_not_dead_f2`) ve o ikinci istemde sağ düğme
  (`e19_rclick_cancelled2`); `e`, F3 (sağlık -10): diriltildi, 126 -> 20
  (`e21_revived`).
* **Yemek tezgâhı**: (9,10) ve (16,8)'in tabelaları (olay 15), kapı hücresi (16,9) -
  olay 12 grubu içeri alır (`e22_stall_door`) - ve tezgâh (15,9), anahtar 31313128, üç
  ziyaret: fiyat sorusu "adam baSI 3" (`e23_food_q`), `h`: reddedildi
  (`e24_food_refused`); fiyat 4, `e`: yemek resmi (`e26_eating1`), 3 yaşayan üye x 4 =
  12 ödendi, 20 -> 8, dört açlık sözcüğü 55/51/40/44 -> 0 (`e26_fed1`); yine fiyat 4,
  8 < 12 iken `e`: "para yetmiyor" (`e28_food_too_poor`), hiçbir şey değişmedi
  (`e28_food_left`).  Bunlar `shop_food`'un üç dalının hepsidir.
* Yeni bir yuvaya **kayıt**, SAKLA002 "gh" (`e29_savemenu`, `e30_saved`).

Sonuç (2026-11-23): **46 kontrol noktasının hepsi aynı** - yazmaçlar, DAC, ekran,
video belleği ve DGROUP (ana sistem alanları hariç; saat alanları `--clock` ile
listelenir).  GRP.DAT ve SAKLA001.SAV aynı.  SAKLA002.SAV 30 baytta, 0x0C-0x2F
arasında farklı: NUL'dan sonraki açıklama kuyruğu (orijinalin ilklendirilmemiş yığın
tamponu, play5'tekiyle aynı baytlar: "sinsan1.cmp" ve dönüş adresleri; portta
sıfırlar).  Diğer 1494 bayt eşit; değiştirilen seçenek baytları ve bayraklar da
bunların arasında.  Port hatası bulunmadı; hiçbir koşu yalnızca fare imlecinden oluşan
bir fark göstermedi.

`s3d_apply_sound_options` ve seçenekler listesi: listenin kendi değişiklikleri
bloklayan `gr_fade` çağrıları kullanır (oyun müziği: 300 ms kısılma, modül durdurulur
ya da başlatılır, 300 ms açılma; görüş boyutunu 100'e geri almak: 100 ms aşağı ve
yukarı); bunlar 3D6E / 3D70 / 3D72 sayaçlarını 0'da bırakır, bu yüzden boşta geri
çağırma işlevinin (idle callback) hiçbir girdiden sonra adımlatacak bir şeyi yoktur -
seçimden 3 s sonra (girdi 3 ve 7 için 8 s) alınan her seçenek kontrol noktası, bir
solmayı beklemeden aynıdır.  Geri çağırma işlevi play6'da daha sonra bir şeyi
adımlatır: çatılı hücreye (16,9) girmek `s3d_apply_daylight`'ın parlaklık ve tempo
sayaçlarını ayarlamasına yol açar; bunlar sonra saatin her 100 ms'sinde bir adım
ilerler.

Zamanlama dersleri (ilk koşudaki iki farkın ikisi de betik zamanlamasıydı):

* `e22_stall_door` ilk önce (16,9)'a adımdan 2.5 s sonra dökülmüştü: solmanın ortası
  (port 3D6E/3D70 = 11/-1, orijinal 13/-3, 106 DAC girdisi, 7318, 731C).  Artık 8 s
  sonra, solmadan sonra.
* `e26_eating1` ilk önce `e`'den 1.2 s sonra dökülmüştü.  Yemek resminin iki beklemesi
  vardır: `fr_show_icon` içinde 50 tur ve ondan sonra 120 tur; portta 1 s + 2.4 s,
  DOSBox-X'te bunun yaklaşık üç katı, yani port ikinci beklemedeydi, orijinal
  birincide (aynı ekran; EMS kullanımı 6E0A, 8702, DC00 ve yüzey 42h'nin tanıtıcısı
  farklıydı).  Artık çapalı: `fr_show_icon` yüklemeden önce yüzey 42h'yi serbest
  bırakır ve beklemesinden sonra yeniden ayırır; `sync E4AF 0000` o beklemenin ilk
  okumasıdır.
* Bir adımın rastgele karşılaşma zarı (`random(300) + 1 <=` haritanın oranı, PC4'te 2)
  tuşun yeniden tohumlamasından 8'den fazla çekiliş sonra gelir: 8 çekilişlik sessiz
  tohumlarla tezgâha yürüyüşün bir adımı savaş başlattı.  play6'nın hareketleri 64
  sessiz çekiliş kullanır (`rkey(..., n=64)`).
* Seçenekler listesinin etkin alanları (hotspot) örtüşür ((24, 133 + 7i)-(162, 149 +
  7i)) ve ilk eşleşme kazanır; listenin imleci için sıcak nokta (hot-point) kayması
  yoktur: girdi i, y = 146 + 7i'de tıklanır.  Tuşlar için işaretçi listenin dışında
  olmalıdır (altındaki bir girdi seçimi geri alır), ve liste min(son seçim, 3)
  girdisinde açılır.

#### Kapsam boşluğu dalgası 3 (2026-11-23): play7 .. play10, playq, playx, Ctrl-Break

```
python tools/verify/mksave.py playN out/verify/scripts/playN.sav      # play7, play8, play9
python tools/verify/mkscript.py NAME out/verify/scripts/NAME.txt
python -u tools/verify/pair.py out/verify/scripts/NAME.txt NAME [--install out/verify/scripts/playN.sav=SAKLA001.SAV]
```
(play10, `play6.sav`'ı kurar.)

| Betik | Ne | Kontrol noktaları | Sonuç |
|---|---|---|---|
| `play7` | bayrak 99'dan bir adım önce bölge 4 (kayıt: 99 olmadan hikâye bayrakları): olay 12'li üç kapı "kapalI" (hareket iptal), sahanlık (14,0) - Seyh cehalet uçup gider, bayrak 99 ayarlanır - sonra silah dükkânı ve eşya dükkânı (alış, satış, tuşlarla), beş girdi üzerinde fareyle **üzerine gelme (hover) + Enter** ile seçenekler listesi | 53 | hepsi aynı; GRP.DAT, SAKLA001.SAV aynı |
| `play8` | bölge 1, (18,19) -> (19,19): silikon baba ve hikâye slayt gösterisi (FRPOS op 27, `demo_story_scene`), 7 resim grubunun hepsi, sonunda bayrak 4 | 14 | hepsi aynı |
| `play9` | bayrak 156 ile bölge 17: `demo_ending` - SON videosu (kareler 300 / 900), soon / devam, kip 1'de kayıt menüsü (yeni yuva "son"), jenerik (credits) | 21 | hepsi aynı; SAKLA002.SAV: açıklama kuyruğu (26 bayt, orijinalde ilklendirilmemiş yığın; play4/5/6'daki gibi) |
| `playq` | menü girdisi 3 (çabuk başla): videolar, açılış diyaloğu, bölge 0, bir tur | 5 | 4'ü aynı; `q03_turn`: yalnızca RNG durumu (aşağıda) |
| `playx` | menü girdisi 4: menüden jenerik | 13 | hepsi aynı |
| `play10` | play6'nın kaydı: (10,7) -> (19,18), olay 20 (bayrak 29 ve 99: op 33 `leave`): "hayIr" (kalır), sonra "evet": `demo_city_map_travel` - Eminönü'nde Enter + "h", 14 yukarı, 60 sola, dikdörtgen 10'a, Enter + "e": bölge 9'a varış, bir tur | 16 | 15'i aynı; `m13_turn`: yalnızca RNG durumu (aşağıda) |
| `playbrk` | ana menüde Ctrl-Break (`kbd 1D E0 46 E0 C6 9D`) | 2 (+ `int23`) | aynı |
| `playbrk3d` | bölge 0'da Ctrl-Break (çabuk başla) | 2 (+ `int23`) | aynı |
| `playcad` | bölge 0'da Ctrl+Alt+Del: oyunun INT 09h kancası Del'i yutar, `g_reset_attempt`'i (DGROUP 750B) ayarlar ve INT 1Bh çağırır | 2 (+ `int23`) | aynı (iki tarafta da 750B = 1) |

**Bulunan ve düzeltilen port hatası: jenerik arka planı.**  `demo_credits_exit`,
kaydırmadan önce grup fotoğrafını (grup.cmp, yüzey 0Bh'ye yüklü) ekrana kopyalar;
port ise yüzey 9'u kopyalıyordu - önceki ekran orada ne bıraktıysa.  Bundan sonra
yalnızca 225x200'lük kaydırma penceresi yeniden çizilir, bu yüzden ekranın geri kalanı
menü başlığını ("...ARI") ya da sonun "...ek"ini jenerik paletinde, benek benek
gösteriyordu.  `demolar.c`'de düzeltildi; playx ve play9'un jeneriği o zamandan beri
aynı.  "Devam Edecek..." ekranında kalan şey - resmin üstüne doğrudan, paneli olmadan
çizilen kayıt menüsü listesi (`son`, `[bosch]`) - orijinalin kendi ekranıdır (play9
`f07_savemenu` aynı).

**Ctrl-Break ve Ctrl+Alt+Del.**  DOSBox-X'te ham tarama kodları olarak (`kbd`, bkz.
"Girdi betikleri") 8042 üzerinden teslim edilir: BIOS tuş tamponunu boşaltır, 0000h
saklar ve INT 1Bh çağırır; DOS bunu oyunun sonraki `kbhit()` çağrısında (INT 21h 0Bh)
INT 23h'ye çevirir.  Ctrl+Alt+Del için oyunun `ctl_int09_isr`'ı Del basış kodunu
kendisi yakalar.  Port eskiden INT 23h işleyicisini doğrudan INT 09h kancasından
çağırıyordu; artık orijinal gibi INT 1Bh çağırıyor (`pc_int1b`), böylece kesme isteği
(break) aynı girdi okumasında geliyor.  `int23` dökümleri üç betiğin hepsinde aynı.

**Yalnızca RNG farkları (`q03_turn`, `m13_turn`).**  Ekran, DAC ve DGROUP'un geri
kalanı eşit; RTL rastgele durumu bir çekilişle farklı (yeniden tohumlamadan sonra port
2, orijinal 3 çekiliş).  Bu çekiliş, keşif döngüsünün portre göz kırpma zarıdır
(`random(20)`, `s3d_loop.c`); döngünün kendi t0'ından bu yana 1000 ms'den fazla saat
geçtiğinde yapılır.  Bu t0, döngü bir varıştan sonra başladığında ayarlanır; bu da
DOSBox-X'te oyun saatinde daha geç gelir (yükleme öykünülmüş zaman harcar:
`m12_arrived`'a 390 ms daha geç ulaşıldı), bu yüzden 2.5 s'lik pencere içinde
orijinalin zarları farklı noktalara düşer.  Mantık değil, saat evresi.

Zamanlama dersleri:

* Jenerik, kendi başlangıcından itibaren saate göre kayar; bu başlangıç DOSBox-X'te
  daha geç gelir: dökümleri, THANX.DOC satırının okunmasına (DGROUP BACC, küçük harfe
  çevrilmiş: "pc", "el", "ya", "oy", "se") ve ondan 1.5 s sonrasına çapalanır
  (`credits_dumps`).
* play8'in `h03_baba` dökümü ilk önce adımdan 4 s sonra alınmıştı: simge yüklemesi
  öykünülmüş zaman harcar, bu yüzden orijinal konuşmanın (say) daha başındaydı.  Artık
  imlecin konuşma için gizlendiği ilk okumaya çapalı (`sync E5F3 0000`).
* Çabuk başlamanın açılış diyaloğu saate bağlı metin kutularından oluşur: ondan sonra
  dökülür (ilk Esc'ten 72 s sonra), ilk Esc'ten itibaren her saniye sessiz tohumlarla
  (3B görünüm son video bitmeden açılmış olabilir).
* vcompare'in iki EMS sayfa eşlemesi kopyası (86F8, E7AC) için saat alanı aralıkları
  bir sözcük kısaydı: eşleme 4 sözcüktür.

#### Başlatma (DSMI) - port ve orijinal, 2026-11-22

`python tools/verify/mkscript.py startup out/verify/scripts/startup.txt;
python -u tools/verify/pair.py out/verify/scripts/startup.txt startup`, ardından
`vcompare.py out/verify/p_startup out/verify/d_startup --dg-detail --clock`
(oyun saatinin 250 ms, 1 s ve 5 s'sinde dökümler), ve
`tests/py/test_istanbul.py` (`test_startup_oracle`: main()'in introyu çağırdığı anda
uzak öbek (far heap) ve DGROUP, bir DOSBox-X bellek yakalamasına karşı).

* **Sürücü yükleme öbek bloğu: gitti.**  Başlatmanın sonundaki uzak öbek zinciri aynı -
  107 blok, aynı first/last/rover, karıştırıcının (mixer) DMA tamponu (1F8h paragraf)
  ve tabloları (404h) dahil - hem 2026-11-21 tarihli yakalamaya hem de bugünkü
  yapılandırmayla (SB16 IRQ 7) yapılan yenisine karşı.  Test artık bütün zinciri sıkı
  biçimde denetliyor.
* **6E06 (konvansiyonel bellek en düşük düzeyi, low-water) ve DE30 (EMS en düşük
  düzeyi): aynı** - `s_01000`, `s_05000`'de, menü kontrol noktalarında ve testte
  (6E06'nın `KNOWN` muafiyeti kaldırıldı).
* **7B70-7BE7** (testin 7BC8.. için "SDI device record" dediği): AMP'nin özel kesme
  yığınıdır (`ampInterrupt` SS:SP'yi DGROUP:7BE4'e geçirir); orijinal oraya son
  zamanlayıcı tikinin çerçevelerini bırakır (dönüş adresleri 171A:0F46/0F79 =
  seg0EF6, DS, yerel değişkenler), portun 8086 yığını yoktur ve sıfırlar bırakır.  Ana
  sistem alanı, hiçbir zaman veri olarak okunmaz.
* **DSMI zamanı E798/E7A0, AMP son tik zamanı 85A2**: saat alanları.  Oyun saati
  1000'de orijinalin DSMI zamanı 10100 (0.1 ms birimi), portunki 10000: DSMI
  zamanlayıcısı NKONTROL'ün saatinden önce başlar ve aradaki başlatma DOSBox-X'te
  öykünülmüş zaman harcar, portun sanal zamanında hiç harcamaz.  Zamanlayıcı servis
  yuvaları 85CE.. yalnızca rutinlerinin uzak işaretçilerinde farklıdır (F000:FEA5 eski
  INT 8, 1969:0096, 1566:0120 ..., portta yerel işaretçiler).
* `s_00250` karşılaştırılamaz: orijinalin ilk girdi okuması saat 440'ta, hâlâ metin
  kipindeyken; `s_01000` ve `s_05000` DGROUP'ta aynı ve ekranda yalnızca intro
  videosunun karesiyle farklı (D618: 1 s'de kare 9'a karşı 4 - video orijinalin
  saatinde daha geç başlar; intro, `frames_match.py` ile kare kare karşılaştırılır).

Açık bir DSMI başlatma farkı kalmadı.

`tools/verify/mkscript.py` betikleri oluşturur (yukarıdaki `play3`; `play2` kontrol
noktası etkisizleştirmesi olmayan eski sürümdür);
`tools/verify/mksave.py` hazırlanmış kayıtları oluşturur.

### Port tarafı

```
out/windows/Lale64.exe --game-dir <copy of orig> --script s.txt --out <dir>
                     [--seed n] [--step-us 100] [--frame-log <dir>]
```

`src/platform/sdl3_main.c` ("input scripts"): sanal zaman
(`pc_set_virtual_time`), pencere girdisi yok sayılır, olaylar `pc_set_input_hook`
üzerinden uygulanır (`src/platform/pc.c`: kbhit/getch/bioskey/INT 33h AX=3).
`--frame-log` her farklı video durumunu yazar (10 ms'lik her tikte bir denetim).

### DOSBox-X tarafı

```
python -u tools/verify/dbx_replay.py s.txt --out <dir> [--game-dir out/verify/game_dbx]
```

main()'de `srand`'dan sonra durur (0546:0292), tohumun üzerine yazar,
`tools/verify/hook.S`'yi (`build_hook.py` tarafından llvm-mingw ile çevrilir) D800h'deki
boş UMB'ye yazar (MCB CC00h DOS'a ait olarak işaretlenir) ve INT 16h/21h/33h'ye kanca
atar.  Kanca, teslim kuralını öykünülen PC'nin içinde uygular (tuşlar BDA halkasına
doldurulur, fare hareketleri sürücünün AX=4'ünden geçer, düğmeler AX=3'ün BX'inin
yerine konur).  Yalnızca `dump`/`quit` işaretleyicileri bir kesme noktasında durur
(`hook+marker_bp`); kanca VGA yazmaçlarını + DAC'ı zaten yakalamıştır, Python A000'ı
(chain-4) ya da 4 düzlemi ve DGROUP'u döker.

### Dökümler ve karşılaştırma

`<name>.vga` (misc, seq 0-4, gc 0-8, crtc 0-24, attr 0-20, DAC), `.vram`
(chain-4: A000'ın CPU görünümü, değilse 4 düzlem), `.dg` (DGROUP), `.txt` (saat).

* `tools/verify/pair.py <script> <tag> [--keep] [--install FILE=NAME]` - iki taraf +
  vcompare + yazılan dosyalar; `--keep` oyun klasörlerini sıfırlamaz (bir öncekinin
  kaydettiğini yükleyen bir betik: `play4`'ten sonra `play4load`); `--install`
  hazırlanmış bir dosyayı (`mksave.py`'nin bir kaydı) sıfırlamadan sonra iki oyun
  klasörüne de kopyalar (`play5`).
* `tools/verify/vcompare.py <port> <orig> [--png dir] [--dg-detail] [--clock]` - aynı
  adlı kontrol noktaları: yazmaçlar, DAC, işlenmiş resim, video belleği, DGROUP.
  DGROUP'ta yok sayılanlar: uzak işaretçiler (öbek/EMS/VGA, portta yerel), yeniden
  konumlandırılmış segment sözcükleri (16E0'a karşı 1F04), RTL vektörleri/PSP, AMP'nin
  özel ISR yığını (7BA0-7BE7), INT 33h bayrakları görüntüsü (E764), DGROUP sonunun
  ötesi (EA80+: yığın).
* `tools/verify/frames_match.py <port frame log> <orig dumps>` - zamanlamadan
  bağımsız: orijinalin her örneği, portun farklı durumları arasında aranır.
* `tools/verify/montage.py <dir> out.png [cols] [filter]` - küçük resim paftası
  (contact sheet).

Oyun klasörleri: kopyalar üzerinde çalışın (`out/verify/game_port`,
`out/verify/game_dbx`); oyun kayıtlarını oraya yazar.

### macOS'ta

Aynı komutlar; `.tools/venv/bin/python`, `out/macos/Lale64` (oradaki pair.py
varsayılanı) ve Homebrew'un `dosbox-x`'i ile (`$DOSBOX_X` bunu geçersiz kılar).
Yöntemin değil, ana sistemin farkları:

* DOSBox-X'in hata ayıklayıcısı Windows dışında `MEMDUMPBIN`'de mutlak yol kabul etmez;
  `oracle.py` yolları depo köküne, yani öykünücünün çalışma dizinine göreli verir.
* `build_hook.py`, `hook.S`'yi sistemin clang'ıyla çevirir ve düz görüntüyü (flat
  image) kendisi bağlar (`ld.lld` yok: `link_flat` nesnenin `R_386_16` yer
  değiştirmelerini (relocation) uygular).  Görüntünün llvm-mingw derlemesiyle bayt bayt
  karşılaştırılmadı; çalıştığını, teslim ettiği oynanışlar gösteriyor.
* Öykünücü "Debugger in Mac OS X is not available unless you start DOSBox-X from a
  terminal" yazar - bu, etkileşimli hata ayıklayıcı penceresiyle ilgilidir; burada
  kullanılan `mcp_server` bağlantısı çalışır.
* İlk kurulum: `cp -R orig out/verify/game_port; cp -R orig out/verify/game_dbx`.

Sonuçlar: [`PROGRESS.md`](PROGRESS.md), "macOS port".

## English

One input script drives both programs; both write checkpoint dumps in the same
format; Python tools compare them.

### Input scripts

Text file, one event per line (`#` comments), times in ms of the **game's own
clock** DGROUP:E74A (NKONTROL, +10 per 100 Hz tick):

```
seed 1234                    srand() seed at start-up (time() override)
50000 key 1C0D               BIOS key word scan<<8 | ASCII
52000 mouse 320 100 1        driver coordinates (x 0..639 in 320-pixel modes), buttons
52300 buttons 0
60000 dump c04_district      checkpoint dump
dumpevery 250 50000 250 i    dump i_<ms> every 250 ms
70000 quit
```

**Delivery rule (both sides):** at every *input read* of the game - `kbhit`
(INT 21h 0Bh), `getch` (INT 21h 01h/07h/08h, and every wake-up while it
blocks), `bioskey` 0/1/10h/11h (INT 16h, idem) and the mouse poll (INT 33h
AX=3) - all events whose time is <= the clock are applied, in file order.

Why: host time and frame counts differ between the two programs (DOSBox-X
spends emulated time decoding/loading, the port's virtual time does not), but
the game's clock and its input reads are game-visible on both.  When the game
waits for input, both see an event at the first read after the same tick.
Breakpoints per input read are not an option on the DOSBox-X side: every
debugger command costs 50 ms (the emulator polls the link at 20 Hz).

**Read-count sync** (`+n` instead of a time): a *counted read* is the entry
of one of the functions above (not the wake-ups of a blocking read; on the
DOSBox-X side the INT 16h calls DOS makes inside the game's INT 21h call are
not counted).  `+n <event>` is due n counted reads after the read that
applied the previous event - or at once at a wake-up of a blocking read.
Loops that poll once per pass then make the same number of passes between
two events on both sides; chargen calls `random(1000)` once per pass, so its
rolls only match this way.  Anchors re-align the count where a clock-bounded
loop (videos, fades with input polls, timed texts) came before:

```
150000 sync D5DE 0019        nothing; due only when also DGROUP word D5DE == 19h
150000 syncne D5DE 0000      ... when D5DE != 0 (battle: the first actor is up)
+30000 key 4800              30000 counted reads after the anchor
+0 srand 00001000            RTL random state DGROUP:94C4 := 1000h (re-aligns rand())
```

**Clock-relative timing** (`@n`): due when the clock is >= n ms past its
value at the read that applied the previous event (hook.S type bit 40h,
`sc_rel == 2` in sdl3_main.c).  Needed wherever the game waits on the clock
in a tight polling loop: `ui_wait_click_or_key` (n x 20 ms), the message
box (mode 0: (lines*6+20) x 150 ms), spell and bump animations, palette and
music fades.  There the reads run at CPU speed - about 14-20 reads/ms in
the port, 37-43 in DOSBox-X - so a read count is a different time on each
side, and keys that arrive inside such a wait are flushed.  `mkscript`'s
`Script.after(n)` switches to `@`, `reads(n)` back to `+`; the mode
sticks, so a clock gap followed by a read gap needs an event in between
(a `srand`).

Checkpoint events that neutralise clock-timed state (both sides):

```
+0 srand 00001000            RTL random state DGROUP:94C4 := 1000h
+0 poke 48E8 03 00 02 00     write bytes (hex) to DGROUP:48E8.. (split into
                             byte/word events, hook.S type 8)
```

Raw keyboard bytes, for what no BIOS key word can express (Ctrl-Break,
Ctrl+Alt+Del):

```
+0 kbd 1D E0 46 E0 C6 9D     scan-code bytes, in order (one hook.S type-9
                             event per byte)
```

DOSBox-X side: each byte goes through the 8042 - command D2h ("write
keyboard output buffer") on port 64h, the byte on 60h - and `hook.S` opens
interrupts until the output buffer is empty again, so IRQ 1 delivers it to
the INT 09h chain (the game's `ctl_int09_isr`, then the BIOS).  Port side:
`sc_kbd` offers the byte to `pc_int09` (the game's hook) first, then models
the BIOS: E0 prefix, Ctrl (1D/9D) and Alt (38/B8) in the shift flags, and
Ctrl + 46 make as Ctrl-Break (`pc_ctrl_break`: key buffer emptied, word 0000h
stored, INT 1Bh).  Other bytes are logged "not modelled".

**`int23` dump:** both sides dump a checkpoint named `int23` when INT 23h is
issued (DOSBox-X: `hook.S` keeps IVT 23h pointing at a wrapper that dumps,
then jumps to the handler that was there; port: the observer of
`pc_set_int23_observer`, called from `dos_break_check` before the handler).
The game's handler exits to DOS, so the replay stops after that dump.

#### What is clock-timed and how play3 neutralises it

| State | Why it differs | play3 |
|---|---|---|
| chargen walking frame / blink 48E8/48EA, the blink's `random(3)` (150 ms tick) | ticks fall on different passes | at each checkpoint `poke 48E8 = 3, 48EA = 2` (no new blink; the next tick draws frame 0 and the normal face), `sync 48E8 0000` (that tick), `srand`, dump; at Space `srand` + `poke 48E8 = 0 0 0 0` - the roll and the next member's screen are then identical |
| 3DSYS portrait blink: `random(20)` once a second, timer and state are locals whose phase depends on load times (DOSBox-X spends emulated clock loading, the port's virtual time does not) | blinks start on different calls; a blink cut by leaving the loop leaves its face on screen | `srand` with a *quiet seed* (`mkscript.quiet_seed`: the next 8 `rand()` values are all >= 1639, so no `random(20)` is 0) every 2 s from the story on and at every exploration key / dump |
| battle talking face `random(2)` every 150 ms while a voice plays (BBC4) | as above | keys 30000 reads after the previous checkpoint (voice over on both sides), `srand` at every key, dumps 10000 reads after a key, 100000 after TAB (the enemy turn's clock-timed animations; the port makes ~3x fewer reads per ms than DOSBox-X) |
| AMP/DSMI timer state, DSMI time, GFL frame/start times, s3d fade time, voice end time 73D1, EMS mapping registers that follow GFL streaming | music/videos started at load-time-dependent clocks | not neutralised: `vcompare.py` lists them by name only with `--clock` (`CLOCK_FIELDS`) |

`vcompare.py` also ignores *host fields* (`HOST_FIELDS`: Borland RTL data
the port replaces - _8087, atexit count, exit hooks, conio _video, delay
calibration/brk/environ, overlay-manager table, harderr vectors - and the
CD request block, kept by cd5737.c in a native mirror; none is read by game
code) and far pointers whose segment is 0 in the port (`farptr_ranges`,
`farptr_tail_filter`: records that keep only the offset).

#### Result (2026-11-21, `play3`)

`python tools/verify/mkscript.py play3 out/verify/scripts/play3.txt;
python -u tools/verify/pair.py out/verify/scripts/play3.txt play3`
(pair.py resets the game folders, runs both sides, compares the dumps and
the files the game wrote):

* menu, chargen start + 4 members: identical (screen, DAC, registers,
  DGROUP); GRP.DAT identical.  `c03_after_chargen` was dumped at a fixed
  read count inside the story video (seq1.GFL, 10 fps, frames due by the
  clock): the port showed frame 3, the original frame 1.  It is now
  anchored on the video itself - `sync D618 0005` (D618 = the last frame
  processed) - and is identical (play4 runs of 2026-11-21, which share
  play3's prefix).
* district 0: arrival, 2 turns, save menu, save to slot 2, the door
  question, the treasure screen: identical; SAKLA001.SAV identical.
* the dog battle: start (first actor up) and 12 battle keys (UP, UP,
  ENTER, TAB x3, both sides' turns): identical at all 13 checkpoints.
* So every checkpoint of play3 but `c03_after_chargen` is identical in
  registers, DAC, screen, video memory and DGROUP (minus host fields; the
  clock fields are listed with `--clock`), and both written files match.
  (Run of 2026-11-21 after the mouse latch; earlier runs differed only in
  the 3DSYS cursor shape E5E1/E5E3 = host pointer, and in battle dumps
  taken mid enemy turn before the 100000-read gap.)
* Shop, item exchange, spells, load: see play4 below.
* Differential suite after these changes: 219/219 (out/base).

Port fixes found on the way: the DSMI driver load opens SDISB16.DSD through
the game's file layer (its 512-byte stdio buffer is the paragraph that made
6E06/DE30 differ, `src/dsmi/dds.c`); the save description buffer is an
uninitialised stack buffer written in full (60 bytes) - modelled as a
zeroed static (`src/game/s3d_menu.c`).

Mouse: hook.S reports at AX=3 only the position the game (AX=0/4/7/8) or
the script set, as the driver clamped it - with `autolock = false`
DOSBox-X's mouse integration otherwise moves the DOS mouse whenever the
host pointer crosses its window (seen as a different 3DSYS cursor shape
E5E3 in one run).

DOSBox-X configuration: the SB16 must be on IRQ 7 (tools/dosbox/ist.conf),
as SES.CFG says.  With IRQ 5 the DSMI driver unmasks IRQ 7, gets no
interrupt, the DMA stalls, every voice's position stays 0 and
ui_wait_voice (05DC:1D1B: position + 20h >= sample end, or Esc) never ends
and eats the script's keys; on hardware (and in the port) a 300h-byte
voice at 3676 Hz ends after ~0.21 s.  Dumps made before this change ran
without sound and are not comparable.

#### play4: battle with spells, game over + load, item exchange, shops, camp, save

`python tools/verify/mkscript.py play4 out/verify/scripts/play4.txt;
python -u tools/verify/pair.py out/verify/scripts/play4.txt play4`
(about 45 min; then `play4load` with `pair.py --keep`, see below).

Flow (play3's prefix up to the dog battle, then):

* **Battle with a spell and the enemy AI, to a loss** (`c12_b00..40`, 42
  action groups planned on the port, `out/verify/h/plan2.py`): Rifki (unit
  27) casts vecize 1 "pis" (`v`, Enter, `o` next enemy, Enter) on the dog
  once it is adjacent, then bumps into it (keypad moves into the enemy);
  the others pass (Tab).  The party has no weapons, so Enter attacks are
  ignored (the attack needs one) and vecize 1 does no damage; the dog's AI
  bites the party to death: ISOSYS result 632E = 2.
* **Game over and load**: "eski bir oyuna devam edecek misiniz?" -> `e` ->
  load menu -> Enter loads SAKLA001 (`c13_*`).  Load is only reachable
  from the main menu and from this game-over loop.
* **Second battle, won**: the door/treasure event again, then four groups
  in which every member casts vecize 1 (out of range: "desem de duyamaz
  ki!") and tries an attack; the dog flees on its turn, result 1
  (`c14_*`).
* **Item exchange** with the man at (8,1) of district 0 (`c15_*`, `i`,
  Enter, Enter), the key event (flag 2), leaving district 0 (flags 81) and
  arriving in district 1 (`c16..c19`).
* **Shops** (district 1: goods at (3,7), weapons at (11,10), food at
  (13,16)): in goods and weapons, each step dumped (`c21_goods_*`,
  `c23_weapons_*`): `b` converts the currencies, buy by keys (`i` shop
  row, Enter, Right Right, Enter), sell by keys (`i`, Enter, `s`), buy by
  mouse (shop slot 3 then inventory slot 4), sell by mouse (inventory slot
  4 then the sell zone), Esc.  Money: 11 -> 216 (convert) -> 78 (key buy)
  -> 87 (key sell) -> 11 (mouse buy) -> 50 (mouse sell).  Food stall:
  "evet", 12 paid, hunger cleared (`c24/c25`).
* **Camp** (`k`, rest, `c26/c27`) and **save** to a new slot (SAKLA002,
  description "cd", `c28/c29`).
* **Doctor**: not in play4 - the only doctor event (key 31313127) is in
  district 4 (PC4 at (8,11)), out of reach of a short script; play5 below
  gets there from a prepared save.

Result (2026-11-21): **all 112 checkpoints of play4 are identical** -
registers, DAC, screen, video memory and DGROUP (minus host fields; clock
fields listed with `--clock`), sound on in both programs (SB16 IRQ 7);
GRP.DAT and SAKLA001.SAV identical.  SAKLA002.SAV differs in 48 bytes,
0x0B-0x3A: the tail of the 60-byte description after its NUL, which
`s3d_save_load_menu` writes from an uninitialised stack buffer
([bp-3CAh]).  In the original that tail is a stale DOS find_t record
("SAKLA001.SAV", attributes, time, size) left by the load menu that ran
from the game-over loop at a different stack depth; the port's static
buffer holds the text of its previous use.  Reproducing it would need a
model of the original stack's history; no game code reads those bytes.
`play4load` (`pair.py ... play4load --keep`: main menu, load SAKLA002 in
a fresh session, one turn) - 4 checkpoints identical except the load
menu's list entry A6AB-A6DB, which holds that same description tail; the
loaded game (district 1, (12,16), 107 money) is identical.
No port bug was found by play4: every divergence on the way was script
timing (fixed in the script, see below).  Differential suite after
these changes: 219/219 (out/base).

Timing lessons (all in `mkscript.py`):

* Clock-timed waits (sign texts, shop greetings, the cast and bump
  animations, fades) use `@n`; keys inside 3D walks are re-seeded with a
  quiet seed in their own read (`rkey`, `rwalk`), long waits re-seed every
  second (`quiet_wait`); dialogs are anchored on the flags they set at the
  end (`sync B9CA+n`).
* `quiet_wait` re-seeds with seeds whose next 64 draws are quiet (the 3D
  view draws more than 8 between two re-seeds a second apart; with 8 a
  portrait blink started on one side only, c25).
* Dumps after a shop's Esc or the game-over result wait for the
  100 ms-step fades of `s3d_apply_sound_options` to end (DAC, 3D6E, 3D70,
  7318, 731C differed mid-fade).
* The shops roll their stock with `random()` right after the greeting:
  no re-seed may fall between the move's re-seed and that roll.
* After a cast or a bump, 12 s of clock (`battle_groups`): with read gaps
  DOSBox-X was still in the spell animation when the next Tab came.
* The second battle's anchor needs `poke D5DE 0000` first (D5DE keeps the
  first battle's last actor); game over is anchored on `sync 632E 0002`.
* `vcompare.CLOCK_FIELDS` gained BBC4 (ISOSYS talking-face timer) and D534
  (the AI decision's clock word): clock stamps only.

#### play5: district 4 from a prepared save - doctor, fortune-teller, hotel, 3DSYS keys

```
python tools/verify/mksave.py play5 out/verify/scripts/play5.sav
python tools/verify/mkscript.py play5 out/verify/scripts/play5.txt
python -u tools/verify/pair.py out/verify/scripts/play5.txt play5 --install out/verify/scripts/play5.sav=SAKLA001.SAV
```
(port 25 s, DOSBox-X about 10 min.)

**The save.**  `mksave.py` derives it from a save the original wrote itself
(`tools/verify/saves/play4_SAKLA002.SAV`, play4's SAKLA002: district 1,
party ali/ayse/rifki/zeki) and changes only: the description ("doktor" +
NULs), position (11,12) facing south, district 3 (PC4), money 206, the
health words of members 2-4 (1, -10, -2), and the three things
`s3d_load_district(first)` sets on a real arrival (shop stocks BBB7/BBAC
cleared, city-map marker BB8A/BB88 = 107/226).  Flags, time, music track,
options and the fee byte (106) stay.  `pair.py --install FILE=NAME` copies
it into both game folders after the reset.  The crafted save loads
identically: `d02_loaded` is identical in DGROUP, screen and DAC.

Flow (39 checkpoints `d00`..`d38`):

* **Load** from the main menu (`d00_menu`, `d01_loadmenu`, `d02_loaded`).
* **Fortune-teller** (10,13), event 18 - ops not used by play3/4: a
  question (`d03_gypsy_q`), `pay 5` (op 26: 206 -> 201), four long texts
  (`d04_gypsy_text` in the first), music 7 and back to 1 (op 25, with
  fades), `findminmax` + `say 5` (ops 23, 17 with WHO), flag 100
  (`d05_gypsy_done`).
* **Doctor** (8,11), through the sign of (9,10) (event 16), four visits:
  1. the fee question "visite : 106" (`d06_doctor_q`), `h`: refused
     (`d07_refused`);
  2. `e`, "kim servis istiyor ?" (`d09_who`), then members that are not
     dead: F1 (health 4, `d10`), a click on portrait 2 (health 1, `d11`), F4
     (health -2, `d12`) - "bu adam OlU deGil. baSkasInI seCin" each time -
     and Esc (`d13_cancelled`, no money taken);
  3. `e`, a click on portrait 3 (health -10): the four texts, health 1,
     201 -> 95 (`d16_revived`);
  4. `e` with 95 < 106: "bu para yetmez!" (`d18_too_poor`).

  The event only distinguishes health <= -5 ("dead") from the rest.
* **Hotel**: the sign of (14,9) (event 13), the room (12,9) with the fixed
  key 31313131 (3D5E bit 2: no encounters while camping, `d19_hotel_room`),
  a camp in it (`d20_camp` inside the rest, `d21_rested`: health 4/4/7/3),
  the keeper (13,8), event 11 (`d22_keeper`).
* **Save** to a new slot, SAKLA002 "ef" (`d23_savemenu`, `d24_saved`).
* **3DSYS keys** no other play-through uses: a step back (`d25`), the
  sidesteps `4`/`6` against doors (refused, `d26`/`d27`), PgDn/PgUp view
  size with a turn in the 200x200 view (`d28`..`d30`), the character
  screen F1 and F3 inside it (`d31`, `d32`, back `d33`), the options list
  `t` (`d34`, Esc `d35`), `n`: the city map har4.cmp (flag 82, `d36`), the
  location text (`d37`) and back through `s3d_enter_view` (`d38`).

Result (2026-11-22): **all 39 checkpoints identical** - registers, DAC,
screen, video memory and DGROUP (minus host fields; clock fields listed
with `--clock`; the clocks themselves differ by up to 1 s at the same
checkpoint, DOSBox-X's emulated loading and drawing time).  GRP.DAT and
SAKLA001.SAV identical.  SAKLA002.SAV differs in 30 bytes, 0x0C-0x2F: again
the tail of the description after its NUL, the original's uninitialised
stack buffer ([bp-3CAh] of `s3d_save_load_menu`) - here it holds what the
`s3d_read_save` of the main menu's load left at that stack depth
("sinsan1.cmp", the last portrait name, return addresses); the port's
static buffer is zero there.  The other 1494 bytes are equal.  No port bug
was found.

Two statements of the documentation were wrong and are corrected from the
original's behaviour: a loaded save keeps its doctor's fee (FORMATS.md said
it is re-rolled), and the doctor treats members at -5 or lower (not "above
-5").

Timing lessons:

* Every way out of the doctor but "too poor" ends in `fr_redraw`, which
  puts the party back on (9,11) still facing west: one Up re-enters.  "Too
  poor" leaves it in the doctor's cell.
* The checkpoint inside the camp was first taken 2 s after `k`: the port
  was at 10:17, the original at 10:11 (one calendar minute per 50 ms of
  clock *plus* the drawing, which costs emulated time in DOSBox-X).  It is
  now anchored on the calendar minute (`sync 3D64 0014`): the first input
  read of that minute's pass is the same point on both sides.
* Long dialogs are anchored on what they set at their end: flag 100
  (`sync B9D6 0010`) for the fortune-teller - its texts take 53 s in the
  port and longer in DOSBox-X - and the revived member's health word
  (`sync 9AF4 0001`) for the doctor; one-line signs get 14 s of clock.
* The two waits of `n` (2000 and 2400 rounds) are ended by keys sent on
  the clock (8 s and 4 s after the previous event).

#### play6: district 4 after flag 99 - sidesteps, options, right button, food stall

```
python tools/verify/mksave.py play6 out/verify/scripts/play6.sav
python tools/verify/mkscript.py play6 out/verify/scripts/play6.txt
python -u tools/verify/pair.py out/verify/scripts/play6.txt play6 --install out/verify/scripts/play6.sav=SAKLA001.SAV
```
(port 15 s, DOSBox-X about 10 min; `play6side`, `play6opt1`, `play6opt`,
`play6doctor`, `play6food` stop after a part.)

**The save.**  As play5's (same base, same `patch`), plus what the story of
district 4 leaves behind once its shops are open.  The doors of the food
stall (16,9), the weapon shop (8,16) and the goods shop (3,17) carry event
12, whose trigger 1 says "kapalI" and cancels the move while **flag 99** is
clear.  Flag 99 is set in one place only (all 18 PCS files and the code
searched): event 10 of PCS4, trigger 2 of the cell (14,0) - the landing of
the stairs out of the cellar of Yeni Camii.  With flag 31 set it shows Seyh
cehalet flying away ("Cok geC kalDIk..."), plays music 1 and sets 99.  The
chain that leads there, each flag set by the event named: 29 (event 1,
(0,19): tabIk vesile takes the party to the resistance; without it the
mosque is "closed for repairs"), 97 (event 2, (8..10,5): the battle with the
birds at the mosque's doors), 30 (event 3, (9,1): the bird that tells of the
cellars), 98 (event 4, (7,0): the battle at the hidden door), 31 (event 5,
(3,3): Seyh cehalet in the cellar; event 6 then teleports to (14,0)), 99.
`mksave.py play6` sets these six flags and music track 1 (event 10's
`music 1` comes right before its `setflag 99`), position (10,7) facing
north, money 126, health 4 / 1 / -10 / -2 as in play5, and hunger words
40 / 36 / 30 / 44 (the base save was written right after a meal: all 0).
Not reproduced: the optional loot of events 1 and 5 (three weapons, a
purse) and the flags of the optional battles (109, 110).

Flow (46 checkpoints `e00`..`e30`):

* **Load** (`e00_menu`, `e01_loadmenu`, `e02_loaded`).
* **Sidesteps** in the open street in front of the mosque: facing north
  `4` -> (9,7) (`e03_side_l`), `6` -> (10,7) (`e04_side_r`); a turn to the
  east (`e05_east`), `4` -> (10,6) (`e06_side_l_east`), `6` -> (10,7)
  (`e07_side_r_east`).  All four succeed.
* **Options list** (`t`, `e08_options`).  It holds seven entries: walking /
  door animation, battle speed (0..9), game music, battle music, sound
  effects, difficulty, view size.  Each is changed once by a left click on
  it (`e09_opt1_anim` .. `e09_opt7_view`: animation off, speed 5 -> 6, game
  music off, battle music off, effects off, difficulty 0 -> 1, view 200 ->
  100) and then back by keys with the pointer moved off the list
  (`e10_opt7_view` .. `e10_opt1_anim`, Down/Up + Enter; speed 6 -> 7).
  Entry 7 switching to the big view sets the animation byte, so the Enter on
  entry 1 switched it off again and one more Enter restores it
  (`e10_opt1_anim_on`).  Esc closes (`e11_view`); opened again
  (`e12_options`) and closed with the right button (`e13_view`).  Final
  option bytes 1 7 1 1 1 0 1.
* **Doctor** (8,11), three visits: `e` and at "kim servis istiyor ?"
  (`e15_who`) the **right mouse button**: cancelled, no money taken
  (`e16_rclick_cancelled`); `e`, F2 (health 1: "bu adam OlU deGil",
  `e18_not_dead_f2`) and the right button at that second prompt
  (`e19_rclick_cancelled2`); `e`, F3 (health -10): revived, 126 -> 20
  (`e21_revived`).
* **Food stall**: the signs of (9,10) and (16,8) (event 15), the door cell
  (16,9) - event 12 lets the party in (`e22_stall_door`) - and the stall
  (15,9), key 31313128, three visits: the price question "adam baSI 3"
  (`e23_food_q`), `h`: refused (`e24_food_refused`); price 4, `e`: the meal
  picture (`e26_eating1`), 3 living members x 4 = 12 paid, 20 -> 8, the four
  hunger words 55/51/40/44 -> 0 (`e26_fed1`); price 4 again, `e` with
  8 < 12: "not enough money" (`e28_food_too_poor`), nothing changed
  (`e28_food_left`).  These are all three branches of `shop_food`.
* **Save** to a new slot, SAKLA002 "gh" (`e29_savemenu`, `e30_saved`).

Result (2026-11-23): **all 46 checkpoints identical** - registers, DAC,
screen, video memory and DGROUP (minus host fields; clock fields listed
with `--clock`).  GRP.DAT and SAKLA001.SAV identical.  SAKLA002.SAV differs
in 30 bytes, 0x0C-0x2F: the description tail after its NUL (the original's
uninitialised stack buffer, the same bytes as in play5: "sinsan1.cmp" and
return addresses; zeros in the port).  The other 1494 bytes are equal,
among them the changed option bytes and the flags.  No port bug was found;
no run showed a mouse-cursor-only difference.

`s3d_apply_sound_options` and the options list: the list's own changes use
blocking `gr_fade` calls (game music: 300 ms down, module stopped or
started, 300 ms up; view size back to 100: 100 ms down and up), they leave
the counters 3D6E / 3D70 / 3D72 at 0, so the idle callback has nothing to
step after any entry - every options checkpoint, 3 s after the choice (8 s
for entries 3 and 7), is identical without waiting for a fade.  The
callback does step something later in play6: entering the roofed cell
(16,9) makes `s3d_apply_daylight` set the brightness and tempo counters,
which then move one step per 100 ms of clock.

Timing lessons (both first-run differences were script timing):

* `e22_stall_door` was first dumped 2.5 s after the step into (16,9): mid
  fade (port 3D6E/3D70 = 11/-1, original 13/-3, 106 DAC entries, 7318,
  731C).  Now 8 s later, after the fade.
* `e26_eating1` was first dumped 1.2 s after `e`.  The meal picture has two
  waits, 50 rounds inside `fr_show_icon` and 120 after it: 1 s + 2.4 s in
  the port, about three times that in DOSBox-X, so the port was in the
  second wait and the original in the first (same screen; EMS use 6E0A,
  8702, DC00 and the handle of surface 42h differed).  Now anchored:
  `fr_show_icon` frees surface 42h before loading and allocates it again
  after its wait, and `sync E4AF 0000` is the first read of that wait.
* A step's random encounter roll (`random(300) + 1 <=` the map's rate, 2 in
  PC4) comes more than 8 draws after the key's re-seed: with the 8-draw
  quiet seeds one step of the walk to the stall started a battle.  play6's
  moves use 64 quiet draws (`rkey(..., n=64)`).
* The options list's hotspots overlap ((24, 133 + 7i)-(162, 149 + 7i)) and
  the first match wins, with no hot-point offset for the list's cursor:
  entry i is clicked at y = 146 + 7i.  For the keys the pointer must be off
  the list (an entry under it takes the selection back), and the list opens
  on entry min(last choice, 3).

#### Gap wave 3 (2026-11-23): play7 .. play10, playq, playx, Ctrl-Break

```
python tools/verify/mksave.py playN out/verify/scripts/playN.sav      # play7, play8, play9
python tools/verify/mkscript.py NAME out/verify/scripts/NAME.txt
python -u tools/verify/pair.py out/verify/scripts/NAME.txt NAME [--install out/verify/scripts/playN.sav=SAKLA001.SAV]
```
(play10 installs `play6.sav`.)

| Script | What | Checkpoints | Result |
|---|---|---|---|
| `play7` | district 4 one step before flag 99 (save: the story flags without 99): the three event-12 doors "kapalI" (move cancelled), the landing (14,0) - Seyh cehalet flies away, flag 99 set - then the weapon shop and the goods shop (buy, sell, by keys), the options list by mouse **hover + Enter** over five entries | 53 | all identical; GRP.DAT, SAKLA001.SAV identical |
| `play8` | district 1, (18,19) -> (19,19): silikon baba and the story slideshow (FRPOS op 27, `demo_story_scene`), all 7 picture groups, flag 4 at the end | 14 | all identical |
| `play9` | district 17 with flag 156: `demo_ending` - SON video (frames 300 / 900), soon / devam, the save menu in mode 1 (new slot "son"), the credits | 21 | all identical; SAKLA002.SAV: the description tail (26 bytes, uninitialised stack in the original, as play4/5/6) |
| `playq` | menu entry 3 (çabuk başla): videos, the opening dialogue, district 0, a turn | 5 | 4 identical; `q03_turn`: RNG state only (below) |
| `playx` | menu entry 4: the credits from the menu | 13 | all identical |
| `play10` | play6's save: (10,7) -> (19,18), event 20 (flags 29 and 99: op 33 `leave`): "hayIr" (stays), then "evet": `demo_city_map_travel` - Enter + "h" in Eminönü, 14 up, 60 left into rectangle 10, Enter + "e": the arrival in district 9, a turn | 16 | 15 identical; `m13_turn`: RNG state only (below) |
| `playbrk` | Ctrl-Break (`kbd 1D E0 46 E0 C6 9D`) at the main menu | 2 (+ `int23`) | identical |
| `playbrk3d` | Ctrl-Break in district 0 (quick start) | 2 (+ `int23`) | identical |
| `playcad` | Ctrl+Alt+Del in district 0: the game's INT 09h hook swallows Del, sets `g_reset_attempt` (DGROUP 750B) and issues INT 1Bh | 2 (+ `int23`) | identical (750B = 1 on both sides) |

**Port bug found and fixed: the credits background.**  `demo_credits_exit`
copies the band photo (grup.cmp, loaded into surface 0Bh) to the screen
before the scroll; the port copied surface 9 - whatever the previous
screen left there.  Only the 225x200 scroll window is redrawn after that,
so the rest of the screen showed the menu title ("...ARI") or the ending's
"...ek" in the credits palette, as speckle.  Fixed in `demolar.c`; playx
and play9's credits identical since.  What stays on "Devam Edecek..." -
the save menu's list (`son`, `[bosch]`) drawn straight onto the picture,
no panel - is the original's own screen (play9 `f07_savemenu` identical).

**Ctrl-Break and Ctrl+Alt+Del.**  Delivered as raw scan codes (`kbd`, see
"Input scripts") through the 8042 in DOSBox-X: the BIOS empties the key
buffer, stores 0000h and issues INT 1Bh; DOS turns that into INT 23h at
the game's next `kbhit()` (INT 21h 0Bh).  For Ctrl+Alt+Del the game's
`ctl_int09_isr` traps the Del make code itself.  The port used to call
the INT 23h handler from the INT 09h hook directly; it now issues INT 1Bh
like the original (`pc_int1b`), so the break arrives at the same input
read.  The `int23` dumps are identical in all three scripts.

**RNG-only differences (`q03_turn`, `m13_turn`).**  Screen, DAC and the
rest of DGROUP equal; the RTL random state differs by one draw (port 2,
original 3 draws after the re-seed).  The draw is the explore loop's
portrait-blink roll (`random(20)`, `s3d_loop.c`), made whenever more than
1000 ms of clock have passed since the loop's own t0.  That t0 is set when
the loop starts after an arrival, which in DOSBox-X comes later on the
game clock (loading costs emulated time: `m12_arrived` was reached 390 ms
later), so within the 2.5 s window the original's rolls fall at
different points.  Clock phase, not logic.

Timing lessons:

* The credits scroll by the clock from their own start, which comes later
  in DOSBox-X: their dumps are anchored on the THANX.DOC line being read
  in (DGROUP BACC, lower-cased: "pc", "el", "ya", "oy", "se") and 1.5 s
  after it (`credits_dumps`).
* play8's `h03_baba` was first dumped 4 s after the step: the icon load
  costs emulated time, so the original was earlier in the say.  Now anchored
  on the first read with the cursor hidden for the say (`sync E5F3 0000`).
* The quick start's opening dialogue is clock-timed text boxes: dumped
  after it (72 s after the first Esc), with quiet seeds every second from
  the first Esc (the 3D view can be up before the last video ends).
* vcompare's clock-field ranges for the two EMS page-map copies (86F8,
  E7AC) were one word short: the map is 4 words.

#### Start-up (DSMI) - port vs original, 2026-11-22

`python tools/verify/mkscript.py startup out/verify/scripts/startup.txt;
python -u tools/verify/pair.py out/verify/scripts/startup.txt startup`, then
`vcompare.py out/verify/p_startup out/verify/d_startup --dg-detail --clock`
(dumps at 250 ms, 1 s and 5 s of the game clock), and
`tests/py/test_istanbul.py` (`test_startup_oracle`: the far heap and DGROUP
at main()'s call of the intro against a DOSBox-X memory capture).

* **Driver-load heap block: gone.**  The far-heap chain at the end of
  start-up is identical - 107 blocks, same first/last/rover, including the
  mixer's DMA buffer (1F8h paragraphs) and tables (404h) - against the
  capture of 2026-11-21 and a new one made with today's configuration (SB16
  IRQ 7).  The test now checks the whole chain strictly.
* **6E06 (conventional low-water) and DE30 (EMS low-water): identical** at
  `s_01000`, `s_05000`, the menu checkpoints and in the test (the
  `KNOWN` exemption of 6E06 is removed).
* **7B70-7BE7** (the test called 7BC8.. "SDI device record"): it is AMP's
  private interrupt stack (`ampInterrupt` switches SS:SP to DGROUP:7BE4);
  the original leaves the frames of the last timer tick there (return
  addresses 171A:0F46/0F79 = seg0EF6, DS, locals), the port has no 8086
  stack and leaves zeros.  Host field, never read as data.
* **DSMI time E798/E7A0, AMP last-tick time 85A2**: clock fields.  At game
  clock 1000 the original's DSMI time is 10100 (0.1 ms units), the port's
  10000: the DSMI timer starts before NKONTROL's clock and the start-up
  in between costs emulated time in DOSBox-X, none in the port's virtual
  time.  The timer-service slots 85CE.. differ only in their routine far
  pointers (F000:FEA5 the old INT 8, 1969:0096, 1566:0120 ..., native
  pointers in the port).
* `s_00250` is not comparable: the original's first input read is at clock
  440, still in text mode; `s_01000` and `s_05000` are identical in DGROUP
  and differ on screen only by the intro video's frame (D618: frame 9 vs
  4 at 1 s - the video starts later on the original's clock; the intro is
  compared frame by frame with `frames_match.py`).

No open DSMI start-up difference remains.

`tools/verify/mkscript.py` builds the scripts (`play3` above; `play2` is
the older version without the checkpoint neutralisation);
`tools/verify/mksave.py` builds prepared saves.

### Port side

```
out/windows/Lale64.exe --game-dir <copy of orig> --script s.txt --out <dir>
                     [--seed n] [--step-us 100] [--frame-log <dir>]
```

`src/platform/sdl3_main.c` ("input scripts"): virtual time
(`pc_set_virtual_time`), window input ignored, events applied through
`pc_set_input_hook` (`src/platform/pc.c`: kbhit/getch/bioskey/INT 33h AX=3).
`--frame-log` writes every distinct video state (one check per 10 ms tick).

### DOSBox-X side

```
python -u tools/verify/dbx_replay.py s.txt --out <dir> [--game-dir out/verify/game_dbx]
```

Breaks at main() after `srand` (0546:0292), overwrites the seed, writes
`tools/verify/hook.S` (assembled by `build_hook.py` with llvm-mingw) into the
free UMB at D800h (MCB CC00h marked as DOS-owned) and hooks INT 16h/21h/33h.
The hook implements the delivery rule inside the emulated PC (keys are
stuffed into the BDA ring, mouse moves go through the driver's AX=4, buttons
replace BX of AX=3).  Only `dump`/`quit` markers stop at a breakpoint
(`hook+marker_bp`); the hook has already captured VGA registers + DAC, Python
dumps A000 (chain-4) or the 4 planes and DGROUP.

### Dumps and comparison

`<name>.vga` (misc, seq 0-4, gc 0-8, crtc 0-24, attr 0-20, DAC), `.vram`
(chain-4: the CPU view of A000, else 4 planes), `.dg` (DGROUP), `.txt` (clock).

* `tools/verify/pair.py <script> <tag> [--keep] [--install FILE=NAME]` - both sides +
  vcompare + written files; `--keep` does not reset the game folders (a
  script that loads what the previous one saved: `play4load` after
  `play4`); `--install` copies a prepared file (a save of `mksave.py`)
  into both game folders after the reset (`play5`).
* `tools/verify/vcompare.py <port> <orig> [--png dir] [--dg-detail] [--clock]` - same-name
  checkpoints: registers, DAC, rendered picture, video memory, DGROUP.  DGROUP
  ignores: far pointers (heap/EMS/VGA, native in the port), relocated segment
  words (16E0 vs 1F04), RTL vectors/PSP, AMP's private ISR stack (7BA0-7BE7),
  the INT 33h flags image (E764), past DGROUP end (EA80+: stack).
* `tools/verify/frames_match.py <port frame log> <orig dumps>` - timing-
  independent: every sample of the original is looked up among the port's
  distinct states.
* `tools/verify/montage.py <dir> out.png [cols] [filter]` - contact sheet.

Game folders: work on copies (`out/verify/game_port`, `out/verify/game_dbx`),
the game writes its saves there.

### On macOS

The same commands, with `.tools/venv/bin/python`, `out/macos/Lale64` (pair.py's
default there) and Homebrew's `dosbox-x` (`$DOSBOX_X` overrides).  Differences
of the host, none of the method:

* DOSBox-X's debugger takes no absolute path in `MEMDUMPBIN` outside Windows;
  `oracle.py` passes paths relative to the repository root, the emulator's
  working directory.
* `build_hook.py` assembles `hook.S` with the system clang and links the flat
  image itself (no `ld.lld`: `link_flat` applies the object's `R_386_16`
  relocations).  Not compared byte by byte with the llvm-mingw build of the
  image; that it works is shown by the play-throughs it delivered.
* The emulator prints "Debugger in Mac OS X is not available unless you start
  DOSBox-X from a terminal" - about its interactive debugger window; the
  `mcp_server` link used here works.
* First set-up: `cp -R orig out/verify/game_port; cp -R orig out/verify/game_dbx`.

Results: [`PROGRESS.md`](PROGRESS.md), "macOS port".
