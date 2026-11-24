# seg0000 – Borland C++ 3.x çalışma zamanı kütüphanesi (`_TEXT`) / seg0000 – Borland C++ 3.x runtime library (`_TEXT`)

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Grup **rtl**. Kaynak: `analysis/ghidra_ist/asm/seg0000.asm` (0000:0000–0000:5460).
`rtl.tsv` 242 girdi içerir: 236 Ghidra fonksiyonu ve yalnızca `#pragma startup` tablosu üzerinden
erişildiği için Ghidra'da fonksiyonu olmayan 6 başlangıç yordamı ("no Ghidra function" olarak işaretli).
Güven düzeyine göre: 164 high, 57 med, 21 low. low girdilerin hepsi iç yardımcılar, parçalar ya da
saplamalardır (stub). Oyuna dönük hiçbir fonksiyon low değildir.

**seg0000'ın tamamı Borland RTL'dir. Hiç `ALTDUZEY\FILEMAN.L` kodu içermez.**
`strrefs_ist.tsv`'deki iki isabet (`FUN_0000_4bb2` → 16E0:88F8 "T:ALTDUZEY\FILEMAN.L") yanlış
pozitiftir. 0x88F8 = 35064 = 4 yıldaki saat sayısıdır; `unixtodos`'un kullandığı bölendir. Gerçek
FILEMAN kodu kaplama (overlay) alanındadır. Bkz. §8.

16E0:0004'teki telif hakkı dizgesi "Borland C++ - Copyright 1991 Borland Intl." şeklindedir (BC++ 3.0/3.1,
büyük bellek modeli, uzak çağrılar, `F_`/`N_` yardımcı önekleri).

### 1. Yerleşim (bağlama sırası)

Modül sırası her dizi içinde alfabetiktir (önce asm modülleri, sonra C modülleri, sonra uzak (far)
dizge kopyaları):

| Aralık | İçerik |
|---|---|
| 0000–04F0 | c0l başlangıç/çıkış (`start`, `_cleanup`, `_checknull`, `_terminate`, `SaveVectors`, `_restorezero`, başlangıç tablosu gezgini, `abort`/ErrorExit). Gömülü veri ve kod: 0260 = DGROUP@ sözcüğü, 02FC = 8087 öykünücüsü kurulumu (başlangıç önceliği 16; INT 34h–3Eh, 75h, 02h'yi kancalar), 04B6 = öykünücüyü kaldırma (çıkış önceliği 16) |
| 04F1–1098 | FP desteği: `__ldtrunc`, `__realcvt`/`__xcvt` (printf %e/%f/%g), `_scantod`/`_scanrslt`/`_scanpop` (scanf/atof), `_fperror`, `__pow10`/`pow10`, `matherr`/`__matherr`, `sin`, `cos`, `atan2` |
| 1099–1522 | `F_FTOL`, `atexit`, `_dos_*`, exit ailesi, long/huge işaretçi yardımcıları (`F_LXMUL`, `F_LDIV`…`F_LUMOD`, kaydırmalar, `F_PADD`/`F_PSUB`/`N_PADA`/`N_PSBP`, `F_PCMP`, `F_SCOPY`, `F_SPUSH`), `getdate`/`gettime`, `getvect`/`setvect` |
| 1523–1E6B | `__IOerror`, `isatty`, `__longtoa`, `lseek`, tmpnam yardımcıları, `N_LXMUL`, `N_PCMP`, `outport*`, **`srand`/`rand`**, `_read`, `segread`, `setdate`/`settime`, `_setupio`, `stime`, `time`, `tell`, `unlink`, **`__vprinter`** (printf motoru) |
| 1E6C–3468 | conio (`__vptr`, `__vram`, `__cputn`, `_VideoInt`, `_crtinit`, `gettext`/`puttext`/`movetext`, `putch`, `getch`, `kbhit`, `textmode`, `__scroll`), `bioskey`, `calloc`, `delay`/`_initdelay`, uzak öbek (far heap) (`farmalloc`/`farfree`/`farrealloc`/`farcalloc`/`farcoreleft`, `malloc`/`realloc`, brk/sbrk), `harderr` ailesi, `int86`/`int86x`/`intdos`/`intdosx`/`intr`, `ioctl`, `_setargv`, `setblock`, `_setenvp`, `sound`/`nosound` |
| 3469–5280 | `atol`/`atoi`, `bdos`, `_chmod`, `close`/`_close`, `eof`, stdio (`fclose`, `fflush`, `flushall`, `fopen`, `fprintf`, `fread`, `fseek`, `ftell`, `fwrite`, `fgetc`, `fputc`, `puts`, `printf`, `sprintf`, `vsprintf`, `setvbuf`), `getenv`, `itoa`/`ltoa`/`ultoa`, mem*/str*, `open`/`_open`, `read`/`write`/`_write`, zaman fonksiyonları (`dostounix`, `unixtodos`, `tzset`, `__isDST`), `_xfclose`, `_xfflush` |
| 5281–5460 | dizge/bellek yordamlarının ikinci bir kopyası: `_fmemchr`, `_fmemcpy`, `_fmemset`, `_fstrcat`, `_fstrcmp`, `_fstrcpy`, `_fstrlen`, `_fstrncpy`, `_fstrstr`. Kod, yakın (near) adlı olanlarla bayt bayt aynıdır (büyük model). Oyun iki takımı da çağırır |

### 2. DGROUP (16E0) genel değişkenleri

| Ofs | Ad | Notlar |
|---|---|---|
| 0004 | telif hakkı | "Borland C++ - Copyright 1991 Borland Intl." |
| 005B–0069 | kaydedilmiş INT 0/4/5/6 vektörleri | SaveVectors / _restorezero |
| 006B–0073 | main için argc / argv / envp | _setargv, _setenvp |
| 0075, 0077, 0079, 007B, 007D | ortam boyutu, ortam segmenti, argv alanı, _psp, _version | c0 |
| 007F | `errno` | |
| 0081/0083 | başlangıçtaki BIOS tik sayısı | c0 (INT 1Ah) |
| 0087–0091 | `_heapbase`, `__brklvl` (008B/008D), `_heaptop` (008F/0091) | öbek |
| 8FA0 | `_8087` (FPU türü) | sin/cos, değer ≥ 3 olduğunda FSIN/FCOS, aksi hâlde öykünücü INT 3Eh kullanır |
| 9030 | FP hata iletisi tablosu (6 baytlık girdiler) | _fperror |
| 90D0–9142 | onun kuvvetleri | __pow10 |
| 918E / E9D2 | atexit sayacı / tablosu (32 uzak işaretçi) | |
| 9190 | `_ctype[257]` (`_ctype+1` = 9191) | atol, tzset |
| 9292 / 9296 / 929A | `_exitbuf`, `_exitfopen`, `_exitopen` (uzak işaretçiler; başlangıçta boş işlev 0000:11AF, sonra `_xfflush` 5249 / `_xfclose` 520C olarak ayarlanır) | |
| 929E | `_streams[20]`, 20 baytlık FILE: +0 level, +2 flags, +4 fd (char), +5 hold, +6 bsize, +8 buffer (uzak), +C curp (uzak), +10 istemp, +12 token (= kendi ofseti). stdin 929E, stdout 92B2, stderr 92C6, stdaux 92DA, stdprn 92EE | |
| 942E | `_nfile` = 20 | |
| 9430 | `_openfd[20]`: başlangıç değerleri 6001, 6002, 6002, A004, A002 | |
| 9458 | `_fmode` = 0x4000 (O_TEXT) | |
| 945A | `_notUmask` = 0xFFFF | |
| 945E / 9460 | `_doserrno` / `_dosErrorToSV[]` | |
| 94BA / 94BE | "TMP" / ".$$$" | tmpnam |
| 94C4 / 94C6 | **rand tohumu, 32 bitlik long (düşük/yüksek sözcük), başlangıç değeri 1** | srand/rand |
| 94C8 | `_stklen` = 0x1000 | |
| 94CA | "(null)" | NULL argümanlı printf %s |
| 9532–9543 | `_video` yapısı (pencere 9534–9537, öznitelik 9538/9539, kip 953A, satır sayısı 953B, sütun sayısı 953C, grafik 953D, kar (snow) 953E, görüntü ofseti 953F, segment 9541) ve `directvideo` (9543 = 1) | conio |
| 957E/9580 | ms başına delay() sayımı (varsayılan 0x0952; `_initdelay` bunu 0x04A9 yapabilir) | |
| 9584/9585 | ungetch bayrağı / karakteri | getch, kbhit |
| 9596/9598 | `environ` | |
| 959A | "\r" | fputc metin kipi |
| 959C/959E | stdin/stdout "setvbuf zaten çağrıldı" bayrakları | |
| 95A0 | ay başına gün tablosu; 95AC birikimli gün tablosu | zaman fonksiyonları |
| 95C6–95D2 | `tzname[2]` (→ "EST"/"EDT"), `timezone` = 18000, `daylight` = 1 | |
| 95E0 | `_realcvt` vektörü = 0000:05AE | |
| 95F0–9620 / 9620–962C | `#pragma startup` / `#pragma exit` tabloları (bkz. §3) | |
| EA52 | tmpnam statik arabelleği | |
| EA60 | SIGFPE işleyici işaretçisi (_fperror kullanır) | |
| EA64–EA6E | harderr durumu | |
| EA70 / EA72 | fgetc / fputc için tek baytlık geçici alan | |

### 3. Başlangıç ve çıkış

`start`, başlangıç tablosunu artan öncelik sırasıyla çalıştırır, `main` = **0546:0001**'i
(argc, argv, envp) ile çağırır, ardından `exit(main())` çağırır. Başlangıç tablosu (callType, prio, adres):

| Öncelik | Yordam |
|---|---|
| 1 | uzak 122B:0D2B (seg0000 dışında, tanımlanmadı) |
| 2 | 0000:1818 `_setupio` |
| 16 | 0000:02FC öykünücü kurulumu |
| 16 | 0000:21CE `_c0crtinit` |
| 16 | uzak 0000:2300 `_initdelay` |
| 16 | 0000:3252 `_setargv` |
| 16 | 0000:3393 `_setenvp` |
| 30 | uzak 0000:4DB6 `tzset` |

Çıkış tablosu: uzak 122B:0197 (öncelik 1) ve 0000:04B6 (öykünücüyü kaldırma, öncelik 16).

`__exit` (11B0) sırasıyla şunları çalıştırır: atexit fonksiyonları, ardından `_cleanup` (çıkış
tablosu), ardından `(*_exitbuf)`, `_restorezero`, `_checknull`, `(*_exitfopen)`, `(*_exitopen)` ve
`_terminate`. INT 0 (bölme hatası) 0000:0171'e kancalanır. Bu işleyici "Divide error" yazdırır ve
`_exit(3)` çağırır.

### 4. Donanım ve işletim sistemi arayüzleri

* INT 21h: 25h/35h vektör ayarlama/okuma, 2Ah–2Dh tarih/saat, 3Ch–3Fh, 40h, 41h, 42h dosya G/Ç, 43h öznitelikler,
  44h ioctl, 4Ah setblock, 4Ch çıkış, 4Eh/4Fh findfirst/findnext, 07h/0Bh konsol, 30h sürüm,
  5801h/67h/48h/49h (DOS ≥ 3.30'da c0: `_nfile` > 20 ise tanıtıcı sayısını artırır; burada 20 olduğundan atlanır).
* INT 10h: conio üzerinden `_VideoInt`. INT 16h: `bioskey`. INT 1Ah: c0 başlangıç tikleri.
* PIT: `delay` için `readtimer` (port 43h/40h, kanal 0). `sound`, kanal 2'yi ve 61h portunu programlar.
* `__vram`, `_video.snow` ayarlıysa CGA karlanmasını önlemek için 3DAh portunu bekler.
* `outport`/`outportb`/`outpw`/`outp`: doğrudan port çıkışı.
* FP: x87 kodu öykünücü kesmeleri olarak üretilir. `sin`/`cos`, FSIN/FCOS kullanmadan önce `_8087` ≥ 3
  (387) olup olmadığını denetler; değilse öykünücünün transandantal işlevini (INT 3Eh) çağırır. `F_FTOL` keser (truncate).

### 5. Oyun/motor kodundan çağrılan RTL fonksiyonları (taşıma (port) bunları sağlamalıdır)

seg0000 dışındaki çağıranlar; her dökümde `CALLF 0000:xxxx` aranarak bulunmuştur. **★ = davranışa duyarlı.**

| Adres | Fonksiyon | Çağrı | Çağıran segmentler | Notlar |
|---|---|---|---|---|
| 1779 | ★ **rand** | 65 | 05DC 2700 303F 39E5 3C2D 3FD1 4595 | **16E0:94C4 (düşük) / 16E0:94C6 (yüksek)** konumundaki 32 bitlik tohum üzerinde LCG `seed = seed*0x015A4E35 + 1`, başlangıç değeri 1. `(seed >> 16) & 0x7FFF` döndürür |
| 1768 | ★ **srand** | 1 | 0546 (main) | düşük sözcüğü argümana, yüksek sözcüğü 0'a ayarlar |
| 190E | ★ time | 1 | 0546 | 0546:0284'teki `main`, `srand((unsigned)time(NULL))` yapar; bu, satır içine açılmış `randomize()` makrosudur |
| – | ★ random(n) | satır içi | | `random()` bir makrodur: oyun `(int)(((long)rand()*n) / 32768)` ifadesini `F_LDIV` üzerinden satır içine açar (örneğin 4595:1A02 `rand()*2/0x8000` yapar). İkili dosyada `random` fonksiyonu yoktur |
| 48B4 | ★ **sprintf** | 55 | 0546 05DC 09D4 0C7F 0D42 2700 303F 39E5 3FD1 4595 5313 | `__vprinter` motoru. DGROUP dizgelerindeki biçimler: `%d %s %lu %.2d %u %5s %ld %02d %5d %03u`. FP biçimi yok. `%.2d` (tamsayıda duyarlık = 2 haneye sıfırla doldurma) ve `%5s`'e dikkat |
| 4253 | ★ printf | 34 | 0546 09D4 0AC6 303F 3FD1 5313 5447 5511 559A | metin kipindeki stdout'a yazar: "\n", "\r\n" olarak yazılır |
| 4630 | puts | 17 | 0546 5511 559A | |
| 3F58 | ★ **itoa** | 44 | 303F 39E5 3FD1 4595 | yalnızca radix = 10 olduğunda işaretlidir. Diğer tabanlarda değer işaretsiz 16 bit sayılır. Küçük harfli rakamlar |
| 3469 | ★ atol | 2 | 4595 | `_ctype` boşluklarını atlar, isteğe bağlı bir işaret alır, ardından ondalık rakamları okur. Taşma algılaması yoktur (mod 2^32 sarar) |
| 49CC / 49A3 / 48F7 / 4973 / 49EB / 4A23 / 4936 / 4A54 | strlen (52 çağrı), strcpy (27), strcat (26), strcmp (5), strncmp (2), strncpy (5), strchr (4), strrev (1) | | 05DC 09D4 0AC6 0C7F 303F 39E5 3FD1 4595 5313 5447 5737 5784 | `strlen(NULL)` 0 döndürür. `strncpy` sıfırla doldurur. `strcmp` bayt farkını döndürür (büyüklüğü değil, işareti önemlidir) |
| 537A / 530B / 53C2 / 53A3 / 53F3 / 52EC / 5281 | _fstrcpy (15), _fstrcat (17), _fstrncpy, _fstrlen, _fstrstr, _fmemset, _fmemchr | | 0AC6 303F 3C2D 4595 5447 5737 05DC | yakın sürümlerle aynı anlam |
| 4001 / 4049 / 4025 | memcpy, memset, setmem | | 1154 303F 5313 5737 0D42 569A 08D0 | yalnızca ileri yönde kopyalama (memcpy çakışan alanlarda güvenli değildir) |
| 25D6 / 24C2 / 273A / 28F7 / 2987 / 25CC / 1F64 | ★ farmalloc, farfree (14), farrealloc, farcalloc, farcoreleft, malloc, calloc | | 09D4 0E9E 1154 2700 303F 3FD1 4595 5313 5447 559A 55CC | **arena davranışı**: bloklar 4 baytlık başlıkla `(n+19)>>4` paragraftır, dolayısıyla döndürülen işaretçiler `seg:0004` biçimindedir. Gezgin (rover) işaretçiden başlayarak ilk uyan (first fit). Öbek DOS setblock ile 1K'lık adımlarla büyür. `farcoreleft`, MEMMAN (09D4:044C) tarafından havuzunu boyutlandırmak için ve FILEMAN hata ayıklama çıktısında kullanılır; bu yüzden değeri DOS bellek yerleşimine bağlıdır |
| 29D7 | getch | 25 | birçok | önce ungetch arabelleği, ardından DOS 07h (yankısız, ^C denetimi yok) |
| 2E45 | kbhit | 12 | | DOS 0Bh |
| 1F3F | bioskey | 15 | 05DC 303F 39E5 3FD1 4595 | |
| 341E | textmode | 5 | 303F 3FD1 | |
| 2321 / 33EB / 3417 | delay, sound, nosound | her biri 1 | 559A | bip sesi. delay, `_initdelay` tarafından kalibre edilir |
| 3968 / 35D9 / 3AAD / 3C95 / 3BBD / 3C3D / 3684 / 3751 / 4759 | ★ fopen, fclose, fread, fwrite, fseek, ftell, fflush, flushall, setvbuf | | 559A 55CC 5646 0546 | **metin/ikili**: 't'/'b' içermeyen bir kip `_fmode` = O_TEXT kullanır. Metin kipi okurken CR'leri ayıklar, ^Z'de durur, LF'yi CRLF olarak yazar; `ftell`/`fseek` arabellekteki '\n'ler için düzeltme yapar. Veri dosyaları "rb"/"wb" ile açılmalıdır (5646/559A'daki çağıranlar denetlenmeli) |
| 112F | _dos_findfirst | 1 | 303F | kayıtlı oyun dizininin taranması |
| 1099 | ★ F_FTOL | 10 | 08D0 2700 | sıfıra doğru kesmeyle FP → long |
| 0E92 / 0F8B | ★ sin, cos | 5 / 3 | 08D0 | x87/öykünücü sonuçları (80 bit). Bit düzeyinde birebirlik özen ister, bkz. PLAN §3.2 |
| 1242 / 12DF / 12E6 / 12EE / 12F6 / 138D / 13CE / 1477 / 14A6 / 1259 / 127A / 1502 | ★ F_LXMUL (93), F_LDIV (24, sıfıra doğru keser), F_LUDIV, F_LMOD (kalan bölünenin işaretini alır), F_LUMOD, F_LXLSH, F_LXURSH, F_PADD (111), F_PSUB, F_PCMP, F_SCOPY (27), F_SPUSH (17) | | her yerde | derleyici yardımcıları: C `long` işlemleri ve huge işaretçi aritmetiği (off 0..15 olacak biçimde normalleştirilmiş `seg:off`) |
| 118F / 119E / 12BC / 12CB | _dos_getvect/_dos_setvect (0D42 ses), getvect/setvect (0DB7 klavye) | | | ISR kurulumu |
| 2ACC | harderr | 1 | 0DB7 | INT 24h kritik hata işleyicisi |
| 2C20 / 2C51 / 2CEA / 2D18 / 2D78 | int86, int86x, intdos, intdosx, intr | | 5511 0C7F 1154 559A | |
| 1736 | outport | 1 | 5511 | |
| 10C5 | atexit | 1 | 1154 | |
| 1207 | exit | 1 | 559A (ölümcül hata yordamı F_559A_0000) | |
| 04ED | _fpu_stub_ret2 | 1 | 1351 (öykünücü) | |

Bulunan ama seg0000 dışından hiç çağrılmayan RTL yordamları (taşımanın bunları oyun API'si olarak
sağlaması gerekmez): `open`/`read`/`write`/`lseek`/`close` (yalnızca stdio içinde kullanılır), `atoi`, `ltoa`, `ultoa`,
`vsprintf`, `fprintf` (yalnızca FP hata raporlamasında kullanılır), `getenv`, `tzset`, `gettext`/`puttext`/`movetext`/`putch`,
`atan2`, `pow10`, scanf FP motoru, `getdate`/`gettime`/`setdate`/`settime`/`stime`, `unlink`,
`_chmod`, `eof`, `ioctl`, `segread`, `bdos`, `memccpy`, `stpcpy`, çağıranı olmayan `strncmp` ailesi kopyaları
(`_fmemcpy`, `_fstrcmp`), `outportb`/`outpw`/`outp`, `realloc`, `isatty`.

### 6. Sadakat açısından kritik notlar

* **RNG.** Tam olarak bir üreteç vardır: 0000:1779'daki `rand`; tohumu 16E0:94C4/94C6'dadır.
  `srand` (0000:1768) bir kez, `main` tarafından, `(unsigned)time(NULL)` ile çağrılır. Bu yüzden
  tekrarlanabilir bir taşıma, tohumun dışarıdan verilmesine izin vermelidir; özgün tohum duvar saatinden
  gelir. `time()`'ın davranışına dikkat edin: TZ ayarlı değilse `tzset` varsayılan olarak EST5EDT
  kullanır (timezone = 18000, daylight = 1) ve `dostounix` 5 saat artı bir saatlik ABD yaz saati (DST)
  ekler. `srand`'a yalnızca düşük 16 bit ulaşır.
* `random(n)`, `F_LDIV` üzerinden `((long)rand()*n)/32768` olarak satır içine açılır. `randomize()` main içinde satır içine açılmıştır.
* **printf motoru.** Yalnızca tamsayı biçimleri. `%.2d` duyarlığa kadar sıfırla doldurur. NULL ile `%s`
  "(null)" yazdırır. `%u`/`%lu` işaretsiz 16/32 bittir. `__longtoa`, `%x` için ve `itoa` içinde küçük harfli onaltılık kullanır.
* **itoa(v, buf, r).** Yalnızca r == 10 olduğunda işaretlidir. Aksi hâlde değer `unsigned int` olarak dönüştürülür.
* **Metin kipinde stdio.** §5'teki fopen/fread satırına bakın. `_fmode` = O_TEXT.
* **Öbek.** `farmalloc` `seg:0004` döndürür ve boyutları paragraf + 4'e yukarı yuvarlar. Ayırma sırası ve
  ilk uyan yeniden kullanımı, oyunun hangi adresleri alacağını belirler. Bu yalnızca oyun işaretçileri
  karşılaştırıyorsa ya da bitişikliğe dayanıyorsa önemlidir (`F_PCMP`'yi 09D4, 5313 ve 5447 kullanır).
* **conio/BIOS.** `delay()`, başlangıçta kalibre edilen bir PIT meşgul beklemesidir. `kbhit`/`getch`
  BIOS'tan değil, DOS'tan geçer.
* **FP.** `sin`/`cos`, `_8087`'ye bağlı olarak FSIN/FCOS'u ya da öykünücüyü seçer. İkisi son ulp'de
  farklılaşabilir. `F_FTOL` keser.
* c0'ın BSS temizliği 16E0:962C–EA7C aralığını kapsar. Oradaki her şey sıfırlanmış olarak başlar.

### 7. Açık sorular

* 122B:0D2B / 122B:0197, PLAN'ın 3DSYS'e atfettiği bir segmentteki uzak bir başlangıç yordamı
  (öncelik 1) ve uzak bir çıkış yordamıdır (öncelik 1). Büyük olasılıkla Borland öykünücü/FP başlatması
  (`_fpinit`) ya da kaplama yöneticisi kancalarıdır. Tanımlanmadı.
* 0000:04ED (`MOV AX,2; RETF`) öykünücüden çağrılır. Tam Borland adı bilinmiyor.
* conio iç adları (`__screenio`, `__scr_*`, `__validxy`, `__fillblank`) davranışa dayanan
  tahminlerdir.
* `_fperror`: tam Borland simge adı doğrulanmadı. Davranışı açıktır.
* seg0EF6'dan (DSMI) 0000:01F6, 0519, 0585 ve 2E37'ye görünen Ghidra "çağıranları", komut ortasına
  düşen sahte çapraz başvurulardır. Bunlar gerçek giriş noktaları değildir.

### 8. ALTDUZEY\FILEMAN.L (seg0000'da değil) – gerçekte nerede olduğu

FILEMAN doğrulama (assert) dizgeleri (16E0:8797–89C0) yığına **negatif anlık değerler** olarak itilir
(`PUSH -0x7869` = 0x8797). `strrefs_ist.tsv`'nin onları kaçırmasının nedeni budur. `68 xx xx` için
ham bayt taraması, doğrusal 0x54490–0x54E5F aralığındaki tüm itmeleri bulur. Bu bölge, **ovl5313**
etiketli bloğun sonu (5313:1294'ten itibaren) artı **ovl5447**'dir (5447:0000–0C89). 5313:1294'teki
fonksiyon blok sınırının ötesine devam eder; ovl5447 dökümünün 5447:0000'da bir komutun ortasında
başlamasının nedeni budur. Bu fonksiyonlar ovl5313/ovl5447'yi kapsayan gruba aittir. Bu bir ilk
geçiştir:

| Adres | Önerilen ad | Açıklama |
|---|---|---|
| 5313:1294 (5447:0000 / 0036 / 004A olarak devam eder; bunlar fonksiyon ortası girişlerdir) | `fm_rle_unpack_ems` | EMS hedefli RLE çözücü. Paketlenmiş bir kaynak üzerinde döner (uzak, `F_PADD` ile ilerlenir). Denetim baytı `b`: `b > 0x80` ise sonraki bayt `b-0x80` kez yinelenir (386 doldurma yordamı `F_2599_028F` üzerinden 32 bitlik `bbbbbbbb` deseni olarak yazılır). Aksi hâlde `b` adet bayt olduğu gibi (literal) kopyalanır (`F_09D4_0006`). Hedef bayrağı (+12h'deki argüman) 1 olduğunda ve çıkış ofseti artı sayı 0xC000'i geçtiğinde EMS penceresini yeniden eşler (`F_1154_0688`, uzak işaretçi döndürür; `& 0x3FFF` = sayfa ofseti). Bu NULL döndürürse (`F_PCMP` ile denetlenir): printf "EMSLock->NULL!", "compteur:%lu, k:%lu, EMSav:%lu" (`farcoreleft()` ile), "1: %d, %d, %d, %d" (16E0:86F8–86FE), `getch()`, ardından ölümcül `F_559A_0000("T:ALTDUZEY\FILEMAN.L", line 30/37/60/89, NULL, code 60/61/62)`. Açılmış uzunluğu (long) döndürür |
| 5447:038F (+03AA/03D7/043B) | `fm_rle_unpack` | aynı RLE, düz bir uzak arabelleğe; 16 bitlik sayaçlar, EMS sayfalaması yok |
| 5447:047E | `fm_load_picture` (low) | 50 baytlık bir arabellekte dosya adı oluşturur (16E0:E7B8, `_fmemset`, `strlen` ×4, `_fstrncpy`), dosyayı açar ve MEMMAN `F_09D4_0A81(dst 16E0:DE26, size, __LINE__=150, __FILE__)` üzerinden yükler. Başarısızlıkta: "Resim Dosyası Bulunamadı!", ardından ölümcül hata |
| 5447:07C7 | `fm_?` | `F_569A_074E`'yi iki kez çağırır (ovl569A). Bilinmiyor |
| 5447:080D | `fm_?` | ölümcül hata yolu, assert satırı 219 (16E0:88F8) |
| 5447:088E / 08B9 | `fm_open_file` (low) | dosya açma. "Dosya açma hatası!" → ölümcül hata (FILEMAN satır 243). Dosya ovl569A üzerinden açılır (saplama 16CD:0025 = FUN_569a_0362); hata durumunda -1 döndürür |
| 5447:093A | `fm_alloc_conv` (low) | 16E0:7468'deki uzak işaretçi ve MEMMAN `F_09D4_05F4` üzerinden çağrı yapar. "Yetersiz Konvansiyonel DOS hafızası" → ölümcül hata |
| 5447:0AB3 / 0B5F | `fm_make_path` (low) | yol/uzantı işleme: `strlen`, `farmalloc`, `_fstrcpy`, `_fstrstr(".")`, `_fstrstr("\\")`, `_fstrcat` |
| 5447:0BDE–0C6E | – | ovl5511'den çağrılan küçük INT 10h / yardımcı saplamalar. FILEMAN'e ait olmayabilir |

Ölümcül hata yordamı `F_559A_0000` (kaplama saplaması 16C1:002A) şu argümanları alır: (`__FILE__` uzak işaretçisi, `__LINE__`,
ileti uzak işaretçisi ya da NULL, hata kodu ya da -1); `exit()` ile sona erer.

## English

Group **rtl**. Source: `analysis/ghidra_ist/asm/seg0000.asm` (0000:0000–0000:5460).
`rtl.tsv` has 242 entries: the 236 Ghidra functions, plus 6 startup routines that are reached only
through the `#pragma startup` table, so Ghidra has no function for them (marked "no Ghidra function").
By confidence: 164 high, 57 med, 21 low. All of the low entries are internal helpers, fragments
or stubs. No game-facing function is low.

**All of seg0000 is Borland RTL. It holds no `ALTDUZEY\FILEMAN.L` code.**
The two `strrefs_ist.tsv` hits (`FUN_0000_4bb2` → 16E0:88F8 "T:ALTDUZEY\FILEMAN.L") are false
positives. 0x88F8 = 35064 = hours in 4 years, the divisor used by `unixtodos`. The real FILEMAN code
is in the overlay area. See §8.

The copyright string at 16E0:0004 is "Borland C++ - Copyright 1991 Borland Intl." (BC++ 3.0/3.1,
large memory model, far calls, `F_`/`N_` helper prefixes).

### 1. Layout (link order)

Module order is alphabetical within each run (the asm modules first, then the C modules, then the
far-string copies):

| Range | Contents |
|---|---|
| 0000–04F0 | c0l startup/exit (`start`, `_cleanup`, `_checknull`, `_terminate`, `SaveVectors`, `_restorezero`, startup-table walker, `abort`/ErrorExit). Embedded data and code: 0260 = DGROUP@ word, 02FC = 8087-emulator install (startup prio 16, hooks INT 34h–3Eh, 75h, 02h), 04B6 = emulator uninstall (exit prio 16) |
| 04F1–1098 | FP support: `__ldtrunc`, `__realcvt`/`__xcvt` (printf %e/%f/%g), `_scantod`/`_scanrslt`/`_scanpop` (scanf/atof), `_fperror`, `__pow10`/`pow10`, `matherr`/`__matherr`, `sin`, `cos`, `atan2` |
| 1099–1522 | `F_FTOL`, `atexit`, `_dos_*`, exit family, long/huge-pointer helpers (`F_LXMUL`, `F_LDIV`…`F_LUMOD`, shifts, `F_PADD`/`F_PSUB`/`N_PADA`/`N_PSBP`, `F_PCMP`, `F_SCOPY`, `F_SPUSH`), `getdate`/`gettime`, `getvect`/`setvect` |
| 1523–1E6B | `__IOerror`, `isatty`, `__longtoa`, `lseek`, tmpnam helpers, `N_LXMUL`, `N_PCMP`, `outport*`, **`srand`/`rand`**, `_read`, `segread`, `setdate`/`settime`, `_setupio`, `stime`, `time`, `tell`, `unlink`, **`__vprinter`** (printf engine) |
| 1E6C–3468 | conio (`__vptr`, `__vram`, `__cputn`, `_VideoInt`, `_crtinit`, `gettext`/`puttext`/`movetext`, `putch`, `getch`, `kbhit`, `textmode`, `__scroll`), `bioskey`, `calloc`, `delay`/`_initdelay`, far heap (`farmalloc`/`farfree`/`farrealloc`/`farcalloc`/`farcoreleft`, `malloc`/`realloc`, brk/sbrk), `harderr` family, `int86`/`int86x`/`intdos`/`intdosx`/`intr`, `ioctl`, `_setargv`, `setblock`, `_setenvp`, `sound`/`nosound` |
| 3469–5280 | `atol`/`atoi`, `bdos`, `_chmod`, `close`/`_close`, `eof`, stdio (`fclose`, `fflush`, `flushall`, `fopen`, `fprintf`, `fread`, `fseek`, `ftell`, `fwrite`, `fgetc`, `fputc`, `puts`, `printf`, `sprintf`, `vsprintf`, `setvbuf`), `getenv`, `itoa`/`ltoa`/`ultoa`, mem*/str*, `open`/`_open`, `read`/`write`/`_write`, time functions (`dostounix`, `unixtodos`, `tzset`, `__isDST`), `_xfclose`, `_xfflush` |
| 5281–5460 | a second copy of the string/memory routines: `_fmemchr`, `_fmemcpy`, `_fmemset`, `_fstrcat`, `_fstrcmp`, `_fstrcpy`, `_fstrlen`, `_fstrncpy`, `_fstrstr`. The code is byte-identical to the near-named ones (large model). The game calls both sets |

### 2. DGROUP (16E0) globals

| Off | Name | Notes |
|---|---|---|
| 0004 | copyright | "Borland C++ - Copyright 1991 Borland Intl." |
| 005B–0069 | saved INT 0/4/5/6 vectors | SaveVectors / _restorezero |
| 006B–0073 | argc / argv / envp for main | _setargv, _setenvp |
| 0075, 0077, 0079, 007B, 007D | env size, env seg, argv area, _psp, _version | c0 |
| 007F | `errno` | |
| 0081/0083 | BIOS tick count at start | c0 (INT 1Ah) |
| 0087–0091 | `_heapbase`, `__brklvl` (008B/008D), `_heaptop` (008F/0091) | heap |
| 8FA0 | `_8087` (FPU type) | sin/cos use FSIN/FCOS when ≥ 3, otherwise emulator INT 3Eh |
| 9030 | FP error message table (6-byte entries) | _fperror |
| 90D0–9142 | powers of ten | __pow10 |
| 918E / E9D2 | atexit count / table (32 far pointers) | |
| 9190 | `_ctype[257]` (`_ctype+1` = 9191) | atol, tzset |
| 9292 / 9296 / 929A | `_exitbuf`, `_exitfopen`, `_exitopen` (far pointers; dummy 0000:11AF at start, set to `_xfflush` 5249 / `_xfclose` 520C) | |
| 929E | `_streams[20]`, 20-byte FILE: +0 level, +2 flags, +4 fd (char), +5 hold, +6 bsize, +8 buffer (far), +C curp (far), +10 istemp, +12 token (= own offset). stdin 929E, stdout 92B2, stderr 92C6, stdaux 92DA, stdprn 92EE | |
| 942E | `_nfile` = 20 | |
| 9430 | `_openfd[20]`: init 6001, 6002, 6002, A004, A002 | |
| 9458 | `_fmode` = 0x4000 (O_TEXT) | |
| 945A | `_notUmask` = 0xFFFF | |
| 945E / 9460 | `_doserrno` / `_dosErrorToSV[]` | |
| 94BA / 94BE | "TMP" / ".$$$" | tmpnam |
| 94C4 / 94C6 | **rand seed, 32-bit long (low/high word), initial value 1** | srand/rand |
| 94C8 | `_stklen` = 0x1000 | |
| 94CA | "(null)" | printf %s with a NULL argument |
| 9532–9543 | `_video` struct (window 9534–9537, attribute 9538/9539, mode 953A, rows 953B, cols 953C, graphics 953D, snow 953E, display offset 953F, segment 9541) and `directvideo` (9543 = 1) | conio |
| 957E/9580 | delay() counts per ms (default 0x0952; `_initdelay` may set it to 0x04A9) | |
| 9584/9585 | ungetch flag / char | getch, kbhit |
| 9596/9598 | `environ` | |
| 959A | "\r" | fputc text mode |
| 959C/959E | stdin/stdout "setvbuf already called" flags | |
| 95A0 | days-per-month table; 95AC cumulative day table | time functions |
| 95C6–95D2 | `tzname[2]` (→ "EST"/"EDT"), `timezone` = 18000, `daylight` = 1 | |
| 95E0 | `_realcvt` vector = 0000:05AE | |
| 95F0–9620 / 9620–962C | `#pragma startup` / `#pragma exit` tables (see §3) | |
| EA52 | tmpnam static buffer | |
| EA60 | SIGFPE handler pointer (used by _fperror) | |
| EA64–EA6E | harderr state | |
| EA70 / EA72 | fgetc / fputc one-byte scratch | |

### 3. Startup and exit

`start` runs the startup table in ascending priority, calls `main` = **0546:0001** with
(argc, argv, envp), then calls `exit(main())`. Startup table (callType, prio, address):

| Prio | Routine |
|---|---|
| 1 | far 122B:0D2B (outside seg0000, not identified) |
| 2 | 0000:1818 `_setupio` |
| 16 | 0000:02FC emulator install |
| 16 | 0000:21CE `_c0crtinit` |
| 16 | far 0000:2300 `_initdelay` |
| 16 | 0000:3252 `_setargv` |
| 16 | 0000:3393 `_setenvp` |
| 30 | far 0000:4DB6 `tzset` |

Exit table: far 122B:0197 (prio 1) and 0000:04B6 (emulator uninstall, prio 16).

`__exit` (11B0) runs, in order: the atexit functions, then `_cleanup` (the exit table), then
`(*_exitbuf)`, `_restorezero`, `_checknull`, `(*_exitfopen)`, `(*_exitopen)` and `_terminate`.
INT 0 (divide error) is hooked to 0000:0171. That handler prints "Divide error" and calls `_exit(3)`.

### 4. Hardware and OS interfaces

* INT 21h: 25h/35h set/get vector, 2Ah–2Dh date/time, 3Ch–3Fh, 40h, 41h, 42h file I/O, 43h attributes,
  44h ioctl, 4Ah setblock, 4Ch exit, 4Eh/4Fh findfirst/findnext, 07h/0Bh console, 30h version,
  5801h/67h/48h/49h (c0 on DOS ≥ 3.30: raise the handle count when `_nfile` > 20; it is 20 here, so this is skipped).
* INT 10h: `_VideoInt`, through conio. INT 16h: `bioskey`. INT 1Ah: c0 start ticks.
* PIT: `readtimer` (port 43h/40h, channel 0) for `delay`. `sound` programs channel 2 and port 61h.
* `__vram` waits on port 3DAh for CGA snow avoidance when `_video.snow` is set.
* `outport`/`outportb`/`outpw`/`outp`: direct port output.
* FP: x87 code is emitted as emulator interrupts. `sin`/`cos` check `_8087` ≥ 3 (387) before using
  FSIN/FCOS, otherwise they call the emulator transcendental (INT 3Eh). `F_FTOL` truncates.

### 5. RTL functions called from game/engine code (the port must provide them)

Callers outside seg0000, found by scanning every listing for `CALLF 0000:xxxx`. **★ = behaviour-sensitive.**

| Addr | Function | Calls | Caller segments | Notes |
|---|---|---|---|---|
| 1779 | ★ **rand** | 65 | 05DC 2700 303F 39E5 3C2D 3FD1 4595 | LCG `seed = seed*0x015A4E35 + 1` on the 32-bit seed at **16E0:94C4 (low) / 16E0:94C6 (high)**, initial value 1. Returns `(seed >> 16) & 0x7FFF` |
| 1768 | ★ **srand** | 1 | 0546 (main) | sets the low word to the argument and the high word to 0 |
| 190E | ★ time | 1 | 0546 | `main` at 0546:0284 does `srand((unsigned)time(NULL))`, which is the `randomize()` macro inlined |
| – | ★ random(n) | inline | | `random()` is a macro: the game inlines `(int)(((long)rand()*n) / 32768)` through `F_LDIV` (for example 4595:1A02 does `rand()*2/0x8000`). No `random` function exists in the binary |
| 48B4 | ★ **sprintf** | 55 | 0546 05DC 09D4 0C7F 0D42 2700 303F 39E5 3FD1 4595 5313 | engine `__vprinter`. Formats in the DGROUP strings: `%d %s %lu %.2d %u %5s %ld %02d %5d %03u`. No FP formats. Note `%.2d` (precision on an integer = zero-pad to 2) and `%5s` |
| 4253 | ★ printf | 34 | 0546 09D4 0AC6 303F 3FD1 5313 5447 5511 559A | to stdout, which is text mode: "\n" is written as "\r\n" |
| 4630 | puts | 17 | 0546 5511 559A | |
| 3F58 | ★ **itoa** | 44 | 303F 39E5 3FD1 4595 | signed only when radix = 10. Other radixes treat the value as unsigned 16-bit. Lower-case digits |
| 3469 | ★ atol | 2 | 4595 | skips `_ctype` whitespace, takes an optional sign, then decimal digits. No overflow detection (wraps mod 2^32) |
| 49CC / 49A3 / 48F7 / 4973 / 49EB / 4A23 / 4936 / 4A54 | strlen (52 calls), strcpy (27), strcat (26), strcmp (5), strncmp (2), strncpy (5), strchr (4), strrev (1) | | 05DC 09D4 0AC6 0C7F 303F 39E5 3FD1 4595 5313 5447 5737 5784 | `strlen(NULL)` returns 0. `strncpy` zero-pads. `strcmp` returns the byte difference (sign matters, not the magnitude) |
| 537A / 530B / 53C2 / 53A3 / 53F3 / 52EC / 5281 | _fstrcpy (15), _fstrcat (17), _fstrncpy, _fstrlen, _fstrstr, _fmemset, _fmemchr | | 0AC6 303F 3C2D 4595 5447 5737 05DC | same semantics as the near versions |
| 4001 / 4049 / 4025 | memcpy, memset, setmem | | 1154 303F 5313 5737 0D42 569A 08D0 | forward copy only (memcpy is not overlap-safe) |
| 25D6 / 24C2 / 273A / 28F7 / 2987 / 25CC / 1F64 | ★ farmalloc, farfree (14), farrealloc, farcalloc, farcoreleft, malloc, calloc | | 09D4 0E9E 1154 2700 303F 3FD1 4595 5313 5447 559A 55CC | **arena behaviour**: blocks are `(n+19)>>4` paragraphs with a 4-byte header, so the returned pointers are `seg:0004`. First fit from a rover. The heap grows with DOS setblock in 1K steps. `farcoreleft` is used by MEMMAN (09D4:044C) to size its pool and in FILEMAN debug output, so its value depends on the DOS memory layout |
| 29D7 | getch | 25 | many | ungetch buffer, then DOS 07h (no echo, no ^C check) |
| 2E45 | kbhit | 12 | | DOS 0Bh |
| 1F3F | bioskey | 15 | 05DC 303F 39E5 3FD1 4595 | |
| 341E | textmode | 5 | 303F 3FD1 | |
| 2321 / 33EB / 3417 | delay, sound, nosound | 1 each | 559A | beep. delay is calibrated by `_initdelay` |
| 3968 / 35D9 / 3AAD / 3C95 / 3BBD / 3C3D / 3684 / 3751 / 4759 | ★ fopen, fclose, fread, fwrite, fseek, ftell, fflush, flushall, setvbuf | | 559A 55CC 5646 0546 | **text/binary**: a mode without 't'/'b' uses `_fmode` = O_TEXT. Text mode strips CR on read, stops at ^Z, writes LF as CRLF, and `ftell`/`fseek` correct for buffered '\n'. Data files must be opened "rb"/"wb" (the callers in 5646/559A should be checked) |
| 112F | _dos_findfirst | 1 | 303F | save-game directory scan |
| 1099 | ★ F_FTOL | 10 | 08D0 2700 | FP → long with truncation toward zero |
| 0E92 / 0F8B | ★ sin, cos | 5 / 3 | 08D0 | x87/emulator results (80-bit). Bit-exactness needs care, see PLAN §3.2 |
| 1242 / 12DF / 12E6 / 12EE / 12F6 / 138D / 13CE / 1477 / 14A6 / 1259 / 127A / 1502 | ★ F_LXMUL (93), F_LDIV (24, truncates toward zero), F_LUDIV, F_LMOD (remainder has the dividend's sign), F_LUMOD, F_LXLSH, F_LXURSH, F_PADD (111), F_PSUB, F_PCMP, F_SCOPY (27), F_SPUSH (17) | | everywhere | compiler helpers: C `long` operations and huge-pointer arithmetic (normalised `seg:off` with off 0..15) |
| 118F / 119E / 12BC / 12CB | _dos_getvect/_dos_setvect (0D42 sound), getvect/setvect (0DB7 keyboard) | | | ISR installation |
| 2ACC | harderr | 1 | 0DB7 | INT 24h critical-error handler |
| 2C20 / 2C51 / 2CEA / 2D18 / 2D78 | int86, int86x, intdos, intdosx, intr | | 5511 0C7F 1154 559A | |
| 1736 | outport | 1 | 5511 | |
| 10C5 | atexit | 1 | 1154 | |
| 1207 | exit | 1 | 559A (fatal error routine F_559A_0000) | |
| 04ED | _fpu_stub_ret2 | 1 | 1351 (emulator) | |

RTL routines that are present but never called from outside seg0000 (not needed by the port as
game API): `open`/`read`/`write`/`lseek`/`close` (only used inside stdio), `atoi`, `ltoa`, `ultoa`,
`vsprintf`, `fprintf` (used only by FP error reporting), `getenv`, `tzset`, `gettext`/`puttext`/`movetext`/`putch`,
`atan2`, `pow10`, the scanf FP engine, `getdate`/`gettime`/`setdate`/`settime`/`stime`, `unlink`,
`_chmod`, `eof`, `ioctl`, `segread`, `bdos`, `memccpy`, `stpcpy`, `strncmp`-family copies with no callers
(`_fmemcpy`, `_fstrcmp`), `outportb`/`outpw`/`outp`, `realloc`, `isatty`.

### 6. Fidelity-critical notes

* **RNG.** There is exactly one generator: `rand` at 0000:1779, with its seed at 16E0:94C4/94C6.
  `srand` (0000:1768) is called once, by `main`, with `(unsigned)time(NULL)`. So a reproducible port
  must allow the seed to be injected; the original seed comes from wall-clock time. Note how `time()`
  behaves: `tzset` defaults to EST5EDT (timezone = 18000, daylight = 1) when TZ is unset, and
  `dostounix` adds 5 h plus one hour of US DST. Only the low 16 bits reach `srand`.
* `random(n)` is inlined as `((long)rand()*n)/32768` through `F_LDIV`. `randomize()` is inlined in main.
* **printf engine.** Integer formats only. `%.2d` zero-pads to the precision. `%s` with NULL prints
  "(null)". `%u`/`%lu` are unsigned 16/32-bit. `__longtoa` uses lower-case hex for `%x` and in `itoa`.
* **itoa(v, buf, r).** Signed only when r == 10. Otherwise the value is converted as `unsigned int`.
* **Text-mode stdio.** See the fopen/fread row in §5. `_fmode` = O_TEXT.
* **Heap.** `farmalloc` returns `seg:0004` and rounds sizes up to paragraphs + 4. Allocation order and
  first-fit reuse decide which addresses the game gets. This only matters if the game compares
  pointers or relies on adjacency (`F_PCMP` is used by 09D4, 5313 and 5447).
* **conio/BIOS.** `delay()` is a PIT busy-wait calibrated at startup. `kbhit`/`getch` go through
  DOS, not BIOS.
* **FP.** `sin`/`cos` choose FSIN/FCOS or the emulator depending on `_8087`. The two can differ in
  the last ulp. `F_FTOL` truncates.
* The c0 BSS clear covers 16E0:962C–EA7C. Everything there starts zeroed.

### 7. Open questions

* 122B:0D2B / 122B:0197 are a far startup routine (prio 1) and a far exit routine (prio 1) in a
  segment the PLAN attributes to 3DSYS. They are probably the Borland emulator/FP init (`_fpinit`) or
  overlay-manager hooks. Unidentified.
* 0000:04ED (`MOV AX,2; RETF`) is called from the emulator. Its exact Borland name is unknown.
* The conio internal names (`__screenio`, `__scr_*`, `__validxy`, `__fillblank`) are guesses based
  on behaviour.
* `_fperror`: the exact Borland symbol name is not confirmed. The behaviour is clear.
* Ghidra "callers" of 0000:01F6, 0519, 0585 and 2E37 from seg0EF6 (DSMI) are bogus mid-instruction
  cross-references. These are not real entry points.

### 8. ALTDUZEY\FILEMAN.L (not in seg0000) – where it actually is

The FILEMAN assert strings (16E0:8797–89C0) are pushed as **negative immediates** (`PUSH -0x7869` =
0x8797). That is why `strrefs_ist.tsv` misses them. A raw byte scan for `68 xx xx` finds every push
in linear 0x54490–0x54E5F. That region is the end of the block labelled **ovl5313** (from 5313:1294)
plus **ovl5447** (5447:0000–0C89). The function at 5313:1294 continues across the block boundary,
which is why the ovl5447 listing starts mid-instruction at 5447:0000. These functions belong to the
group that covers ovl5313/ovl5447. This is a first pass:

| Address | Suggested name | Description |
|---|---|---|
| 5313:1294 (continues as 5447:0000 / 0036 / 004A, which are mid-function entries) | `fm_rle_unpack_ems` | RLE decoder with an EMS destination. Loops over a packed source (far, walked with `F_PADD`). Control byte `b`: if `b > 0x80`, the next byte is repeated `b-0x80` times (stored as 32-bit pattern `bbbbbbbb` through the 386 fill `F_2599_028F`). Otherwise `b` literal bytes are copied (`F_09D4_0006`). When the destination flag (arg at +12h) is 1 and the output offset plus count passes 0xC000, it re-maps the EMS window (`F_1154_0688`, returns a far pointer; `& 0x3FFF` = page offset). If that returns NULL (checked with `F_PCMP`): printf "EMSLock->NULL!", "compteur:%lu, k:%lu, EMSav:%lu" (with `farcoreleft()`), "1: %d, %d, %d, %d" (16E0:86F8–86FE), `getch()`, then fatal `F_559A_0000("T:ALTDUZEY\FILEMAN.L", line 30/37/60/89, NULL, code 60/61/62)`. Returns the unpacked length (long) |
| 5447:038F (+03AA/03D7/043B) | `fm_rle_unpack` | the same RLE into a plain far buffer, 16-bit counters, no EMS paging |
| 5447:047E | `fm_load_picture` (low) | builds a file name in a 50-byte buffer (16E0:E7B8, `_fmemset`, `strlen` ×4, `_fstrncpy`), opens and loads it through MEMMAN `F_09D4_0A81(dst 16E0:DE26, size, __LINE__=150, __FILE__)`. On failure: "Resim Dosyası Bulunamadı!" (picture file not found), then fatal |
| 5447:07C7 | `fm_?` | calls `F_569A_074E` twice (ovl569A). Unknown |
| 5447:080D | `fm_?` | fatal path, assert line 219 (16E0:88F8) |
| 5447:088E / 08B9 | `fm_open_file` (low) | file open. "Dosya açma hatası!" (file open error) → fatal (FILEMAN line 243). The file is opened through ovl569A (stub 16CD:0025 = FUN_569a_0362), which returns -1 on error |
| 5447:093A | `fm_alloc_conv` (low) | calls through the far pointer 16E0:7468 and MEMMAN `F_09D4_05F4`. "Yetersiz Konvansiyonel DOS hafızası" (insufficient conventional DOS memory) → fatal |
| 5447:0AB3 / 0B5F | `fm_make_path` (low) | path/extension handling: `strlen`, `farmalloc`, `_fstrcpy`, `_fstrstr(".")`, `_fstrstr("\\")`, `_fstrcat` |
| 5447:0BDE–0C6E | – | small INT 10h / helper stubs called from ovl5511. Possibly not FILEMAN |

The fatal-error routine `F_559A_0000` (overlay stub 16C1:002A) takes (`__FILE__` far pointer, `__LINE__`, message far pointer or NULL,
error code or -1) and ends in `exit()`.
