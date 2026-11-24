# Motor grubu: ALTDUZEY kütüphanesi + ISTANBUL.C main / Engine group: ALTDUZEY library + ISTANBUL.C main

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Kapsam: seg0546, seg05DC, seg09D4, seg0AC6, seg0C7F, seg0D42, seg0DB7, ovl5447, ovl5511, ovl559A,
ovl55CC, ovl5737. Adlar `engine.tsv` dosyasındadır. asm2599 yordamları, ovl569A/ovl5646 (arşiv / DOS
dosya katmanı) ve seg10EF (DSMI zamanlayıcı hizmeti) bu grubun dışındadır. Burada yalnızca motorun
onlara bağımlı olduğu yerlerde anlatılırlar.

### 0. Ghidra dökümlerindeki kaplama adresleri yanlıştır (önce bunu okuyun)

`tools/exeload.py` kaplamaları (overlay) yanlış yerleştirir. `mem[oseg*16 : ...] = code` ataması,
uzunluğu henüz `oseg*16`'dan kısa olan bir bytearray üzerinde çalışır; bu yüzden Python her kaplamayı
kendi segmentine değil, arabelleğin o anki sonuna ekler. Böylece her kaplama bir öncekinin hemen
ardına bitişik olarak yerleşir. `build/ist_flat.bin`, Ghidra projesi ve ovl2700 dışındaki tüm
`analysis/ghidra_ist/asm/ovlXXXX.asm` dökümleri (listing) kaplama kodunu
**döküm ofseti = gerçek ofset + delta** konumunda gösterir:

| ovl | 2700 | 303F | 39E5 | 3C2D | 3FD1 | 4595 | 5313 | 5447 | 5511 | 559A | 55CC | 5646 | 569A | 5737 | 5784 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| delta | 0 | −26 | −49 | −72 | −95 | −121 | −141 | −172 | −194 | −213 | −240 | −265 | −281 | −312 | −330 |

Saplama-thunk (stub thunk) hedefleri (`F_5447_052A` vb.; yani "-> callee" açıklamaları) **gerçek**
ofsetlerdir. Dökümlerde fonksiyonların ortasına düşerler. Örneğin fm_load_cmp'nin gerçek adresi
5447:052A'dır, ama döküm adresi 5447:047E'dir (`FUN_5447_047e`). Her kaplamanın ilk |delta| baytı bir
önceki kaplamanın dökümünün sonunda görünür. Görev özetinin "ovl5447 0BDE.." konumuna yerleştirdiği
VGA yordamları aslında **ovl5511:0000..00FD**'dedir; "ovl5447:0000" ise fm_load_cmp'nin kuyruğudur.
Delta uygulandığında tüm kaplama giriş önsözleri (prologue) yerine oturur (denetlenen 14 kaplamanın
hepsinde). `engine.tsv` gerçek adresleri kullanır ve farklı olduğu yerde döküm adresini açıklamada
verir. Düzeltme: exeload'da dilim atamasından önce `mem`'i `oseg*16+len(code)` uzunluğuna kadar
doldurun. Ardından düz görüntüyü ve Ghidra projesini yeniden üretin. Kaplamalar üzerinde çalışan diğer
envanter gruplarının da aynı düzeltmeye ihtiyacı vardır.

İlgili bulgu: **seg122B, 3DSYS değildir.** 122B:08EF, Borland'ın `_OvrInitEms(handle=0, first=0,
pages=0x12)` fonksiyonudur; main bunu "Overlay sistemi tamam" yazdırmadan önce çağırır. Dolayısıyla
seg122B (en azından o kısmı) VROOMM kaplama yöneticisidir.

### 1. Video

**Kip.** Oyun düz BIOS **13h kipini** kullanır (320×200, 256 renk, chain-4, A000:0000'da doğrusal).
Kip hiçbir zaman zincirsiz (unchained) hâle getirilmez: programın hiçbir yerinde Mode X, Sequencer 4.
yazmacı ya da CRTC 14h/17h yazımı yoktur; satır ikileme ayarı da değiştirilmez.
- ogr_init (5511:06F0) şunları yapar:
  - INT 10h AX=1A00h ile VGA olup olmadığını denetler; yoksa "VGA kartı bulunamadı!" ile hata verir.
  - Yüzey yuvası 0'ı ayarlar: w=320 (E29B), h=**204** (E29D). Bu yuvanın mm tanımlayıcısı E5D8
    `{far A000:0000, u32 size 0xFF00, type 0}` olur.
  - 768 baytlık iki palet ayırır: E5EF (ana) ve E5EB (çalışma/DAC kopyası).
  - ogr_int10(0x0013)'ü çağırır, ardından 7318 = 731E yapar (tam parlaklık).
- ogr_int10 (5511:0000) `mov ax,[arg]; int 10h` işlemidir. 13h (oyun), **12h** (640×480×16,
  yalnızca DEMOLAR "comp4a/comp4b" jenerik ekranları için, 3C2D gerçek ~074C/0921; orada E29B=640,
  E29D=240, boyut 0x25800) ve **03h** (çıkış, o559A_fatal'dan) ile çağrılır.
- 12h kipinde çizim, satır geri çağırma işlevi (callback) sys_rowcopy_planar (0AC6:000A) ile
  sys_blit_cb (0AC6:01C5) üzerinden yapılır. Geri çağırma işlevi 4 düzlemin (plane) her biri için
  harita maskesini `OUT 3C4h, (mask<<8)|02h` ile yazar (ogr_set_map_mask 5511:000A; maske 1,2,4,8
  sırasıyla döner) ve kenar bit maskeleriyle width/8 bayt kopyalar.

**Sayfa çevirme yok.** CRTC başlangıç adresini yazan tek yordam ogr_set_start_addr'dır (5511:0090)
ve DEMOLAR onu bir kez, 0 ile çağırır. 3DAh üzerinde bit3'ün önce 1 olmasını, sonra temizlenmesini
bekler, ardından `start = u32 arg >> 8 / & 0xFF` ile `outport(3D4h,(hi<<8)|0Ch)` ve
`outport(3D4h,(lo<<8)|0Dh)` yapar.

**Bölünmüş ekran (split screen) vardır ama kullanılmaz.** ogr_set_split_line (5511:0018) hiçbir yerden
çağrılmaz. `v = 2*n` ile şunları yapar:
- CR18 = **v mod 255** (bx=0FFh ile `div bx`; bu bir yazar hatasıdır: `& 0xFF` olmalıydı).
- CR11 bit7 temizlenir.
- CR07 bit4 = v bit8 (`(v>>4)&10h`).
- CR11 bit7 yeniden 1 yapılır.
- CR09 bit6 = v bit9 (`(v>>3)&40h`).

Bunların hepsi cli/sti arasında çalışır.

**Öznitelik denetleyicisi (attribute controller).**
- ogr_screen_on (5511:00E1): `in 3DAh; in 3BAh; out 3C0h,20h`; bu PAS'ı 1 yapar ve ekranı
  açar.
- ogr_screen_off (5511:00FD): aynı işlem, ama `out 3C0h,00h` ile; bu ekranı karartır.

**Kare sunumu.** Kareler ekran dışı mm bloklarında (çoğunlukla EMS'te) oluşturulur ve bütün olarak
VRAM olan yuva 0'a kopyalanır. 3C2D gerçek 0369'daki DEMOLAR animasyon döngüsü bu düzeni gösterir:
1. `in 3DAh` ile bit3 1 olana, sonra temizlenene kadar bekle (dikey geri izlemenin sonu).
2. mm_copy_range (09D4:07C9) ile yuva 0'a kopyala. Kaynak EMS'teyse bu, seg1154 üzerinden doğrudan
   A000'e yapılan bir INT 67h AH=57h taşımasıdır.
3. Kare hızını E74A ms saatiyle ayarla.

Fare imleci yazılımsaldır ve doğrudan VRAM'e çizilir (bölüm 2).

**Palet / DAC.** gr_set_dac (0C7F:000C) önce `out 3C6h,FFh` (PEL maskesi), sonra `out 3C8h,start`
yapar. Her parça (chunk) için 3DAh bit3'ün 1 olmasını, sonra temizlenmesini bekler, ardından
`count/chunks*3` baytlık `rep outsb 3C9h` yapar. gr_set_palette (0C7F:0050) 256 rengin hepsini tek
parçada yükler; böylece yazma, geri izleme biter bitmez başlar. Paletler 6 bitlik DAC değerleridir
(768 bayt). Solmalar (fade) şöyle çalışır:
- gr_fade (0C7F:073F) `(duration_ms, dBright, dVolume)` imzasına sahiptir.
- Her yinelemede E74A'dan `level = 7318 + dBright*t/duration` değerini hesaplar ve
  `E5EB[i] = E5EF[i]*level/731E` yapar (tamsayı bölmesi, 768 öğe).
- Her yinelemede DAC'yi yeniden yükler.
- Ses (73D0) açıksa müzik ses düzeyini de 0EF6:1625 üzerinden kademeli değiştirir (taban 731A,
  en çok 7320).
- ogr_palette_step_fade (5511:03DF) adım tabanlı bir solmadır (adım başına ±n, tik gecikmesiyle).

**Yüzeyler.** DGROUP E29B'deki tablo 8 baytlık 90 yuva tutar:
`{u16 w; u16 h; far ptr to mm handle}`. Yuva 0 ekrandır. Yuva 9 imleç bileşimi, yuva 10 (0Ah) imlecin
altında kalanı saklama alanıdır (save-under). Yuva 0x4B imleç şekillerini tutar: altı adet 15×15
şekilden oluşan 90×15'lik bir şerit. Yuva 3, 4, 5 ve 0x54 deneyx/gocuk/irem6/irem62.cmp yazı tipi
görüntülerini tutar. Blit çekirdeği sys_blit'tir (0AC6:0430):
`blit(sx,sy,w,h,dx,dy,srcSlot,dstSlot,rowfn far[,key])`. İki yuvayı da mm_ems_lock/mm_map üzerinden
eşler ve asm satır yordamlarını çağırır:
- 2599:02D0: opak kopyalama.
- 2599:0341: renk anahtarlı kopyalama (anahtar baytı ek argümandır; imleç 0 kullanır).
- 2599:03C4: aynalanmış renk anahtarlı kopyalama.
- 2599:028F: memset32.
- 2599:02AF: memcpy32.

2599:0341'in hata yolunun yalnızca ds'yi yığına (stack) ittikten sonra `pop si; pop ds` yaptığına
dikkat edin; bu yol bir gün izlenirse yığını bozar.

### 2. Zamanlayıcı ve girdi

**Zamanlayıcı.** Motor, seg10EF'teki DSMI zamanlayıcı hizmetini kullanır; bu kod bu grupta değildir.
- tsInit (10EF:007D), INT 08h'yi `INT 21h AX=3508h/2508h` ile kancalar (hook).
- 10EF:0036, PIT kanal 0'ı programlar (`out 40h` önce düşük bayt, sonra yüksek bayt).
- 10EF:0048'deki ISR önce EOI gönderir (`out 20h,20h`), sti yapar, ardından 85CE'deki 14'er baytlık
  en çok 16 yordamı dolaşır. Her yordam 16 bitlik bir kesirli biriktirici kullanır
  (`step = 0xFFFF:? / rate`) ve pushf/lcall ile çağrılır; bu yüzden her yordam iret ile biter.
  Yuva 0, 0xFFFF hızındaki (18.2 Hz) eski BIOS vektörüdür. PIT, istenen en küçük bölenle
  çalışır.
- Kayıtlı yordamlar:
  - ctl_timer_tick (0DB7:01F4), bölen **0x2E9B = 11931**, yaklaşık 100.007 Hz.
    `dword E74A += 10` yapar; bu, tüm oyun zamanlamasının okuduğu milisaniye saatidir.
  - snd_timer_100hz (0D42:0120, 0x2E9B); AMP çalarının yoklama (poll) yordamını (0EF6:0F4C) çağırır.
  - 1145:0096 (0x2E9B).
  - snd_timer_40hz (0D42:0093), bölen **0x7485** (≈40 Hz); SDI bayrakları & 0x80 koşuluna bağlıdır.
    0EAE:0414 çağrısının çevresinde EMS eşlemini kaydeder/geri yükler.

**Klavye.** ctl_install (0DB7:0026) şu vektörleri ayarlar (RTL getvect/setvect ile, 0000:12BC ve
0000:12CB):
- **INT 09h → ctl_int09_isr.** Yalnızca Ctrl+Alt+Del'i yakalar: BIOS 40:17 &0Ch ile birlikte
  `in al,60h == 53h`. Sonra 61h portu üzerinden klavyeye onay verir, EOI gönderir ve INT 1Bh yapar.
  Diğer tüm tuşlar BIOS'a zincirlenir.
- **INT 23h → ctl_int23_isr**; ölümcül hata işleyicisi üzerinden çıkar.
- **INT 00h → ctl_div0_isr.** Sıfıra bölmede **AX=EAX=FFFFh döndürür ve 3 bayt atlar**
  (3 baytlık bir `div` komutu varsayar). Bu sadakat açısından kritiktir: sıfıra bölen oyun kodu
  0xFFFF alır.
- ctrlbrk(ctl_ctrlbrk_handler); işleyici 3 kesmeye (break) izin verir.

Tarama kodu (scancode) arabelleği yoktur. Oyun tuşları BIOS/RTL `kbhit()` ve `getch()` üzerinden okur
(0000:2E45 ve 0000:29D7). ctl_flush_keys bunları boşaltır.

**Fare.** Tüm çağrılar `int86x(0x33, &E766, &E756, &E74E)` ile INT 33h üzerinden geçer:
- İşlev 0, sıfırlama: farenin var olup olmadığı 7324'e yazılır.
- İşlev 3, x, y ve düğmeleri okuma: x, 7325 kadar sağa kaydırılır. Sıfırlamadan sonra x 320 ise
  7325=1 olur; bu 640 genişlikli bir sürücü demektir.
- İşlev 4, konum ayarlama.
- İşlev 7 ve 8, yatay ve dikey aralıkları ayarlama (0..319 ve 0..199).

İmleç yalnızca yazılımsaldır. gr_cursor_update (0C7F:026B) eski konumdaki yuva 10'u geri yükler,
ekranın 15×15'lik bir bloğunu yuva 10'a kaydeder ve yuva 9'u anahtar 0 ile çizer. İmleç 7314/7316
sınırlarına göre kırpılır. 8 baytlık `{x1,y1,x2,y2}` biçiminde en çok 41 etkin bölge (hotspot)
dikdörtgeni E5F5'te durur (gr_zone_under_cursor / gr_cursor_in_zone).

### 3. Dosyalar

**CKD arşivleri.** Bu katman ovl569A'dır; Pascal çağrı kuralıyla yazılmış koddur (yığını çağrılan
temizler). Bu grupta değildir, ama davranışı burada doğrulanmıştır.
- `ckd_open(name)` (569A:0047), E86C'de 12 baytlık en çok 8 arşiv yuvası kullanır:
  `{u16 dos handle, u16 count, far name, far entries}`.
- 12 baytlık başlığı okur ve "FLiB" imzasını denetler.
- Ardından **header+8'e, yani sondaki dizin kopyasının ofsetine konumlanır (seek)**. Oyun sondaki
  kopyayı okur, satır içi başlıkları hiç okumaz. Bu tüm CKD'lerde doğrulanmıştır: hdr[8],
  filesize − count×0x41'e eşittir.
- `count` adet 0x41 baytlık "FiLE" kaydını 0x38 baytlık bellek içi girdilere okur:
  - +0: u16 tanıtıcı (handle)
  - +2: char[32] ad
  - +22h: u32 veri ofseti (rec+35h)
  - +26h: u32 boyut (rec+39h)
  - +2Ah: u32 özgün boyut (rec+3Dh)
  - +2Eh: u32 konum (0)
  - +32h: rec+31h'den alınan u32. Bu, 16 baytlık 8.3 ad alanının son 4 baytıdır ve görülen
    dosyalarda hep 0'dır.
  - +36h: u16 arşiv-içi bayrağı (1)
- `fs_init(n)` (569A:02CB), E868'de 0x38 baytlık n adet açık dosya yuvası oluşturur. main n=40 kullanır.
- `fs_open(mode, name)` (569A:0342) **tüm arşivleri yükleme sırasıyla doğrusal olarak arar**. 32
  karakteri büyük/küçük harf duyarsız karşılaştırır (a-z büyük harfe çevrilir) ve ilk eşleşme kazanır.
  Girdiyi boş bir yuvaya kopyalar. Hiçbir şey eşleşmezse gerçek bir DOS açma işlemine geri döner
  (5646:003A, yuva bayrağı +36h = 0).
- Okuma (053C) paylaşılan arşiv tanıtıcısını `offset+pos` konumuna getirir, okumayı boyutla sınırlar
  ve pos'u ilerletir. Sınırlama olduğunda `pos = size-1` yapar (sic).
- Konumlama (seek) 074E, tell 08B2, kapatma 04CA'dadır. Kapatma, paylaşılan arşiv tanıtıcısını kapatmaz.

main şu sırayla açar: 0gfl, muzik, sounds, frpos, ikon, ivir, savas, 3d.ckd, ardından adı bir yığın
arabelleğinde duran bir arşiv daha.

**Kaynak API'si (FILEMAN.L, ovl5447).**
- fm_load_cmp(memtype, name, slot): 5447:052A.
- fm_load_pal(name, first, count): 5447:08B9; dosya ofseti 0'dan count×3 baytı E5EF+first×3'e okur.
  DAC'yi **ayarlamaz**; bunu çağıran taraf gr_set_palette ya da gr_fade ile yapar.
- fm_load_file(memtype, name, mmblock): 5447:093A; ham bir dosyayı EMS üzerinden parça parça bir
  bloğa yükler. RIX tablosu bu yolla yüklenir.
- fm_filelength: 5447:0873.
- fm_change_ext: 5447:0B5F.

Bunların hepsi çalıştıkları süre boyunca mm_ems_lock(DE26)'yı tutar ve "Resim Dosyası Bulunamadı!"
ya da "Dosya açma hatası!" ile başarısız olur.

**CMP biçimi (koddan doğrulanmıştır).**
```
u16 width, u16 height,
RLE stream to EOF (length = filesize-4):
  c = byte
  c >  0x80 : run   — count = c-0x80 (1..127), next byte = value  (written with memset32)
  c <= 0x80 : literal — copy next c bytes (0 = no-op, 0x80 = 128 bytes)
decoded size = width*height, 8-bit palette indices, row-major
```
İki çözücü (decoder) vardır:
- fm_cmp_decode (5447:043B): 16 bitlik kaynak indeksi; bayt sayısını döndürür. w×h < 64K olduğunda
  kullanılır.
- fm_cmp_decode_huge (5447:0000): 32 bitlik indeksler. EMS'e yazarken pencere içi ofset 0xC000'i
  aştığında 64K'lık bir pencereyi yeniden eşler (1154:0688); geleneksel belleğe yazarken huge
  işaretçiler kullanır.

İkisinde de sıkıştırılmış dosyanın tamamı önce bir farmalloc arabelleğine okunur. Yuvanın yüzey
bloğu memtype 0 (geleneksel) ya da 1 (EMS) ile ayrılır; öteki türde bir blok zaten varsa o blok
dönüştürülür.

**EMS önbelleği.** Kaynaklar yalnızca doğrudan EMS destekli yüzey yuvalarına ya da mm bloklarına
çözülerek önbelleğe alınmış olur: "EMS statik bloklar" (303F:394A), ses örnekleri (ui_init_voices) ve
snd_ems_* kancaları (MCPSampleRealAddress/Enable/DisableVirtualSamples) üzerinden DSMI sanal
örnekleri. main EMS'i `mm_init((ems_free + 0x20.0000 + 0x10.0000)/1024 KB)` ile ayırır ve bir asgari
miktar şart koşar.

### 4. MEMMAN (seg09D4) API'si

**Tanıtıcı tablosu.** DGROUP DA48, 9 baytlık 110 girdi tutar:
`{u16 off_or_emsHandle; u16 seg; u32 size; u8 type}`; burada type 0 = geleneksel (farmalloc),
1 = EMS, FFh = boş demektir. mm_alloc bu tabloya yakın (near) bir işaretçi döndürür. Toplamlar: 6E0A
ayrılmış EMS bayt sayısıdır; DE30/DE32 ve 6E06/6E08 boş belleğin en düşük düzey (low-water)
işaretleridir. 6E02/6E04 EMS kütüphanesinin durumudur; sıfırdan farklıysa EMS etkindir.

**EMS arka ucu.** EMS kütüphanesi seg1154/seg1134'tür (bu grupta değildir):
- 1154:010D başlatma
- 0410 ayırma
- 061B serbest bırakma
- 0688 bir u32 ofsette 64K'lık pencere eşleme ve uzak (far) işaretçi döndürme
- 0963/0A0D/0AB7 INT 67h AH=57h taşımaları: geleneksel→EMS, EMS→geleneksel, EMS→EMS
- 0BAB/0BF4 sayfa eşlemini kaydetme/geri yükleme
- 0C4D boş bayt sayısı

**Kilit disiplini.** Geçerli 4 sayfalık eşleme 86F8'de (4×u16) tutulur.
- mm_ems_lock(ctx, file, line) bunu ctx'e kopyalar ve ctx.busy'yi ayarlar; iç içe kilit ölümcül
  hatadır ("Once line: %d, file: %s'dan simdi de line ...").
- mm_ems_unlock, ctx'teki eşlemeyi geri kurar.
- Genel ctx DE26 her blitter ve yükleyici tarafından kullanılır.

mm_init tuhaf biçimde önce `INT 21h AH=3Ah AL=1 BX=1` çağrısı yapar. Bu, DS:DX'te çöp değerle bir
RMDIR'dir ve büyük olasılıkla bir hata ayıklayıcı ya da bellek yöneticisi yoklamasıdır; zararsızdır,
ancak gerçek bir rmdir olarak öykünülmemelidir.

### 5. Ses yapıştırıcı kodu (glue)

**NSES.L / OSES.L.** oses_init (55CC:0000) **SES.CFG**'yi okur: char[13] sürücü .DSD dosya adı,
ardından 0x5C baytlık SDI aygıt yapılandırması (E7EC), ardından 8DDA'ya bir u16. main, 8DDA'yı
"Animasyon kalitesi" olarak yazdırır. oses_init daha sonra:
- sürücüyü DDS (0DF2) üzerinden yükler;
- MCPSampleRealAddress, MCPEnableVirtualSamples, MCPDisableVirtualSamples, GetEnvString ve SetVector
  içe aktarımlarını 0D42:0005/0041/005E/00CF/00EB'deki NSES kancalarına bağlar;
- SDIInitDevice, SDIStartOutput ve SDIStopOutput'u (8DD6, çıkışta çağrılır) çözümler;
- bölüm 2'de listelenen zamanlayıcı yordamlarını kaydeder.

Ses örnekleri (sample) DE34/DE38'deki 0x17 baytlık 49 girdili bir tabloya gider.

### 6. Tanımlanan diğer modüller

- **ovl5511 = `ALTDUZEY\OGRAPHS.L`** ("T:ALTDUZEY\OGRAPHS.L" dizgesi): VGA portları, kip ayarlama,
  fare INT 33h, palet solması.
- **ovl5447 = `ALTDUZEY\FILEMAN.L`**: FILEMAN'in yükleyici kısmı. Bellekte kalıcı (resident) FILEMAN
  kodu, PLAN'a göre seg0000'dadır.
- **ovl559A = hata/çıkış modülü (adı bilinmiyor).** o559A_fatal(file,line,msg,code):
  - code ile indislenen, 8A34'teki hata dizgesi tablosunu (uzak işaretçiler) kullanarak
    "(%d) numaralı iç hata" yazdırır;
  - 750A&4 bayrağıyla özyinelemeye karşı korunur ("Çift hata");
  - AMP ve SDI'yı durdurur, DDS'i serbest bırakır, B384'teki arşiv tanıtıcılarını kapatır, tsClose'u
    çalıştırır, kip 3'ü ayarlar, CD sesini durdurur, TRDOS.DAT yazı tipini yükler (INT 10h AX=1100h),
    mm_shutdown ve ctl_uninstall'u çalıştırır, BIOS shift bayraklarını temizler ve exit(0)'ı çağırır.
- **ovl55CC = `ALTDUZEY\OSES.L`**: ses başlatma ve ses örneği yükleme.
- **ovl5737 = CD ses modülü (MSCDEX)**: INT 2Fh AX=1500h/1501h/1510h, IOCTL 0Ah/0Bh/0Ch,
  84h PLAY ve 85h STOP aygıt istekleri. Bu modülün "port G/Ç"si yalnızca MSCDEX aygıt istekleridir.
  Özetlerdeki 0x3D4 isabeti bir port değildir.
- seg0AC6 = SYSTEMS.L (blitter'lar), seg0C7F = NGRAPHS.L (DAC, imleç, etkin bölgeler, solma),
  seg0D42 = NSES.L, seg0DB7 = NKONTROL.L, seg05DC = `CODE\INTRFACE.L` (dizgede ALTDUZEY\ değil,
  CODE\ yazar): arayüz döngüsü, iletişim kutuları, konuşma seslerini çalma.

### 7. main() akışı (0546:0001)

1. TRDOS yazı tipini yükle.
2. fs_init(40), ardından "Dosya sistemi hazır".
3. _OvrInitEms(0,0,18); −1 döndürürse EMS iletisiyle başarısız olur.
4. DDS ve zamanlayıcıyı başlat.
5. oses_init(1). SES.CFG yoksa "KUR.EXE'yi çalıştırınız" yazdırır.
6. Evet/Hayır sor.
7. Asgari miktar denetimiyle mm_init(EMS), ardından geleneksel bellek denetimi.
8. Tabloları temizle, ardından ctl_install, ctl_timer_install ve oses_init(0).
9. ui_init_tables.
10. Arşivleri aç.
11. Yazı tiplerini (CMP yuvaları 3/4/5/0x54) ve xdintro.rix'i yükle.
12. FRPOS/3D/EMS-statik başlatmayı çalıştır.
13. ogr_mouse_init, ardından ogr_init (13h kipi).
14. Giriş (3C2D:054D).
15. grphazir/grp.dat'ı yükle, ardından 303F/3C2D modülleri üzerinden ana döngü.
16. "...SON..." ve bitiş.

### Açık sorular

- SES.CFG'nin sonundaki u16 değerin (8DDA) tam anlamı ve ne zaman iki kez okunduğu.
- 0AC6 blit varyantları (0615/0C5A/0F21/10F1..1307) ile 0C7F:08FC/0A51 daha yakından incelenmelidir.
  Adları `[low]`'dur.
- 5511:0018 (bölünmüş ekran) ve düzlemsel yol ya ölü koddur ya da yalnızca jenerik ekranlarında
  kullanılır. Bir öykünücünün 12h kipine yalnızca comp4 ekranları için ihtiyacı vardır.
- 2599:0018/04AD/0500 (asm, 386): amacı belirlenmedi (ölçekli/AA blit'ler mi?).

## English

Scope: seg0546, seg05DC, seg09D4, seg0AC6, seg0C7F, seg0D42, seg0DB7, ovl5447, ovl5511, ovl559A,
ovl55CC, ovl5737. Names are in `engine.tsv`. The asm2599 routines, ovl569A/ovl5646 (archive / DOS
file layer) and seg10EF (DSMI timer service) are outside this group. They are described here only
where the engine depends on them.

### 0. Overlay addresses in the Ghidra listings are wrong (read this first)

`tools/exeload.py` places overlays incorrectly. `mem[oseg*16 : ...] = code` runs on a bytearray
that is still shorter than `oseg*16`, so Python appends each overlay at the current end of the
buffer and not at its segment. Each overlay therefore sits contiguously after the previous one.
`build/ist_flat.bin`, the Ghidra project and every `analysis/ghidra_ist/asm/ovlXXXX.asm` listing
except ovl2700 show overlay code at **listing offset = true offset + delta**:

| ovl | 2700 | 303F | 39E5 | 3C2D | 3FD1 | 4595 | 5313 | 5447 | 5511 | 559A | 55CC | 5646 | 569A | 5737 | 5784 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| delta | 0 | −26 | −49 | −72 | −95 | −121 | −141 | −172 | −194 | −213 | −240 | −265 | −281 | −312 | −330 |

The stub-thunk targets (`F_5447_052A` and so on, which are the "-> callee" annotations) are the
**true** offsets. They land mid-function in the listings. For example, fm_load_cmp has true
address 5447:052A but listing address 5447:047E (`FUN_5447_047e`). The first |delta| bytes of each
overlay show up at the end of the previous overlay's listing. The VGA routines that the task
brief places at "ovl5447 0BDE.." are really **ovl5511:0000..00FD**, and "ovl5447:0000" is the
tail of fm_load_cmp. Every overlay-entry prologue (all 14 checked overlays) lines up once the
delta is applied. `engine.tsv` uses true addresses and gives the listing address in the
description where it differs. Fix: in exeload, pad `mem` to `oseg*16+len(code)` before the slice
assignment. Then regenerate the flat image and the Ghidra project. Other inventory groups that
work on overlays need the same correction.

Related finding: **seg122B is not 3DSYS.** 122B:08EF is Borland's `_OvrInitEms(handle=0, first=0,
pages=0x12)`, which main calls before printing "Overlay sistemi tamam". So seg122B (at least that
part) is the VROOMM overlay manager.

### 1. Video

**Mode.** The game uses plain BIOS **mode 13h** (320×200, 256 colours, chain-4, linear at
A000:0000). It is never unchained: no Mode X, no Sequencer reg 4 or CRTC 14h/17h writes anywhere
in the program, and no line doubling changes.
- ogr_init (5511:06F0) does the following:
  - Checks for VGA with INT 10h AX=1A00h; on failure it fails with "VGA kartı bulunamadı!".
  - Sets surface slot 0: w=320 (E29B), h=**204** (E29D). Its mm descriptor E5D8 is
    `{far A000:0000, u32 size 0xFF00, type 0}`.
  - Allocates two 768-byte palettes: E5EF (master) and E5EB (working/DAC copy).
  - Calls ogr_int10(0x0013), then sets 7318 = 731E (full brightness).
- ogr_int10 (5511:0000) is `mov ax,[arg]; int 10h`. It is called with 13h (game), **12h**
  (640×480×16, only for the DEMOLAR "comp4a/comp4b" credit screens, 3C2D true ~074C/0921, where
  E29B=640, E29D=240, size 0x25800), and **03h** (exit, from o559A_fatal).
- Mode 12h drawing goes through sys_blit_cb (0AC6:01C5) with the row callback
  sys_rowcopy_planar (0AC6:000A). For each of the 4 planes the callback writes the map mask with
  `OUT 3C4h, (mask<<8)|02h` (ogr_set_map_mask 5511:000A; mask rotates 1,2,4,8) and copies
  width/8 bytes, with edge bit-masks.

**No page flipping.** The only CRTC start-address writer is ogr_set_start_addr (5511:0090), and
DEMOLAR calls it once, with 0. It waits on 3DAh for bit3 to be set and then clear, then does
`outport(3D4h,(hi<<8)|0Ch)` and `outport(3D4h,(lo<<8)|0Dh)` with `start = u32 arg >> 8 / & 0xFF`.

**Split screen exists but is unused.** ogr_set_split_line (5511:0018) has no callers. With
`v = 2*n` it does the following:
- CR18 = **v mod 255** (`div bx` with bx=0FFh, an author bug: it should be `& 0xFF`).
- CR11 bit7 is cleared.
- CR07 bit4 = v bit8 (`(v>>4)&10h`).
- CR11 bit7 is set again.
- CR09 bit6 = v bit9 (`(v>>3)&40h`).

All of this runs inside cli/sti.

**Attribute controller.**
- ogr_screen_on (5511:00E1): `in 3DAh; in 3BAh; out 3C0h,20h`, which sets PAS and turns the
  display on.
- ogr_screen_off (5511:00FD): the same, with `out 3C0h,00h`, which blanks the screen.

**Frame presentation.** Frames are composed in off-screen mm blocks (often in EMS) and copied
whole into slot 0, which is VRAM. The DEMOLAR animation loop at 3C2D true 0369 shows the pattern:
1. Wait `in 3DAh` until bit3 is set, then until it clears (end of vertical retrace).
2. Copy with mm_copy_range (09D4:07C9) into slot 0. For an EMS source this is an INT 67h
   AH=57h move straight into A000 through seg1154.
3. Pace the frame with the ms clock E74A.

The mouse cursor is software and is drawn directly into VRAM (section 2).

**Palette / DAC.** gr_set_dac (0C7F:000C) does `out 3C6h,FFh` (PEL mask), then `out 3C8h,start`.
For each chunk it waits for 3DAh bit3 to be set, then clear, then does
`rep outsb 3C9h` of `count/chunks*3` bytes. gr_set_palette (0C7F:0050) loads all 256 colours in
one chunk, so the write starts just after retrace ends. Palettes are 6-bit DAC values
(768 bytes). Fades work as follows:
- gr_fade (0C7F:073F) has the signature `(duration_ms, dBright, dVolume)`.
- Each iteration it computes `level = 7318 + dBright*t/duration` from E74A and sets
  `E5EB[i] = E5EF[i]*level/731E` (integer division, 768 entries).
- It reloads the DAC each iteration.
- It also ramps music volume through 0EF6:1625 (base 731A, max 7320) when sound (73D0) is on.
- ogr_palette_step_fade (5511:03DF) is a step-based fade (±n per step, with a tick delay).

**Surfaces.** The table at DGROUP E29B holds 90 slots of 8 bytes:
`{u16 w; u16 h; far ptr to mm handle}`. Slot 0 is the screen. Slot 9 is the cursor composite and
slot 10 (0Ah) is the cursor save-under. Slot 0x4B holds the cursor shapes: a 90×15 strip of six
15×15 shapes. Slots 3, 4, 5 and 0x54 hold the font images deneyx/gocuk/irem6/irem62.cmp. The blit
core is sys_blit (0AC6:0430):
`blit(sx,sy,w,h,dx,dy,srcSlot,dstSlot,rowfn far[,key])`. It maps both slots through
mm_ems_lock/mm_map and calls asm row routines:
- 2599:02D0: opaque copy.
- 2599:0341: colour-key copy (the key byte is the extra argument; the cursor uses 0).
- 2599:03C4: mirrored colour-key copy.
- 2599:028F: memset32.
- 2599:02AF: memcpy32.

Note that 2599:0341's error path does `pop si; pop ds` after pushing only ds, which corrupts the
stack if it is ever taken.

### 2. Timer and input

**Timer.** The engine uses the DSMI timer service in seg10EF; that code is not in this group.
- tsInit (10EF:007D) hooks INT 08h with `INT 21h AX=3508h/2508h`.
- 10EF:0036 programs PIT channel 0 (`out 40h` low byte then high byte).
- The ISR at 10EF:0048 sends EOI first (`out 20h,20h`), does sti, then walks up to 16 routines of
  14 bytes each at 85CE. Each routine uses a 16-bit fractional accumulator
  (`step = 0xFFFF:? / rate`) and is called as pushf/lcall, so each routine ends with iret.
  Slot 0 is the old BIOS vector at rate 0xFFFF (18.2 Hz). The PIT runs at the smallest requested
  divisor.
- Registered routines:
  - ctl_timer_tick (0DB7:01F4), divisor **0x2E9B = 11931**, about 100.007 Hz. It does
    `dword E74A += 10`, a millisecond clock that all game timing reads.
  - snd_timer_100hz (0D42:0120, 0x2E9B), which calls the AMP player poll (0EF6:0F4C).
  - 1145:0096 (0x2E9B).
  - snd_timer_40hz (0D42:0093), divisor **0x7485** (≈40 Hz), conditional on SDI flags & 0x80.
    It saves/restores the EMS map around 0EAE:0414.

**Keyboard.** ctl_install (0DB7:0026) sets these vectors (with RTL getvect/setvect, 0000:12BC and
0000:12CB):
- **INT 09h → ctl_int09_isr.** It only traps Ctrl+Alt+Del: `in al,60h == 53h` with BIOS 40:17
  &0Ch. Then it acks the keyboard through port 61h, sends EOI, and does INT 1Bh. Every other key
  chains to the BIOS.
- **INT 23h → ctl_int23_isr**, which exits through the fatal handler.
- **INT 00h → ctl_div0_isr.** On division by zero it **returns AX=EAX=FFFFh and skips 3 bytes**
  (it assumes a 3-byte `div` instruction). This is fidelity-critical: game code that divides by
  zero gets 0xFFFF.
- ctrlbrk(ctl_ctrlbrk_handler), where the handler allows 3 breaks.

There is no scancode buffer. The game reads keys through BIOS/RTL `kbhit()` and `getch()`
(0000:2E45 and 0000:29D7). ctl_flush_keys drains them.

**Mouse.** All calls go through INT 33h with `int86x(0x33, &E766, &E756, &E74E)`:
- Function 0, reset: presence goes to 7324.
- Function 3, read x, y, buttons: x is shifted right by 7325. 7325=1 when x after the reset is 320,
  which means a 640-wide driver.
- Function 4, set position.
- Functions 7 and 8, set the horizontal and vertical ranges (0..319 and 0..199).

The cursor is software only. gr_cursor_update (0C7F:026B) restores slot 10 at the old position,
saves a 15×15 block of the screen into slot 10, and draws slot 9 with key 0. The cursor is
clipped against limits 7314/7316. Up to 41 hotspot rectangles of 8 bytes `{x1,y1,x2,y2}` live at
E5F5 (gr_zone_under_cursor / gr_cursor_in_zone).

### 3. Files

**CKD archives.** This layer is ovl569A, which is Pascal-convention code (callee pops). It is not
in this group, but its behaviour is confirmed here.
- `ckd_open(name)` (569A:0047) uses up to 8 archive slots of 12 bytes at E86C:
  `{u16 dos handle, u16 count, far name, far entries}`.
- It reads the 12-byte header and checks "FLiB".
- It then **seeks to header+8, which is the offset of the trailing directory copy**. The game reads
  the trailer and never the inline headers. This is verified on all CKDs: hdr[8] equals
  filesize − count×0x41.
- It reads `count` 0x41-byte "FiLE" records into 0x38-byte in-memory entries:
  - +0: u16 handle
  - +2: char[32] name
  - +22h: u32 data offset (rec+35h)
  - +26h: u32 size (rec+39h)
  - +2Ah: u32 original size (rec+3Dh)
  - +2Eh: u32 position (0)
  - +32h: u32 from rec+31h. This is the last 4 bytes of the 16-byte 8.3 name field and is always
    0 in the files seen.
  - +36h: u16 in-archive flag (1)
- `fs_init(n)` (569A:02CB) creates n open-file slots of 0x38 bytes at E868. main uses n=40.
- `fs_open(mode, name)` (569A:0342) **linearly searches every archive in load order**. It compares
  32 chars case-insensitively (a-z upcased) and the first match wins. It copies the entry into a
  free slot. If nothing matches it falls back to a real DOS open (5646:003A, slot flag +36h = 0).
- Read (053C) seeks the shared archive handle to `offset+pos`, clamps to size, and advances pos.
  On clamp it sets `pos = size-1` (sic).
- Seek is 074E, tell 08B2, close 04CA. Close does not close the shared archive handle.

main opens, in this order: 0gfl, muzik, sounds, frpos, ikon, ivir, savas, 3d.ckd, then one more
archive named in a stack buffer.

**Resource API (FILEMAN.L, ovl5447).**
- fm_load_cmp(memtype, name, slot) at 5447:052A.
- fm_load_pal(name, first, count) at 5447:08B9: reads count×3 bytes from file offset 0 into
  E5EF+first×3. It does **not** set the DAC; the caller does that with gr_set_palette or gr_fade.
- fm_load_file(memtype, name, mmblock) at 5447:093A: loads a raw file into a block, chunked
  through EMS. The RIX table is loaded this way.
- fm_filelength at 5447:0873.
- fm_change_ext at 5447:0B5F.

All of these hold mm_ems_lock(DE26) while they run and fail through "Resim Dosyası Bulunamadı!"
or "Dosya açma hatası!".

**CMP format (verified from code).**
```
u16 width, u16 height,
RLE stream to EOF (length = filesize-4):
  c = byte
  c >  0x80 : run   — count = c-0x80 (1..127), next byte = value  (written with memset32)
  c <= 0x80 : literal — copy next c bytes (0 = no-op, 0x80 = 128 bytes)
decoded size = width*height, 8-bit palette indices, row-major
```
Two decoders exist:
- fm_cmp_decode (5447:043B): 16-bit source index, returns the byte count. Used when w×h < 64K.
- fm_cmp_decode_huge (5447:0000): 32-bit indices. Into EMS it remaps a 64K window
  (1154:0688) whenever the in-window offset exceeds 0xC000; into conventional memory it uses huge
  pointers.

In both, the whole compressed file is first read into a farmalloc buffer. The surface block for
the slot is allocated with memtype 0 (conventional) or 1 (EMS), or converted if a block of the
other type already exists.

**EMS caching.** Resources are cached simply by being decoded straight into EMS-backed surface
slots or mm blocks: "EMS statik bloklar" (303F:394A), voice samples (ui_init_voices), and DSMI
virtual samples through the snd_ems_* hooks (MCPSampleRealAddress/Enable/DisableVirtualSamples).
main allocates EMS with `mm_init((ems_free + 0x20.0000 + 0x10.0000)/1024 KB)` and requires a
minimum.

### 4. MEMMAN (seg09D4) API

**Handle table.** DGROUP DA48 holds 110 entries of 9 bytes:
`{u16 off_or_emsHandle; u16 seg; u32 size; u8 type}`, where type is 0 = conventional (farmalloc),
1 = EMS, FFh = free. mm_alloc returns a near pointer into this table. Totals: 6E0A is the EMS bytes
allocated; DE30/DE32 and 6E06/6E08 are low-water marks of free memory. 6E02/6E04 is the EMS lib
state; nonzero means EMS is active.

**EMS backend.** The EMS library is seg1154/seg1134 (not in this group):
- 1154:010D init
- 0410 alloc
- 061B free
- 0688 map a 64K window at a u32 offset and return a far ptr
- 0963/0A0D/0AB7 INT 67h AH=57h moves: conv→EMS, EMS→conv, EMS→EMS
- 0BAB/0BF4 save/restore the page map
- 0C4D free bytes

**Lock discipline.** The current 4-page mapping is kept at 86F8 (4×u16).
- mm_ems_lock(ctx, file, line) copies it into ctx and sets ctx.busy; a nested lock is fatal
  ("Once line: %d, file: %s'dan simdi de line ...").
- mm_ems_unlock remaps from ctx.
- The global ctx DE26 is used by every blitter and loader.

mm_init oddly issues `INT 21h AH=3Ah AL=1 BX=1` first. This is RMDIR with garbage DS:DX and is
probably a debugger or memory-manager probe; it is harmless but must not be emulated as a real
rmdir.

### 5. Sound glue

**NSES.L / OSES.L.** oses_init (55CC:0000) reads **SES.CFG**: char[13] driver .DSD file name,
then 0x5C bytes of SDI device config (E7EC), then u16 at 8DDA. main prints 8DDA as
"Animasyon kalitesi". oses_init then:
- loads the driver through DDS (0DF2);
- resolves the imports MCPSampleRealAddress, MCPEnableVirtualSamples, MCPDisableVirtualSamples,
  GetEnvString and SetVector to the NSES hooks at 0D42:0005/0041/005E/00CF/00EB;
- resolves SDIInitDevice, SDIStartOutput and SDIStopOutput (8DD6, which is called at exit);
- registers the timer routines listed in section 2.

Samples go into a table of 49 entries of 0x17 bytes at DE34/DE38.

### 6. Other modules identified

- **ovl5511 = `ALTDUZEY\OGRAPHS.L`** (string "T:ALTDUZEY\OGRAPHS.L"): VGA ports, mode set, mouse
  INT 33h, palette fade.
- **ovl5447 = `ALTDUZEY\FILEMAN.L`**: the loader part of FILEMAN. Resident FILEMAN code is in
  seg0000, per PLAN.
- **ovl559A = error/exit module (name unknown).** o559A_fatal(file,line,msg,code):
  - prints "(%d) numaralı iç hata" with the error-string table at 8A34 (far ptrs) indexed by code;
  - guards against recursion with flag 750A&4 ("Çift hata");
  - stops AMP and SDI, frees DDS, closes the archive handles at B384, runs tsClose, sets mode 3,
    stops CD audio, loads the TRDOS.DAT font (INT 10h AX=1100h), runs mm_shutdown and
    ctl_uninstall, clears the BIOS shift flags, and calls exit(0).
- **ovl55CC = `ALTDUZEY\OSES.L`**: sound init and sample loading.
- **ovl5737 = CD-audio module (MSCDEX)**: INT 2Fh AX=1500h/1501h/1510h, IOCTL 0Ah/0Bh/0Ch,
  device requests 84h PLAY and 85h STOP. Its "port I/O" is MSCDEX device requests only. The
  0x3D4 hit in the summaries is not a port.
- seg0AC6 = SYSTEMS.L (blitters), seg0C7F = NGRAPHS.L (DAC, cursor, hotspots, fade),
  seg0D42 = NSES.L, seg0DB7 = NKONTROL.L, seg05DC = `CODE\INTRFACE.L` (the string says CODE\,
  not ALTDUZEY\): UI loop, dialogs, voice playback.

### 7. main() sequence (0546:0001)

1. Load the TRDOS font.
2. fs_init(40), then "Dosya sistemi hazır".
3. _OvrInitEms(0,0,18), which fails with the EMS message if it returns −1.
4. DDS and timer init.
5. oses_init(1). If SES.CFG is missing it prints "KUR.EXE'yi çalıştırınız".
6. Ask Evet/Hayır.
7. mm_init(EMS) with a minimum check, then a conventional memory check.
8. Clear the tables, then ctl_install, ctl_timer_install and oses_init(0).
9. ui_init_tables.
10. Open the archives.
11. Load the fonts (CMP slots 3/4/5/0x54) and xdintro.rix.
12. Run the FRPOS/3D/EMS-static init.
13. ogr_mouse_init, then ogr_init (mode 13h).
14. Intro (3C2D:054D).
15. Load grphazir/grp.dat, then the main loop through the 303F/3C2D modules.
16. "...SON..." and the ending.

### Open questions

- The exact meaning of SES.CFG's trailing u16 (8DDA) and when it is read twice.
- 0AC6 blit variants (0615/0C5A/0F21/10F1..1307) and 0C7F:08FC/0A51 need a closer look. Names are
  `[low]`.
- 5511:0018 (split screen) and the planar path are dead or used only by the credits. An emulator
  needs mode 12h only for the comp4 screens.
- 2599:0018/04AD/0500 (asm, 386): purpose not established (scaled/AA blits?).
