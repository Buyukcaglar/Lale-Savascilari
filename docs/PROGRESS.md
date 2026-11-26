# İlerleme / Progress

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

İşlevlerin durum açıklamaları: **taslak** (draft; yazıldı, henüz doğrulanmadı), **doğrulandı**
(verified; bir karşılaştırmalı testi (differential test) geçiyor ya da kâhine (oracle) karşı
denetlendi), **yerine konuldu** (replaced; platform/RTL karşılığı var, geri derlemeye
(decompilation) gerek yok).

### Kilometre taşları

| # | Kilometre taşı | Durum |
|---|---|---|
| 1 | Temel: paket açıcı (unpacker), segment modeli, Ghidra hattı (pipeline), FPU düzeltmesi, listelemeler | tamam (2026-11-20) |
| 2 | Envanter: tüm işlevler adlandırıldı ve modüller haritalandı | tamam (2026-11-21): 880 ad (`analysis/names.tsv`), modül haritası PLAN.md §2'de |
| 3 | Biçimler, Python görüntüleyicileriyle birlikte belgelendi | tamam (2026-11-23): [`FORMATS.md`](FORMATS.md) içindeki her biçim tüm oyun dosyalarına karşı denetlendi - FLiB, PAL, PUR/SND, RES, CMP, GFL, FRPOS, GRP/SAV, TRDOS, KOR, RIX, AMF, DSD (`tools/fmtcheck.py`, görüntüleyiciler, testler); yalnızca bellekteki ISOSYS savaş durumu kısmen doğrulandı |
| 4 | Platform katmanı: VGA öykünmesi (emulation), SDL3 ana programı (host), zamanlayıcı, girdi, ses | VGA: 13h/X/12h modlarının ekrana taranması (scan-out), BIOS tabloları ve DAC'ler DOSBox-X'e karşı doğrulandı; sanal CD sürücüsü + CD müziği seçeneği; SDL3 ana programı tamam (`src/platform/sdl3_main.c`: pencere + çizici (renderer), klavye/fare ile olay döngüsü, PIT saatli zamanlayıcı, `audio_sdl.c` üzerinden oyun + CD sesi) |
| 5 | Motor (ALTDUZEY) geri derlendi | arayüzün altındaki tüm motor katmanları tamam ve bütünleştirildi (2026-11-21): tek DLL'de 103/103 karşılaştırmalı test, birebir Borland uzak öbek (far heap) düzeni; GRTEXT (10/10) + INTRFACE (34/34) tamam, 17 test, ana yapıda (base) (120/123 yeşil; kalan 3'ü WP16 ile birleştirilen 3DQUICK testleri). Oyun modülü dalgası WP13–17 tamam ve bütünleştirildi (2026-11-21): IST.EXE'nin her işlevi geri derlendi; son savaş testi gelmeden önce ortak DLL 218/219; bütün oyunun doğrulanması (docs/VERIFY.md): giriş bölümü (intro), DOSBox-X'te açılıştan menüye kadar orijinalle eşleşiyor (200 örnek, 0–50 s; EMS çift eşleme düzeltmesi); betikli oynanış (scripted play-through) play3 (okuma sayısı eşitlemesi + poke/srand çapaları) her denetim noktasında orijinalle birebir eşleşiyor – menü, karakter yaratma (chargen) x4, semt, kayıt, kapı/hazine olayı, savaş başlangıcı + 12 savaş tuşu (yazmaçlar, DAC, ekran, VRAM, DGROUP; GRP.DAT ve SAKLA001.SAV bayt bayt aynı) – hikâyeye geçişteki saate bağlı solmanın içindeki tek bir denetim noktası dışında. Yol üzerinde düzeltilenler: kayıt açıklaması arabelleği, DDS sürücüsünün vfs üzerinden yüklenmesi (en düşük boş bellek işaretleri (lowwater) 6E06/DE30 artık eşleşiyor), DOSBox-X SB16 IRQ 7 (ses bekleme). play3'ün karakter yaratma solması denetim noktası artık hikâye videosunun karesine bağlandı (aynı). play4 (2026-11-21, iki tarafta da ses açık): köpeğin yapay zekâsına kaybedilen büyü savaşı, oyun sonu + yükleme, kazanılan ikinci savaş, eşya değişimi, mal/silah dükkânları (tuşlarla ve fareyle alım/satım), yiyecek tezgâhı, kamp, kayıt – 112 denetim noktasının hepsi aynı (ana program/saat alanları dışında DGROUP, ekran, DAC); SAKLA002.SAV yalnızca açıklamanın kuyruğunda ayrılıyor, orijinalde ilklendirilmemiş yığın (stack); uyarlamada (port) hata bulunmadı, yeni, saate göreli `@n` betik zamanlaması. Hekim: play5 (2026-11-22, 4. semtte hazırlanmış kayıt): hekim (reddetme, ölü değil x3, diriltme, parası yetmiyor), falcı, otel odası + kamp + otelci, kayıt, geri adım/yana adım/görüş boyutu/karakter ekranı/seçenekler/şehir haritası – 39 denetim noktası aynı. Lale64.exe (eski adı lale.exe) giriş bölümünü çalıştırıyor (açılış EMS/DAC/VGA/ekranda DOSBox-X kâhiniyle eşleşiyor; sürücü yüklemesi dosya katmanından geçtiğinden beri açılıştaki öbek zinciri (107 blok) ve DGROUP aynı; hâlâ farklı olanlar AMP'nin özel kesme yığını, yerel (native) yordam işaretçileri ve DSMI saat evresi – hiçbirini oyun kodu okumuyor; 2026-11-22'de denetlendi). Daha önce: altyapı tamam (2026-11-20): DGROUP modeli `dg[]`, öykünmeli PC `pc.h`, Unicorn karşılaştırmalı test düzeneği, SDL3 ana program döngüsü; 1. dalga (EMS/MEMMAN/NKONTROL, DSMI DDS/zamanlayıcı, 386 blok kopyalayıcıları (blitter) + 3D dörtgenler, dosya katmanı + Borland stdio, AMP oynatıcı, MCP karıştırıcı) tamam |
| 6 | Oyun modülleri geri derlendi | tamam (2026-11-21): main, DEMOLAR, CHARGEN, FRPOS, 3DQUICK, 3DSYS, ISOSYS, GFL – bkz. "Geri derlenmiş modüller" |
| 7 | DSMI ses sistemi geri derlendi | tamam: oynatıcı, yükleyici, CDI, MCP karıştırıcı (doğrulandı); ses arka ucu ana programa bağlandı (NSES yerel MCP'yi SDISB16.DSD olarak kaydeder; `audio.c` / `audio_sdl.c`) |
| 8 | DOSBox-X'e karşı bütün oyunun doğrulanması | play3, play4, play4load her denetim noktasında aynı (docs/VERIFY.md). Karar (kullanıcı, 2026-11-22): kayıt açıklamasının NUL'dan sonraki kuyruğu uyarlamada sıfır kalır (orijinalde ilklendirilmemiş yığın, hiç okunmaz). Boşluk kapatma dalgaları (gap waves) 2026-11-22/23 (aşağıda): hekim, ISOSYS Ctrl-Break / ses açık / kapı duvarı, 3DQUICK 100x100, CD sesi, play5..play10, playq, playx, DOSBox-X'in kendi içinde Ctrl-Break ve Ctrl+Alt+Del. Her DEMOLAR yordamı artık orijinale karşı bir oynanışta çalışıyor |
| 9 | macOS derlemesi | tamam (2026-11-23): Apple Silicon üzerinde `Lale64` + `Lale64.app`, evrensel (universal) sürüm paketi; karşılaştırmalı test takımı ve DOSBox-X'e karşı betikli oynanışlar Mac'te çalışıyor – bkz. aşağıdaki "macOS uyarlaması" ve [`MACOS.md`](MACOS.md) |

### İşlev durumu

`python tools/funcstatus.py` tarafından `src/` içindeki `Original:` / `Verified:` yorumlarından
üretildi (2026-11-23; `--list` her yordamı adıyla listeler). Yalnızca kendi `Original:` satırı
olan yordamlar sayılır; bu yüzden bir modülün sayısı "Geri derlenmiş modüller" bölümündeki
sayıdan düşük olabilir (birlikte belgelenen küçük yardımcılar).

| Segment | Kaynaklar | İşlevler | doğrulandı | taslak | yerine konuldu | belirtilmemiş |
|---|---|---|---|---|---|---|
| 0000 | bc_farheap, bc_stdio, ngraphs, speaker | 65 | 61 | 4 | 0 | 0 |
| 0546 | istanbul | 1 | 1 | 0 | 0 | 0 |
| 05DC | intrface | 32 | 32 | 0 | 0 | 0 |
| 08D0 | q3d | 8 | 8 | 0 | 0 | 0 |
| 09D4 | memman | 21 | 21 | 0 | 0 | 0 |
| 0AC6 | systems | 18 | 18 | 0 | 0 | 0 |
| 0C7F | ngraphs | 15 | 15 | 0 | 0 | 0 |
| 0D42 | nses | 11 | 9 | 2 | 0 | 0 |
| 0DB7 | nkontrol | 11 | 8 | 3 | 0 | 0 |
| 0DDF | dsmistr | 9 | 9 | 0 | 0 | 0 |
| 0DF2 | dds | 11 | 11 | 0 | 0 | 0 |
| 0E9E | memalloc | 4 | 3 | 1 | 0 | 0 |
| 0EAE | cdi | 28 | 28 | 0 | 0 | 0 |
| 0EF6 | amp | 32 | 32 | 0 | 0 | 0 |
| 105B | amfload | 3 | 3 | 0 | 0 | 0 |
| 10EF | timer | 9 | 9 | 0 | 0 | 0 |
| 111B | amfload | 1 | 1 | 0 | 0 | 0 |
| 1134 | ems | 9 | 9 | 0 | 0 | 0 |
| 1145 | dsmitime | 6 | 6 | 0 | 0 | 0 |
| 1154 | emsheap | 17 | 16 | 1 | 0 | 0 |
| 122B | istanbul | 1 | 1 | 0 | 0 | 0 |
| 2599 | blit386 | 11 | 11 | 0 | 0 | 0 |
| 2700 | q3dview, q3dview_anim, q3dview_map | 28 | 28 | 0 | 0 | 0 |
| 303F | s3d_camp, s3d_charscr, s3d_district, s3d_keys, s3d_loop, s3d_menu, s3d_move, s3d_panel, s3d_save | 30 | 30 | 0 | 0 | 0 |
| 39E5 | chargen | 12 | 12 | 0 | 0 | 0 |
| 3C2D | demolar | 13 | 13 | 0 | 0 | 0 |
| 3FD1 | frpos, frpos_shop, frpos_util | 25 | 25 | 0 | 0 | 0 |
| 4595 | iso_ai, iso_battle, iso_char, iso_combat, iso_draw, iso_field, iso_map, iso_spell, iso_turn, iso_util | 69 | 69 | 0 | 0 | 0 |
| 5313 | gfl | 12 | 12 | 0 | 0 | 0 |
| 5447 | fileman | 7 | 7 | 0 | 0 | 0 |
| 5511 | ographs | 13 | 13 | 0 | 0 | 0 |
| 559A | fatal | 4 | 3 | 1 | 0 | 0 |
| 55CC | oses | 2 | 2 | 0 | 0 | 0 |
| 5646 | vfile | 9 | 9 | 0 | 0 | 0 |
| 569A | vfs | 13 | 13 | 0 | 0 | 0 |
| 5737 | cd5737 | 15 | 15 | 0 | 0 | 0 |
| 5784 | grtext | 10 | 10 | 0 | 0 | 0 |
| **tümü** | | **585** | **573** | **12** | **0** | **0** |

12 taslak şunlardır: `speaker.c` içindeki PC hoparlörü yordamları (`bc_readtimer`, `bc_delay`,
`bc_sound`, `bc_nosound`: tek kullanıcıları olan `o559A_beep` hiçbir yerden başvurulmuyor), NSES
getenv/setvect kancaları (ana programın ortamı, IVT yok), `ctl_div0_isr`, `ctl_timer_install`,
`ctl_dds_timer_init` (bir kesme işleyicisi ve DSMI'ye iki ince çağrı; inceleme yoluyla),
`MemRealloc` (hiç çağrılmıyor), `emsGetPageMapRaw` ve `o559A_beep` (başvurulmuyor).

### Geri derlenmiş modüller

| Modül | İşlevler | Durum |
|---|---|---|
| ovl5737 CD sesi (`src/game/cd5737.c`) | 15/15 | doğrulandı (2026-11-22, `tests/py/test_cd5737.py`, 16 test: her aygıt isteği, dönüş değeri ve DGROUP yazımı, bir MSCDEX modeli üzerinde orijinal koda karşı; 466/468 komut çalıştırıldı, kalan 2'sine ulaşılamaz). Düzeltilenler: MSCDEX yokken / yanlış sürücüde / yalnızca ses içeren diskte `vtoc_ok`, `play_track` içinde yığının yeniden kullanımı, `read_cfg` (içi boş bir taslaktı (stub)); `cdrom.c` artık CD olmayan bir sürücü için isteğe dokunmuyor ve alt birimi (subunit) dolduruyor, MSCDEX'in yaptığı gibi. Karşılaştırılmayan: MSCDEX olmadan `drive_info` (orijinalde ilklendirilmemiş uzak (far) işaretçi, çağıranı yok) |
| Borland `rand/srand` (`src/rtl/bc_rand.c`) | 2/2 | doğrulandı (karşılaştırmalı, 4 tohum × 2000 adım) |
| FILEMAN `fm_cmp_decode` (`src/engine/fileman.c`) | 1/7 | doğrulandı (karşılaştırmalı, oyunun CMP dosyaları + rastgele veri) |
| DSMI DDS kayıt defteri seg0DF2 (`src/dsmi/dds.c`) | 11/11 | doğrulandı (karşılaştırmalı); DDSLoadDriver .DSD adlarını yerel sürücülere eşler |
| DSMI dizgi yardımcıları seg0DDF (`src/dsmi/dsmistr.c`) | 9/9 | doğrulandı (karşılaştırmalı, tuhaflıklar korundu) |
| DSMI Zamanlayıcı Hizmeti seg10EF (`src/dsmi/timer.c`) | 9/9 | doğrulandı (karşılaştırmalı: çağrı sırası, yuva tablosu, PIT yazımları) |
| DSMI zaman tabanı seg1145 (`src/dsmi/dsmitime.c`) | 6/6 | doğrulandı (karşılaştırmalı) |
| DSMI ham EMS seg1134 (`src/dsmi/ems.c`) | 9/9 | doğrulandı (karşılaştırmalı) |
| DSMI EMS öbeği seg1154 (`src/dsmi/emsheap.c`) | 17/17 | başvurulmayan emsGetPageMapRaw dışında doğrulandı (blok yerleşimi bayt bayt aynı) |
| DSMI MemAlloc seg0E9E (`src/dsmi/memalloc.c`) | 4/4 | dönüş kodları doğrulandı |
| MEMMAN seg09D4 (`src/engine/memman.c`) | 22/22 | doğrulandı (karşılaştırmalı); birebir aktarılmış Borland uzak öbeği üzerinde (`far_malloc`/`far_free`, `mm_coreleft` → `bc_farcoreleft`); en düşük boş bellek işareti (low-water mark) 6E06 orijinalle eşleşiyor (5. kilometre taşı) |
| NKONTROL seg0DB7 (`src/engine/nkontrol.c`) | 11/11 | tik/bayraklar doğrulandı; INT 09h kancası ve INT 23h işleyicisi DOSBox-X'e karşı (playbrk, playbrk3d, playcad); div-0 ISR ve iki DSMI zamanlayıcı çağrısı inceleme yoluyla |
| Borland stdio/io (`src/rtl/bc_stdio.c`) | 40 yordam | doğrulandı (karşılaştırmalı, metin kipi dahil) |
| stdio tanıtıcı (handle) katmanı ovl5646 (`src/engine/vfile.c`) | 9/9 | doğrulandı (karşılaştırmalı) |
| FLiB sanal dosyaları ovl569A (`src/engine/vfs.c`) | 13/13 | doğrulandı (karşılaştırmalı, 8 arşivin hepsi) |
| 386 blok kopyalayıcıları asm2599 (`src/engine/blit386.c`) | 11/11 | doğrulandı (karşılaştırmalı, hedefin tamamı) |
| DSMI CDI seg0EAE + AMP 2.00 oynatıcı seg0EF6 + AMF yükleyici seg105B/111B (`src/dsmi/cdi.c, amp.c, amfload.c`) | 100/100 | doğrulandı (oyunun 15 AMF'sinin hepsi: 2,500 tik boyunca aynı CDI çağrı akışları) |
| SDISB16.DSD'deki DSMI MCP v3.5 yazılım karıştırıcısı (`src/dsmi/mcp.c`) | tüm karıştırıcı + 26 CDI yuvası + SDI kabuğu | Unicorn'daki sürücüye karşı bit bit aynı olarak doğrulandı (DMA halkası); 22050 Hz stereo s16 |
| NSES seg0D42 + OSES ovl55CC (`src/engine/nses.c, oses.c`) | 13/13 | getenv/setvect kancaları dışında doğrulandı; yerel MCP, SDISB16.DSD olarak kayıtlı |
| Ses arka ucu (`src/platform/audio.c, audio_sdl.c`) | – | PIT ile saatlenen öykünmeli SB16 DMA; oyun + CD sesi tek bir SDL akışında; ana programa bağlandı |
| FILEMAN'ın geri kalanı ovl5447 (`src/engine/fileman.c`) | 7/7 | doğrulandı (gerçek CKD üyeleri, >64 KB resimler, EMS yeniden eşleme noktaları) |
| Ölümcül hata/çeşitli ovl559A (`src/engine/fatal.c`) | 4/4 | 3'ü doğrulandı; beep'e hiçbir yerden başvurulmuyor |
| Borland uzak öbeği (`src/rtl/bc_farheap.c`) | 10 yordam | doğrulandı: seg:off, farcoreleft ve alan (arena) baytları, main() anlık görüntüsünden (snapshot) orijinale karşı; MEMMAN, DSMI MemAlloc/EMS öbeği, FILEMAN, dosya katmanı ve oyun modüllerinin hepsi bellek ayırmayı bunun üzerinden yapıyor |
| GFL/GHD video oynatıcısı ovl5313 (`src/game/gfl.c`) | 12/12 | doğrulandı (çözücü 35 videonun hepsinde tools/gflview.py'ye karşı; oynatıcı orijinale karşı çalıştırıldı: DGROUP/EMS/ekran/palet/CDI) |
| SYSTEMS seg0AC6 + NGRAPHS seg0C7F + OGRAPHS ovl5511 (`src/engine/systems.c, ngraphs.c, ographs.c`) | 52/52 | doğrulandı (23 test: DGROUP, A000, VGA yazmaçları/DAC/düzlemler, EMS); solma ara değerlemesi yalnızca ms=0'da |
| main seg0546 + DEMOLAR ovl3C2D (`src/game/istanbul.c`, `demolar.c`) | 14/14 | doğrulandı (6 test: beklemeler, resim gösterme/solma, bölge çerçevesi, müziği kapatma; DOSBox-X'e karşı: açılış ve giriş bölümü, menünün her girdisi, hikâye slayt gösterisi (play8), oyun sonu (play4), final (play9), jenerik (playx, play9; arka plan yuvası 2026-11-23'te düzeltildi), şehir haritasıyla yolculuk (play10), yeni oyun girişi (play3, playq)) |
| CHARGEN ovl39E5 (`src/game/chargen.c`) | 12/12 | doğrulandı (8 test: özellik zarları, çizim, GRP.DAT yükleme/kaydetme, iptal, tuşlarla ve fareyle 4 kişilik tam akış, animasyonlar) |
| ISOSYS savaşları ovl4595 (`src/game/iso*.c`) | 77/77 | doğrulandı (32 test: kurallar, yapay zekâ ile 96 tohumlanmış tur, her vecize, 18 haritanın hepsinin savaş alanı, INT 0 işleyicisinin anlamı üzerinden sıfıra bölme, gerçek SAVAS.CKD grafikleriyle 8 tam betikli savaş); 2026-11-22 (`test_iso_d.py`, 12 test): Ctrl-Break (orijinal, INT 23h işleyicisi üzerinden bir sonraki kbhit()'te sona erer; uyarlama savaşa devam ediyordu – düzeltildi), ses açık yolları (tek çağrılarda ve üç tam savaşta aynı CDI/AMP çağrı sırası ve durumu), çubuk resmi olmayan kapı duvarı (oyunla gelen 18 haritada ortaya çıkamaz; zorlandığında orijinal 10h iç hatasıyla sona erer, uyarlama da öyle – çöp blit yok, çökme yok) |
| FRPOS dükkân/olay betikleri ovl3FD1 (`src/game/frpos*.c`) | 33/33 | doğrulandı (5 test: 18 PCSn dosyasının hepsinin her olayı/tetikleyicisi, 33/33 işlem kodu; tools/frposdis.py) – kendi DLL'inde, dalgadan sonra ana yapıya katılıyor |
| 3DSYS keşif ovl303F (`src/game/s3d*.c`) | 32/32 | doğrulandı (karakter ekranı, döngü, menüler, panel, kamp, hareket, tuşlar, orijinalin SAKLA001.SAV dosyası dahil kayıt yazma + okuma, semt yükleme/girme/konum, tüm duvar türleri); 2026-11-22 (`test_s3d_gap.py`): portre/bölge yordamı 303F:27F7 (44 durum) ve hiçbir kodun başvurmadığı iki yordam – hata ayıklama arabellekleri 303F:23ED, harita yazıcı 303F:189D (yazılan baytlar aynı) – doğrulandı |
| 3DQUICK birinci şahıs çizicisi ovl2700 (`src/game/q3dview*.c`) | 36/36 | doğrulandı (15 test: ~340 görüş boyutunda açılış tabloları, 18 haritanın hepsi x 4 yön, otomatik harita, animasyonlar); 2026-11-22 (`test_q3dview_gap.py`, 5 test): oyunun yapılandırmasında (100 pencere, hız 9) dönüşler A000/yüzeyler/DGROUP'un tamamında aynı; 100x100 çizim zemin/tavan şeritlerine kadar aynı, orada orijinal (ve uyarlama) şerit blitter'ında sonsuza dek döner – oyun hiçbir zaman 100x100'de çizmez; "742 bayt" farkı, uyarlamada `q3d_draw_quad_mapped` içindeki 64 KB ofset sarmasının eksik olmasından kaynaklanıyordu (yalnızca göz ardı edilmiş bir ölümcül hatadan sonra) – `q3d.c` içinde düzeltildi (`far_at`) |
| 3D dörtgenler seg08D0 (`src/game/q3d.c`) | 7/7 | doğrulandı (karşılaştırmalı, bit bit aynı x87 modeli); EMS kilit sarmalayıcısı inceleme yoluyla |

### Boşluk dalgası (2026-11-22)

Yukarıdaki boşluklar kapatılırken bulunan motor ve ana program düzeltmeleri:

| Yer | Sorun | Düzeltme |
|---|---|---|
| `src/engine/intrface.h` | `ui_wait_voice` sessizlik örneğini DGROUP:DE1E'den alıyordu; orijinal DF1Eh'yi yığına iter (05DC:1E3C: ses yuvası 0Ah, bosch.pur) | sabit düzeltildi; `test_iso_d.test_sound_silence_sample` |
| `src/engine/systems.c` | hedef satırı geleneksel bellek bloğunun üstünde kalan `sys_blit` / `sys_blit_recolor` (bir savaş metin kutusunun büyük baş harfi, y = -1 / -2) çöküyordu; orijinalin huge işaretçisi bloğun hemen altına sarar | parça, orijinalin 1 MB sarmasının yerleştirdiği yere yerleştirilir (320 genişlikli bir yüzey için: negatif satırlar bloğun altına düşer ve atılır, geri kalanı 0. satırdan itibaren çizilir; diğer genişliklerde parçanın tamamı bloğu ıskalar); `test_iso_d.test_textbox_capital`, `test_q3dview_gap.test_render_100` (`sys_blit`; `sys_blit_recolor` aynı akıl yürütmeyle, ona ulaşan test yok) |
| `src/game/q3d.c` | dörtgen çizicide eksik 64 KB ofset sarması (yalnızca göz ardı edilmiş bir ölümcül hatadan sonra) | `far_at` |
| `src/platform/cdrom.c` | CD olmayan bir sürücüye yapılan isteğe bir durum yazılıyordu; MSCDEX başlığa dokunmaz; alt birim doldurulmuyordu | MSCDEX'teki gibi |
| `src/platform/pc.c`, `nkontrol.c`, `sdl3_main.c` | Ctrl-Break ana program tarafından üretilemiyordu | Ctrl+Pause / Ctrl+ScrollLock = BIOS Ctrl-Break (arabellek boşaltılır, 0000h sözcüğü, bir sonraki kbhit()'te INT 23h); oyunun INT 09h kancasına her basma kodu (make code) sunulur. Yazılan bir Ctrl+C sıradan bir tuş olarak kalır, tıpkı DOSBox-X'teki orijinalin ana menüde onu aldığı gibi (MS-DOS'un kendisi orada kesebilir – burada doğrulanamaz) |
| `src/platform/sdl3_main.c` | bir tuşun bırakılması basılması gibi işleniyordu: Esc, Enter, Tab, imleç ve işlev tuşları etkileşimli oyunda iki kez itiliyordu (betikler pencereyi atlar) | bırakmalar yalnızca shift bayraklarını günceller |

Dalganın son durumu (2026-11-23, cfcacce commit'inin derlemesi): karşılaştırmalı test takımı
256/256 (out/base); play4 112/112 denetim noktası, play4load ve play5 (39/39), kayıt
açıklamasının kuyruğu dışında (sıfır olarak tutulur, bkz. 8. kilometre taşı) DOSBox-X'teki
orijinalle aynı. play4 ve play4load son `sys_blit` düzeltmesinden önceki derlemeyle, play5 ise
ondan sonraki derlemeyle çalıştırıldı.

İkinci tur (2026-11-23):

* **play6** (4. semtin hikâye bayrakları 29, 97, 30, 98, 31, 99 ile hazırlanmış kayıt): hekimin
  "kim" istemine sağ tıklama, 99 numaralı bayrağın açtığı kapının arkasındaki yiyecek tezgâhı
  (reddetme, yeme, parası yetmiyor), seçenekler listesinin her girdisinin fareyle değiştirilip
  tuşlarla geri alınması, iki yönde de başarılı yana adımlar; 46 denetim noktası aynı
  (docs/VERIFY.md).
* **Ctrl-C / Ctrl-S**: Microsoft'un yayımladığı MS-DOS kaynak kodundan (v2.0 ikili, v4.0 kaynak)
  kesinleştirildi: INT 21h AH=0Bh (kbhit()) bekleyen bir ^C'yi okur ve INT 23h çağırır, ^S'yi
  ve ondan sonraki karakteri okur; AH=07h (getch()) hiç denetlemez. Uyarlama MS-DOS'u izler
  (`src/platform/pc.c`); DOSBox-X'in DOS'u karaktere bakmaz, bu yüzden orijinal orada yalnızca
  Ctrl-Break ile kesilir; bu, referanstan bilinçli olarak ayrılan tek noktadır.
  ^P / ^N (yazıcı yankısı) modellenmedi.
* **Tuş bırakma düzeltmesi** canlı pencerede doğrulandı: Lale64.exe'ye gönderilen tuş
  iletileriyle her Esc için bir giriş ekranı, her Aşağı/Yukarı için bir menü girdisi.

### Boşluk dalgası 3 (2026-11-23)

İkinci turun açık bıraktığı liste kapatıldı (ayrıntılar: docs/VERIFY.md "Gap wave 3"):

* **4. semtin kapalı kapıları, silah ve mal dükkânları, fareyle üzerine gelip Enter ile
  seçenekler**: play7, 53 denetim noktası aynı.
* **Hikâye slayt gösterisi** (play8, 14), **final** (play9, 21), **menüden jenerik**
  (playx, 13), **hızlı başlangıç** (playq), **şehir haritasıyla yolculuk** (play10, 16): kayıt
  açıklamasının kuyruğu (önceki gibi) ve iki tur denetim noktasındaki RNG durumu dışında aynı
  (bir portre göz kırpma zarı, bir varıştan sonraki saat evresi; ekran ve DGROUP'un geri kalanı
  eşit).
* **DOSBox-X içinde Ctrl-Break ve Ctrl+Alt+Del**: yeni betik olayı `kbd` (8042 komutu D2h
  üzerinden ham tarama kodları) ve iki tarafta bir `int23` dökümü; ana menüde (playbrk) ve bir
  semtte (playbrk3d, playcad) aynı.
* **MSCDEX olmadan `o5737_cd_drive_info`**: yapısı gereği karşılaştırılamaz; INT 2Fh 1501h hiçbir
  şey yazmadığında orijinal, ilklendirilmemiş bir yığın uzak işaretçisini okur; IST.EXE'de
  çağıranı yok. `cd5737.c` içinde belgelendi.
* **4.0'dan sonraki MS-DOS**: Ralf Brown's Interrupt List (her DOS sürümü için başvuru kaynağı,
  delorie.com yansısı) INT 21h AH=0Bh için "DOS 1+ - ^C/^Break are checked, and INT 23
  is called if either pressed" (^C/^Break denetlenir, biri basılmışsa INT 23 çağrılır), AH=07h
  için "does not check ^C/^Break" (^C/^Break denetlenmez) der ve sürüme özgü bir istisna
  vermez; uyarlamanın modellediği de budur.

Yolda düzeltilenler:

| Yer | Sorun | Düzeltme |
|---|---|---|
| `src/game/demolar.c` | `demo_credits_exit` ekrana 0Bh (grup.cmp) yerine 9. yüzeyi kopyalıyordu: jenerik, kayan pencerenin çevresinde bir önceki ekranı gösteriyordu | 0Bh yuvası, orijinaldeki gibi (3C2D:222C) |
| `src/engine/nkontrol.c`, `src/platform/pc.c` | INT 09h kancası Ctrl+Alt+Del'de INT 23h işleyicisini doğrudan çağırıyordu; orijinal INT 1Bh çağırır, böylece DOS bir sonraki `kbhit()`'te keser | `pc_int1b()`; `int23` dökümü için INT 23h gözlemcisi |
| `tools/verify/vcompare.py` | EMS sayfa eşlemi saat alanları (86F8, E7AC) bir sözcük kısaydı | 4 sözcük |
| `tools/ghidra_run.sh`, `FixSwitch.java` | RTL'nin satır içi switch tablosu 0000:0B08 çözülemiyordu; fpupatch.py bir önceki switch listesi üzerinde çalışıyordu | C biçimi (satır içi tablolar); switch listesi değiştiğinde hat ikinci kez çalışır: 46 tablo, 0 çözülemeyen |

### Gerçek CD, iki baskı, DATA / SAVE / VIDEO (2026-11-24)

Kullanıcının oyun CD'sinden aldığı redumper dökümü (dump) geldi (`_references/cd`). Dökümün neyi
kesinleştirdiği ve üzerine neler kurulduğu:

* **Disk** ([`DISC.md`](DISC.md)): 1. ve 2. izler redump.org 91232 ile eşleşir; veri izi 48
  örnek erken basılmıştır, içe aktarıcı bunu onarır; 3. izin son 48 örneği eksiktir. Veri
  dosyaları 2005 sürümününkilere eşittir. Benzetilmiş kalıbın tahminleri (etiket, sektör
  sayıları) doğru çıktı.
* **İki baskı (edition)** ([`EDITIONS.md`](EDITIONS.md)): diskteki 1996 `IST.EXE`, 2005'tekinden
  `main`, iki DEMOLAR yordamı ve `ctl_flush_keys` içinde, ayrıca hata çağrılarındaki satır
  numaraları ve dosya adlarında ayrılır. Bunların hepsi `ED_CD` arkasında geri derlendi;
  uyarlama ikisini de çalıştırır.
* **Açılış ve içe aktarma** ([`INSTALL.md`](INSTALL.md)): `DATA/` (bir disk `LALE.CHD` olarak
  ya da 2005 sürümünün dosyaları), `SAVE/` (oyunun yazdığı her şey, iki baskıda da), kaynaklar:
  CD sürücüsü, cue/bin, ISO, CHD, klasör, ZIP, 7Z, RAR. CHD (v5, cdzl/cdfl) yazıcısı ve
  okuyucusu, deflate, FLAC, Mode 1 EDC/ECC, ISO 9660 ve ZIP uyarlamanın kendi kodudur; hiçbir
  kütüphane eklenmedi.
* **4K videolar**: `VIDEO/` içindeki büyütülmüş kopyalar, oyunun kendi oynatıcısının üzerinde
  kare kare gösterilir; Ctrl+V ikisi arasında geçiş yapar ve `4K Video` / `Orijinal Video`
  bildirimini gösterir. Windows'ta Media Foundation, macOS'ta AVFoundation (ikincisi henüz
  derlenmedi).
* **Yolda bulunan: gerçek zamanda dikey geri dönüş (retrace) beklemeleri**
  (`src/platform/pc.c`): ana programın boşta uyuması (500 us), öykünmeli saati 64 us'lik dikey
  geri dönüş darbesinin üzerinden atlatıyordu; bu yüzden geri dönüşü bekleme birkaç kare
  sürüyordu. Video oynatıcı geride kalıyor ve giriş bölümünün 79 karesinden yalnızca 20 kadarını
  gösteriyordu; diğer bütün geri dönüş beklemeleri de aynı şekilde uzuyordu. Uyku artık darbeden
  önce bitiyor ve 3DAh'nin art arda iki okuması arasında saat darbenin başlangıcını geçemiyor.
  Tüm kareler oynuyor (79/79, 172/172 ölçüldü). Sanal zamanlı çalıştırmalar (testler, betikli
  oynanışlar) hiç etkilenmemişti.
* **Testler**: `tests/test_codecs.c` (ctest `codecs`, uydurma veriler üzerinde 288 denetim);
  karşılaştırmalı test takımı ve diğer birim testleri değişmeden geçiyor.

* **Türkçe iletişim kutuları, sistemin temasında** (2026-11-25, kullanıcının isteği): açılış
  sorusu, içe aktarma ilerlemesi, hatalar ve Hakkında kutusu; Windows'ta yerel (`ui_win32.c`),
  açık ve koyu, tema anında değişir; oyun penceresinin başlık çubuğu da modu izler. Tüm metinler
  `src/platform/uitext.h` içinde. Bkz. [`INSTALL.md`](INSTALL.md#programın-iletişim-kutuları).
* Windows için **Sürüm 1.0**: `tools/package_windows.sh` -> `dist/Lale64-1.0-Windows.zip`
  (program, SDL3.dll, önce Türkçe sonra İngilizce bir okuma dosyası (`OKU BENI - READ ME.txt`), lisanslar,
  `lale.ini.example`). Media Foundation ilk kullanımda yüklenir; bu yüzden program Windows'un N
  sürümlerinde de açılır (orada 4K videolar olmadan).

* **Alt+Enter** (2026-11-25, kullanıcının isteği): tam ekran ile pencere arasında geçiş, her
  sistemde; programındır, oyuna iletilmez.
* **Konsol penceresi yok** (2026-11-25, kullanıcının isteği): `Lale64.exe` bir Windows pencere
  programıdır (GUI alt sistemi). Günlüğü (log) bir boruya ya da dosyaya yönlendirilmişse oraya,
  bir komut isteminden başlatıldıysa o konsola yazılır; Explorer'dan açıldığında görünmez.
* **Level dergisinin CD'si** (2026-11-25, kullanıcının diski): Ocak 1998 kapak CD'si, redumper
  ile dökümü alındı (`_references/cd_level`, bkz. [`DISC.md`](DISC.md#level-dergisinin-cdsi-_referencescd_level)).
  Oyun dosyaları ilk CD ile bayt bayt aynı, ama ses izi yok. Port onu sürücüden ve disk
  kalıbından içe aktarır ve oynatır: varsayılan olarak 2005 baskısı (müzikle), `--edition cd` ile
  diskteki 1996 programı (giriş ve menü müziği olmadan); her ikisi de menüye ve oyuna ulaştı. CD
  sürücüsü artık etiketi `ISTANBUL` olmasa da kökünde oyunu taşıyan bir diski bulur; CD baskısı
  ses izi olmayan bir diskle de çalışır. Test kipinde (`--frames`) içe aktarma sırasında
  pencere artık "Yanıt Vermiyor" olmaz.
* **CD baskısı orijinaline karşı** (2026-11-25): `pair.py --cd`, 1996 `IST.EXE` DOSBox-X'te;
  intro'nun 1600 örneğinin tamamı portun görüntü durumlarıyla eşleşir (bkz.
  [`VERIFY.md`](VERIFY.md#cd-baskısı-1996---port-ve-orijinal-2026-11-25)).

Açık kalanlar: fiziksel bir sürücüden ses (burada sürücü yok), eksiksiz bir 3. iz, yeni
parçaların Mac derlemesi, CD baskısının intro'dan ötesinin (oynanışın) 1996 `IST.EXE`'ye
karşı karşılaştırılması.

### Başlatıcı desteği (2026-11-26)

Program iki başlatıcı (launcher) üzerinden kurulup başlatılabilir (kullanıcının isteği). Program
değiştirilmedi: oyunu zaten komut satırından alıyordu (`Lale64 --source <yol>`). Oyuncuya dönük
anlatım: [`README.md`](../README.md), [`INSTALL.md`](INSTALL.md#başlatıcılar-launcher).

* **`recomplauncher.json`** (depo kökü): [RecompLauncher](https://recomplauncher.com) bu dosyayı
  sürüm arşivinin en üstünde, programın yanında (`Lale64/` klasörünün içinde) okur. İçinde:
  oyuncunun seçtiği kaynak (`.iso`, `.cue` ya da içinde `3D.CKD` bulunan klasör), *Play*'in
  çalıştırdığı komut (`Lale64.exe` / `Lale64.app`, `--source <seçim>`), kayıt klasörü
  (`SAVE`) ve çıkış kodu 1'in anlamı. Arşivler ve `.chd` listede yoktur, çünkü o başlatıcı
  bunları kabul etmez. Kılavuz:
  <https://github.com/elripalda/RecompLauncher/blob/main/docs/DEVELOPERS.md>.
* **Kural: her sürüm arşivi `recomplauncher.json` taşır.** Yalnızca depoda duran dosya oyuncu
  için hiçbir şey yapmaz. `tools/package_windows.sh` ve `tools/package_macos.sh` onu depo
  kökünden paketin `Lale64/` klasörüne kopyalar. Sürüm dosyalarının adında sistemi belirten
  sözcük kalmalıdır (`Windows`, `macOS`).
* **Birlikte güncellenecekler:** bir komut satırı seçeneği (`--source`), programın dosya adı
  (`Lale64.exe`, `Lale64.app`), kayıt klasörü (`SAVE`) ya da bir ayar anahtarı değişirse,
  `recomplauncher.json` aynı sürümde değişir. Sürüm dosyasının adı (`Lale64-*-Windows.zip`) ya
  da programın adı değişirse `launcher/setup.ps1` içindeki `$Asset` ve `$Program` da değişir.
* **`launcher/launcher.json`, `launcher/setup.ps1`**:
  [sp00nznet'in recomp-launcher'ı](https://github.com/sp00nznet/recomp-launcher) için. Kurucu
  oyun klasörünü bulur ya da sorar, depoyu indirir ve `setup.ps1` betiğini çalıştırır (yalnızca
  Windows). Betik hiçbir şey derlemez: oyun klasörünü denetler (`3D.CKD`, `0GFL.CKD`,
  `TRDOS.DAT`), en yeni Windows sürümünü GitHub'dan indirir (GitHub'ın SHA-256 değeriyle
  karşılaştırır), `play\` klasörüne açar ve programı oyun klasörüyle başlatan
  `Lale Savascilari (recomp).cmd` dosyasını depo köküne yazar. `setup.ps1` git'te CR LF satır
  sonlarıyla saklanır.
* **`.github/workflows/installer.yml`**: sp00nznet'in yeniden kullanılabilir iş akışını
  çağırır; `005639c34bc2fc3096219fce2bd0a5ea13f8ac5b` işlemesine (commit) sabitlenmiştir
  (`launcher-ref` olarak da). `main` dalına yapılan ve `launcher/**` ya da iş akışını değiştiren
  bir itme (push), kurucuyu iş akışı çıktısı (artifact) olarak derler. İtilen bir `v*` etiketi
  ayrıca `Lale64-Installer-windows-x64.exe` ve `SHA256SUMS.txt` dosyalarını o etiketin sürümüne
  ekler; sürüm henüz yoksa onu kendiliğinden üretilmiş notlarla oluşturur. Bu yüzden yeni bir
  sürümde ya GitHub sürümünü etiketten önce oluşturun ya da notları sonradan düzeltin (önce
  Türkçe, sonra İngilizce). Proje sp00nznet'in `catalog.json` listesinde değildir.
* **Sürüm 1.0'a yapılan** (26 Kasım 2026): v1.0 sürümünün iki zip dosyası yerinde değiştirildi.
  Etiket aynı, program dosyaları bayt bayt aynı, yeni derleme yok; eklenen tek dosya
  `Lale64/recomplauncher.json`.

  | Dosya | Boyut | SHA-256 |
  |---|---|---|
  | `Lale64-1.0-Windows.zip` | 2,663,450 bayt | `3e64cc6a98376296679e10f26624d0758b9769cee52b030bd2c9eb1a8c4ce35f` |
  | `Lale64-1.0-macOS.zip` | 6,257,546 bayt | `a85691bc8b835a97ebdf166d6b82c146b179ca079d8b11189b6b966cb56ecc8b` |

* **Bilinen sınır:** adı verilen bir kaynak her açılışta yeniden içe aktarılır
  (`src/platform/setup.c`, `setup_game` içindeki 1. adım koşulsuzdur; `install_import`
  `DATA/` içinde aynı oyunun bulunup bulunmadığına bakmaz). RecompLauncher her *Play*'de
  `--source` verdiği için kopyalama her seferinde yinelenir: klasörde veri dosyaları `DATA/`
  içine yeniden kopyalanır (`SAVE/` içinde zaten bulunan dosyalara dokunulmaz), `.iso` ve
  `.cue` için `DATA/LALE.CHD` yeniden yazılır ve doğrulanır (gerçek CD'nin cue/bin dökümünde
  yaklaşık 13 s + 5 s, bkz. [`INSTALL.md`](INSTALL.md#doğrulama)). Sonuç doğrudur, yalnızca
  yavaştır. `DATA` aynı oyunu zaten içeriyorsa içe aktarmayı atlamak, sonraki bir sürüm için
  küçük bir kod değişikliğidir. sp00nznet'in `.cmd` dosyası da `--source` verir; aynı şey
  orada da geçerlidir.
* **Mac'te denetim (2026-11-26 gece, kullanıcının isteği; hiçbir şey değiştirilmedi):**
  yayımlanan `Lale64-1.0-macOS.zip` indirildi (GitHub'ın SHA-256 değeriyle aynı), `ditto` ile
  açıldı; `recomplauncher.json` şemaya uyuyor ve depodakiyle aynı. `Lale64.app`, başlatıcının
  yaptığı gibi başka bir klasörden, `RECOMPLAUNCHER=1` ve `--source <oyun klasörü>` ile
  çalıştırıldı: oyunu `{install}/DATA` içine aldı, `{install}/SAVE` klasörünü yaptı (bildirimdeki
  kayıt yolu) ve başlık ekranına geldi. Başlatıcıda ayarı yok. Gerçek bir kayıt yapılmadı;
  RecompLauncher'ın kendisi çalıştırılmadı (Mac sürümü noterden geçmemiş). **Risk:** uygulama
  ad hoc imzalı ve noterden geçmemiş; Gatekeeper onu reddeder, başlatıcı indirdiği dosyayı
  karantinaya alırsa *Play* engellenebilir (denenemedi).

### macOS uyarlaması (2026-11-23)

Bir Apple Silicon Mac üzerinde derlendi ve doğrulandı (macOS 27.2, Apple clang, Homebrew SDL3
3.4.18, DOSBox-X 2026.10.01, Ghidra 12.2 + openjdk@21). Neler yazıldığı ve uyarlamanın ortak
kodda neler bulduğu: [`MACOS.md`](MACOS.md).

| Denetim | Sonuç |
|---|---|
| Derleme | `cmake --preset macos`: `Lale64`, `Lale64.app`, `lale_testlib.dylib`, derleyici uyarısı yok. `macos-release`: evrensel uygulama, statik SDL3, en az macOS 11. `windows-cross` (macOS için llvm-mingw 20261104): Windows programı aynı ağaçtan hâlâ derleniyor, uyarı yok – çalıştırılmadı |
| Mac üzerinde çözümleme hattı | `tools/bootstrap.sh --tools --ghidra`: `ist_fpu.bin` tekrarlanan çalıştırmalarda aynı ve `fpupatch.py`'nin taramasıyla birebir aynı; 10,750 göreli dallanma, 0 yanlış. Ghidra'nın geri derleyicisinin (decompiler) sürümde arm64 Mac ikilisi yok: taslak C yok (ne uyarlamanın ne testlerin buna ihtiyacı var) |
| `ctest`: `vga`, `cdaudio` | geçiyor |
| Karşılaştırmalı test takımı (Unicorn'daki orijinal kod ile arm64 kütüphanesi karşı karşıya) | 256/256 geçiyor (`ctest`, son derleme, 2026-11-23; 29 dk). Yol üzerindeki üç başarısızlık gerçek bulgulardı (öykünmeli belleğin hizalanması, Windows'un bir erişim ihlalini `OSError`'a çevirmesine dayanan bir test, ve `cfcacce`'nin aynı anda ana dalda düzelttiği `sys_blit` sarması): bkz. MACOS.md "Found while porting" |
| DOSBox-X'teki orijinale karşı bütün oyun (`tools/verify/pair.py`) | `play3` (27 denetim noktası), `play4` (112), `play4load`, `play5`: her denetim noktası aynı, `GRP.DAT` ve `SAKLA001.SAV` aynı. Tek farklar belgelenmiş olanlardır, Windows'takilerle aynı: kayıt açıklamasının NUL'dan sonraki kuyruğu (play4/play4load'da `SAKLA002.SAV` 48 bayt, play5'te 30; play4load'da yükleme menüsünün liste girdisi A6AB-A6DB). `play3` ve `play4` uyarlama tarafında son derlemeyle tekrarlandı: tüm denetim noktaları yine aynı |
| Sürüm uygulamasının arm64 ve x86_64 dilimleri karşılaştırması (`play3`, Rosetta) | 108 döküm dosyasının hepsi ve yazılan `GRP.DAT` / `SAKLA001.SAV` bayt bayt aynı, ayrıca `macos` ön ayarının (preset) derlemesinin dökümleriyle de aynı |
| Açılış (`startup` betiği) | `s_01000`, `s_05000` Windows'taki gibi: DGROUP aynı, resim yalnızca giriş videosunun karesinde farklı (VERIFY.md'de anlatılan, saate bağlı fark) |
| Ses | SDL'in disk sürücüsü: giriş bölümünün 25 s'si kaydedildi, baştan sona sessiz değil (dinlenmedi) |
| Uygulama | oyun dosyaları yanındayken LaunchServices üzerinden başlatıldı (dosyalar bulundu, 1500 kare sunuldu); Hakkında kutusu ekran dışında çizildi, açık ve koyu |

Bir kişi tarafından denetlenmeyenler: kulakla ses, tam ekran, klasör iletişim kutusu ve Hakkında
kutusunun bir ekranda görünümü (uyarlamayı yapan oturum ekran görüntüsü alamıyordu).

### Çözümleme hattı düzeltmeleri (2026-11-20)

| Sorun | Düzeltme | Etki |
|---|---|---|
| Hazır (stock) Ghidra, gerçek kip (real mode) yakın dallanmalarını *doğrusal* 64 KB sınırlarında sarıyor; segment-ofset sarması (TLINK'in >32 KB geriye çağrılar için kullandığı) da eksikti | `ia_rel16.patch` (+`rmseg` bağlamı), `FixNear` | 183 yanlış dallanma hedefi → 10,738 içinde 0 (her çalıştırmada `branchcheck.py` ile denetlenir) |
| Switch atlama tabloları izlenmiyordu | `FixSwitch` | 45 tablo, 304 case hedefi ayrıştırıldı (disassembled) (ör. 33 FRPOS işlem kodu) |
| FixFPU, yanlış akışlarla ulaşılan işlenenlerin içindeki `CD 3x` baytlarını yamıyor ve gerçek yerleri kaçırıyordu (ovl2700, ~650 bayt) | içe aktarmadan önce `tools/fpupatch.py` ile doğrusal tarama (switch tablolarını atlayarak); FixFPU yalnızca KUR.EXE için tutuldu (2026-11-21) | ikili görüntü (image) artık bağımsız taramayla aynı; 10,746 dallanma, hâlâ 0 yanlış |
| Yanlış akışlardan doğan sahte işlevler | `PruneFunctions` | 10 tanesi kaldırıldı (2'sine "fragments" (parçalar) adı verilmişti) |
| Toplam | | çözülen çağrı yerleri 5,034 → 5,573; dizgi başvuruları 951 → 1,019; geri derleme başarısızlıkları 25 → 4 |
| RTL `__xcvt` (0000:0ADD, printf'in kayan nokta dönüşümü), `CMP` sınırı olmayan 3 girdili satır içi bir tablo üzerinden atlıyor (0000:0B08: float / double / long double); bu yüzden FixSwitch onu atlıyor, `fpupatch.py` da tablo sözcüklerini kod olarak tarayıp 0B13'teki `CD 3C` öykünücü çağrısını kaçırıyordu | FixSwitch C biçimi "inline": `JMP`'nin hemen ardındaki tablo, en düşük case hedefine kadar okunur; tablonun ötesini gösteren ve eşlenmiş her girdi kabul edilir, aksi halde yine raporlanır (2026-11-23) | 46 tablo / 307 case hedefi, 0 çözülemeyen; `__xcvt` tamamen çözüldü (48 → 514 bayt); 10,787 dallanma, 0 yanlış; fpupatch 2,022 → 2,023 yer |

Artık çözülemeyen tablo kalmadı (2026-11-23). O günkü çalıştırmanın sayıları, `__xcvt`
düzeltmesinden önce → sonra: çağrı yerleri 5,593 → 5,596, dizgi başvuruları 1,021 → 1,022 (yenisi
yanlış pozitif: 0000:0C5B'deki `MOV CX,4`, DS:0004 sanılmış), geri derleme başarısızlıkları 3.
`fpupatch.py`, *bir önceki* çalıştırmanın `build/ist_switches.tsv` dosyasındaki tablo
aralıklarını atlar; bu yüzden temiz bir `build/` üzerinde `tools/ghidra_run.sh` hattı
kendiliğinden ikinci kez çalıştırır.
Bu tarihten önce yazılmış çağıran listeleri (`analysis/names/*.md`), switch'ler ya da yanlış
çözülmüş yakın çağrılar üzerinden gelen çağıranları kaçırıyor olabilir; güncel listelemelere
karşı yeniden denetleyin.

### Referans çalışma ortamı

`C:\DOSBox-X\dosbox-x.exe -conf tools/dosbox/ist.conf` orijinali `orig/` klasöründen çalıştırır
(386, VGA, EMS, SB16). 2026-11-20'de giriş bölümüne kadar açıldığı doğrulandı.
`tools/dosbox/oracle.py` onu hata ayıklayıcı bağlantısı üzerinden betikle yönetir (kesme
noktaları, DGROUP/VGA/DAC dökümleri).

## English

Status legend for functions: **draft** (written, not yet verified), **verified** (passes a
differential test or was checked against the oracle), **replaced** (platform/RTL
equivalent, no decompilation needed).

### Milestones

| # | Milestone | State |
|---|---|---|
| 1 | Foundation: unpacker, segment model, Ghidra pipeline, FPU fix, listings | done (2026-11-20) |
| 2 | Inventory: all functions named and modules mapped | done (2026-11-21): 880 names (`analysis/names.tsv`), module map in PLAN.md §2 |
| 3 | Formats documented, with Python viewers | done (2026-11-23): every format in [`FORMATS.md`](FORMATS.md) checked against all game files - FLiB, PAL, PUR/SND, RES, CMP, GFL, FRPOS, GRP/SAV, TRDOS, KOR, RIX, AMF, DSD (`tools/fmtcheck.py`, the viewers, the tests); only the in-memory ISOSYS battle state is partly verified |
| 4 | Platform layer: VGA emulation, SDL3 host, timer, input, audio | VGA: modes 13h/X/12h scan-out, BIOS tables and DACs verified against DOSBox-X; virtual CD drive + CD-music option; SDL3 host done (`src/platform/sdl3_main.c`: window + renderer, event pump with keyboard/mouse, PIT-clocked timer, game + CD audio through `audio_sdl.c`) |
| 5 | Engine (ALTDUZEY) decompiled | all engine layers below the UI done and integrated (2026-11-21): 103/103 differential tests in one DLL, exact Borland far-heap layout; GRTEXT (10/10) + INTRFACE (34/34) done, 17 tests, in base (120/123 green; the 3 are the 3DQUICK tests, merged by WP16). Game-module wave WP13–17 done and integrated (2026-11-21): every function of IST.EXE is decompiled; shared DLL 218/219 before the last battle test landed; whole-game verification (docs/VERIFY.md): the intro matches the original in DOSBox-X from start-up to the menu (200 samples, 0–50 s; EMS double-mapping fix); scripted play-through play3 (read-count sync + poke/srand anchors) matches the original exactly at every checkpoint – menu, chargen x4, district, save, door/treasure event, battle start + 12 battle keys (registers, DAC, screen, VRAM, DGROUP; GRP.DAT and SAKLA001.SAV byte-identical) – except one checkpoint inside the clock-timed fade into the story. Fixed on the way: save description buffer, DDS driver load via vfs (lowwater 6E06/DE30 now match), DOSBox-X SB16 IRQ 7 (voice wait). play3's chargen-fade checkpoint now anchored on the story video's frame (identical). play4 (2026-11-21, with sound on both sides): spell battle lost to the dog's AI, game over + load, second battle won, item exchange, goods/weapon shops (buy/sell by keys and mouse), food stall, camp, save – all 112 checkpoints identical (DGROUP minus host/clock fields, screen, DAC); SAKLA002.SAV differs only in the description tail, uninitialised stack in the original; no port bugs found, new clock-relative `@n` script timing. Doctor: play5 (2026-11-22, crafted save in district 4): doctor (refuse, not dead x3, revive, too poor), fortune-teller, hotel room + camp + keeper, save, step back/sidestep/view size/character screen/options/city map – 39 checkpoints identical. Lale64.exe (was lale.exe) runs the intro (start-up matches the DOSBox-X oracle in EMS/DAC/VGA/screen; start-up heap chain (107 blocks) and DGROUP identical since the driver load goes through the file layer; what still differs is AMP's private interrupt stack, native routine pointers and the DSMI clock phase – none read by game code; checked 2026-11-22). Earlier: infrastructure done (2026-11-20): DGROUP model `dg[]`, emulated PC `pc.h`, Unicorn differential harness, SDL3 host loop; wave 1 (EMS/MEMMAN/NKONTROL, DSMI DDS/timer, 386 blitters + 3D quads, file layer + Borland stdio, AMP player, MCP mixer) done |
| 6 | Game modules decompiled | done (2026-11-21): main, DEMOLAR, CHARGEN, FRPOS, 3DQUICK, 3DSYS, ISOSYS, GFL – see "Decompiled modules" |
| 7 | DSMI audio decompiled | done: player, loader, CDI, MCP mixer (verified); audio backend wired into the host (NSES registers the native MCP as SDISB16.DSD; `audio.c` / `audio_sdl.c`) |
| 8 | Full-game verification against DOSBox-X | play3, play4, play4load identical at every checkpoint (docs/VERIFY.md). Decision (user, 2026-11-22): the save description's tail after its NUL stays zeros in the port (uninitialised stack in the original, never read). Gap waves 2026-11-22/23 (below): doctor, ISOSYS Ctrl-Break / sound-on / door wall, 3DQUICK 100x100, CD audio, play5..play10, playq, playx, Ctrl-Break and Ctrl+Alt+Del in DOSBox-X itself. Every DEMOLAR routine now runs in a play-through against the original |
| 9 | macOS build | done (2026-11-23): `Lale64` + `Lale64.app` on Apple Silicon, universal release package; differential suite and scripted play-throughs against DOSBox-X run on the Mac – see "macOS port" below and [`MACOS.md`](MACOS.md) |

### Function status

Generated by `python tools/funcstatus.py` from the `Original:` / `Verified:` comments
of `src/` (2026-11-23; `--list` names every routine).  Only routines with their own
`Original:` line are counted, so a module's count can be below the "Decompiled modules"
count (small helpers documented together).

| Segment | Sources | Functions | verified | draft | replaced | unstated |
|---|---|---|---|---|---|---|
| 0000 | bc_farheap, bc_stdio, ngraphs, speaker | 65 | 61 | 4 | 0 | 0 |
| 0546 | istanbul | 1 | 1 | 0 | 0 | 0 |
| 05DC | intrface | 32 | 32 | 0 | 0 | 0 |
| 08D0 | q3d | 8 | 8 | 0 | 0 | 0 |
| 09D4 | memman | 21 | 21 | 0 | 0 | 0 |
| 0AC6 | systems | 18 | 18 | 0 | 0 | 0 |
| 0C7F | ngraphs | 15 | 15 | 0 | 0 | 0 |
| 0D42 | nses | 11 | 9 | 2 | 0 | 0 |
| 0DB7 | nkontrol | 11 | 8 | 3 | 0 | 0 |
| 0DDF | dsmistr | 9 | 9 | 0 | 0 | 0 |
| 0DF2 | dds | 11 | 11 | 0 | 0 | 0 |
| 0E9E | memalloc | 4 | 3 | 1 | 0 | 0 |
| 0EAE | cdi | 28 | 28 | 0 | 0 | 0 |
| 0EF6 | amp | 32 | 32 | 0 | 0 | 0 |
| 105B | amfload | 3 | 3 | 0 | 0 | 0 |
| 10EF | timer | 9 | 9 | 0 | 0 | 0 |
| 111B | amfload | 1 | 1 | 0 | 0 | 0 |
| 1134 | ems | 9 | 9 | 0 | 0 | 0 |
| 1145 | dsmitime | 6 | 6 | 0 | 0 | 0 |
| 1154 | emsheap | 17 | 16 | 1 | 0 | 0 |
| 122B | istanbul | 1 | 1 | 0 | 0 | 0 |
| 2599 | blit386 | 11 | 11 | 0 | 0 | 0 |
| 2700 | q3dview, q3dview_anim, q3dview_map | 28 | 28 | 0 | 0 | 0 |
| 303F | s3d_camp, s3d_charscr, s3d_district, s3d_keys, s3d_loop, s3d_menu, s3d_move, s3d_panel, s3d_save | 30 | 30 | 0 | 0 | 0 |
| 39E5 | chargen | 12 | 12 | 0 | 0 | 0 |
| 3C2D | demolar | 13 | 13 | 0 | 0 | 0 |
| 3FD1 | frpos, frpos_shop, frpos_util | 25 | 25 | 0 | 0 | 0 |
| 4595 | iso_ai, iso_battle, iso_char, iso_combat, iso_draw, iso_field, iso_map, iso_spell, iso_turn, iso_util | 69 | 69 | 0 | 0 | 0 |
| 5313 | gfl | 12 | 12 | 0 | 0 | 0 |
| 5447 | fileman | 7 | 7 | 0 | 0 | 0 |
| 5511 | ographs | 13 | 13 | 0 | 0 | 0 |
| 559A | fatal | 4 | 3 | 1 | 0 | 0 |
| 55CC | oses | 2 | 2 | 0 | 0 | 0 |
| 5646 | vfile | 9 | 9 | 0 | 0 | 0 |
| 569A | vfs | 13 | 13 | 0 | 0 | 0 |
| 5737 | cd5737 | 15 | 15 | 0 | 0 | 0 |
| 5784 | grtext | 10 | 10 | 0 | 0 | 0 |
| **all** | | **585** | **573** | **12** | **0** | **0** |

The 12 drafts: the PC-speaker routines of `speaker.c` (`bc_readtimer`, `bc_delay`,
`bc_sound`, `bc_nosound`: their only user, `o559A_beep`, is unreferenced), the NSES
getenv/setvect hooks (host environment, no IVT), `ctl_div0_isr`, `ctl_timer_install`,
`ctl_dds_timer_init` (an interrupt handler and two thin calls into DSMI, by
inspection), `MemRealloc` (never called), `emsGetPageMapRaw` and `o559A_beep`
(unreferenced).

### Decompiled modules

| Module | Functions | State |
|---|---|---|
| ovl5737 CD audio (`src/game/cd5737.c`) | 15/15 | verified (2026-11-22, `tests/py/test_cd5737.py`, 16 tests: every device request, return value and DGROUP write vs the original code over an MSCDEX model; 466/468 instructions run, the 2 others unreachable). Fixed: `vtoc_ok` without MSCDEX / wrong drive / audio-only disc, stack reuse in `play_track`, `read_cfg` (was a stub); `cdrom.c` now leaves the request untouched for a non-CD drive and fills in the subunit, as MSCDEX does. Not compared: `drive_info` without MSCDEX (uninitialised far pointer in the original, no callers) |
| Borland `rand/srand` (`src/rtl/bc_rand.c`) | 2/2 | verified (differential, 4 seeds × 2000 steps) |
| FILEMAN `fm_cmp_decode` (`src/engine/fileman.c`) | 1/7 | verified (differential, game CMP files + random) |
| DSMI DDS registry seg0DF2 (`src/dsmi/dds.c`) | 11/11 | verified (differential); DDSLoadDriver maps .DSD names to native drivers |
| DSMI string helpers seg0DDF (`src/dsmi/dsmistr.c`) | 9/9 | verified (differential, quirks kept) |
| DSMI Timer Service seg10EF (`src/dsmi/timer.c`) | 9/9 | verified (differential: call order, slot table, PIT writes) |
| DSMI time base seg1145 (`src/dsmi/dsmitime.c`) | 6/6 | verified (differential) |
| DSMI raw EMS seg1134 (`src/dsmi/ems.c`) | 9/9 | verified (differential) |
| DSMI EMS heap seg1154 (`src/dsmi/emsheap.c`) | 17/17 | verified except unreferenced emsGetPageMapRaw (block placement byte-exact) |
| DSMI MemAlloc seg0E9E (`src/dsmi/memalloc.c`) | 4/4 | return codes verified |
| MEMMAN seg09D4 (`src/engine/memman.c`) | 22/22 | verified (differential); on the transcribed Borland far heap (`far_malloc`/`far_free`, `mm_coreleft` → `bc_farcoreleft`); low-water mark 6E06 matches the original (milestone 5) |
| NKONTROL seg0DB7 (`src/engine/nkontrol.c`) | 11/11 | tick/flags verified; INT 09h hook and INT 23h handler against DOSBox-X (playbrk, playbrk3d, playcad); div-0 ISR and the two DSMI timer calls by inspection |
| Borland stdio/io (`src/rtl/bc_stdio.c`) | 40 routines | verified (differential incl. text mode) |
| stdio handle layer ovl5646 (`src/engine/vfile.c`) | 9/9 | verified (differential) |
| FLiB virtual files ovl569A (`src/engine/vfs.c`) | 13/13 | verified (differential, all 8 archives) |
| 386 blitters asm2599 (`src/engine/blit386.c`) | 11/11 | verified (differential, whole destinations) |
| DSMI CDI seg0EAE + AMP 2.00 player seg0EF6 + AMF loader seg105B/111B (`src/dsmi/cdi.c, amp.c, amfload.c`) | 100/100 | verified (all 15 game AMFs: identical CDI call streams over 2,500 ticks) |
| DSMI MCP v3.5 software mixer from SDISB16.DSD (`src/dsmi/mcp.c`) | all mixer + 26 CDI slots + SDI shell | verified bit-exact (DMA ring) against the driver in Unicorn; 22050 Hz stereo s16 |
| NSES seg0D42 + OSES ovl55CC (`src/engine/nses.c, oses.c`) | 13/13 | verified except getenv/setvect hooks; native MCP registered as SDISB16.DSD |
| Audio backend (`src/platform/audio.c, audio_sdl.c`) | – | emulated SB16 DMA clocked by the PIT; game + CD audio in one SDL stream; wired into the host |
| FILEMAN rest ovl5447 (`src/engine/fileman.c`) | 7/7 | verified (real CKD members, >64 KB pictures, EMS remap points) |
| Fatal/misc ovl559A (`src/engine/fatal.c`) | 4/4 | 3 verified; beep unreferenced |
| Borland far heap (`src/rtl/bc_farheap.c`) | 10 routines | verified: seg:off, farcoreleft and arena bytes vs the original from the main() snapshot; MEMMAN, DSMI MemAlloc/EMS heap, FILEMAN, the file layer and the game modules all allocate through it |
| GFL/GHD video player ovl5313 (`src/game/gfl.c`) | 12/12 | verified (decoder on all 35 videos vs tools/gflview.py; player runs vs original: DGROUP/EMS/screen/palette/CDI) |
| SYSTEMS seg0AC6 + NGRAPHS seg0C7F + OGRAPHS ovl5511 (`src/engine/systems.c, ngraphs.c, ographs.c`) | 52/52 | verified (23 tests: DGROUP, A000, VGA regs/DAC/planes, EMS); fade interpolation only at ms=0 |
| main seg0546 + DEMOLAR ovl3C2D (`src/game/istanbul.c`, `demolar.c`) | 14/14 | verified (6 tests: waits, picture show/fade, zone outline, music off; against DOSBox-X: start-up and intro, every menu entry, story slideshow (play8), game over (play4), ending (play9), credits (playx, play9; background slot fixed 2026-11-23), city-map travel (play10), new-game intro (play3, playq)) |
| CHARGEN ovl39E5 (`src/game/chargen.c`) | 12/12 | verified (8 tests: stat rolls, draw, load/save GRP.DAT, cancel, full 4-member flow by keys and mouse, animations) |
| ISOSYS battles ovl4595 (`src/game/iso*.c`) | 77/77 | verified (32 tests: rules, AI 96 seeded turns, every vecize, battlefield of all 18 maps, divide-by-zero via the INT 0 handler semantics, 8 whole scripted battles with real SAVAS.CKD graphics); 2026-11-22 (`test_iso_d.py`, 12 tests): Ctrl-Break (the original ends through its INT 23h handler at the next kbhit(); the port continued the battle – fixed), sound-on paths (same CDI/AMP call order and state in single calls and three whole battles), door wall without bar picture (cannot occur in the 18 shipped maps; forced, the original ends with internal error 10h and so does the port – no garbage blit, no crash) |
| FRPOS shop/event scripts ovl3FD1 (`src/game/frpos*.c`) | 33/33 | verified (5 tests: every event/trigger of all 18 PCSn files, 33/33 opcodes; tools/frposdis.py) – in its own DLL, joins base after the wave |
| 3DSYS exploration ovl303F (`src/game/s3d*.c`) | 32/32 | verified (charscr, loop, menus, panel, camp, move, keys, save write + read incl. the original's SAKLA001.SAV, district load/enter/location, all wall kinds); 2026-11-22 (`test_s3d_gap.py`): the portrait/zone routine 303F:27F7 (44 cases), and the two routines no code references – debug buffers 303F:23ED, map writer 303F:189D (written bytes identical) – verified |
| 3DQUICK first-person renderer ovl2700 (`src/game/q3dview*.c`) | 36/36 | verified (15 tests: start-up tables over ~340 view sizes, all 18 maps x 4 facings, automap, animations); 2026-11-22 (`test_q3dview_gap.py`, 5 tests): rotations in the game's configuration (100 window, speed 9) identical in whole A000/surfaces/DGROUP; 100x100 render identical up to the floor/ceiling strips, where the original (and the port) loops forever in the strip blitter – the game never renders at 100x100; the "742 bytes" were the port missing the 64 KB offset wrap in `q3d_draw_quad_mapped` (only after an ignored fatal error) – fixed in `q3d.c` (`far_at`) |
| 3D quads seg08D0 (`src/game/q3d.c`) | 7/7 | verified (differential, bit-exact x87 model); EMS lock wrapper by inspection |

### Gap wave (2026-11-22)

Engine and host fixes found while closing the gaps above:

| Where | Problem | Fix |
|---|---|---|
| `src/engine/intrface.h` | `ui_wait_voice` set the silence sample from DGROUP:DE1E; the original pushes DF1Eh (05DC:1E3C: sound slot 0Ah, bosch.pur) | constant corrected; `test_iso_d.test_sound_silence_sample` |
| `src/engine/systems.c` | `sys_blit` / `sys_blit_recolor` with a destination row above a conventional block (the big capital of a battle text box, y = -1 / -2) crashed; the original's huge pointer wraps to just below the block | the piece is placed as the original's 1 MB wrap places it (for a 320-wide surface: negative rows fall below the block and are dropped, the rest drawn from row 0; for other widths the whole piece misses the block); `test_iso_d.test_textbox_capital`, `test_q3dview_gap.test_render_100` (`sys_blit`; `sys_blit_recolor` by the same reasoning, no test reaches it) |
| `src/game/q3d.c` | missing 64 KB offset wrap in the quad drawer (after an ignored fatal error only) | `far_at` |
| `src/platform/cdrom.c` | request to a non-CD drive got a status written; MSCDEX leaves the header alone; subunit not filled in | as MSCDEX |
| `src/platform/pc.c`, `nkontrol.c`, `sdl3_main.c` | Ctrl-Break could not be produced by the host | Ctrl+Pause / Ctrl+ScrollLock = BIOS Ctrl-Break (buffer emptied, word 0000h, INT 23h at the next kbhit()); the game's INT 09h hook is offered every make code. A typed Ctrl+C stays an ordinary key, as the original in DOSBox-X takes it at the main menu (MS-DOS itself may break there – not verifiable here) |
| `src/platform/sdl3_main.c` | a key release was handled like a press: Esc, Enter, Tab, cursor and function keys were pushed twice in interactive play (scripts bypass the window) | releases only update the shift flags |

Final state of the wave (2026-11-23, build of commit cfcacce): differential suite 256/256
(out/base); play4 112/112 checkpoints, play4load and play5 (39/39) identical to the original in
DOSBox-X apart from the save description tail (kept as zeros, see milestone 8) - play4 and
play4load were run on the build before the last `sys_blit` correction, play5 after it.

Second round (2026-11-23):

* **play6** (crafted save with the district-4 story flags 29, 97, 30, 98, 31, 99): right
  button at the doctor's "who" prompt, the food stall behind the flag-99 door (refuse, eat,
  too poor), every entry of the options list changed by mouse and back by keys, successful
  sidesteps in both directions - 46 checkpoints identical (docs/VERIFY.md).
* **Ctrl-C / Ctrl-S**: settled from Microsoft's published MS-DOS source (v2.0 binary, v4.0
  source): INT 21h AH=0Bh - kbhit() - reads a waiting ^C and issues INT 23h, reads ^S and
  the next character; AH=07h - getch() - never checks.  The port follows MS-DOS
  (`src/platform/pc.c`); DOSBox-X's DOS does not look at the character, so the original
  there breaks only on Ctrl-Break - the one deliberate difference from the reference.
  ^P / ^N (printer echo) are not modelled.
* **Key release fix** confirmed in the live window: key messages posted to Lale64.exe, one
  intro screen per Esc, one menu entry per Down/Up.

### Gap wave 3 (2026-11-23)

The list left open by the second round, closed (details: docs/VERIFY.md "Gap wave 3"):

* **District 4's closed doors, weapon and goods shops, options by hover + Enter**: play7,
  53 checkpoints identical.
* **Story slideshow** (play8, 14), **ending** (play9, 21), **credits from the menu**
  (playx, 13), **quick start** (playq), **city-map travel** (play10, 16): identical but for
  the save description tail (as before) and the RNG state at two turn checkpoints (one
  portrait-blink roll, clock phase after an arrival; screen and the rest of DGROUP equal).
* **Ctrl-Break and Ctrl+Alt+Del inside DOSBox-X**: new script event `kbd` (raw scan codes
  through the 8042 command D2h) and an `int23` dump on both sides; at the main menu
  (playbrk) and in a district (playbrk3d, playcad) identical.
* **`o5737_cd_drive_info` without MSCDEX**: not comparable by construction - the original
  reads an uninitialised stack far pointer when INT 2Fh 1501h writes nothing; no caller in
  IST.EXE.  Documented in `cd5737.c`.
* **MS-DOS after 4.0**: Ralf Brown's Interrupt List (the reference for every DOS version,
  delorie.com mirror) gives INT 21h AH=0Bh as "DOS 1+ - ^C/^Break are checked, and INT 23
  is called if either pressed" and AH=07h as "does not check ^C/^Break", with no
  version-specific exceptions - what the port models.

Fixed on the way:

| Where | Problem | Fix |
|---|---|---|
| `src/game/demolar.c` | `demo_credits_exit` copied surface 9 to the screen instead of 0Bh (grup.cmp): the credits showed the previous screen around the scroll window | slot 0Bh, as the original (3C2D:222C) |
| `src/engine/nkontrol.c`, `src/platform/pc.c` | the INT 09h hook called the INT 23h handler directly on Ctrl+Alt+Del; the original issues INT 1Bh, so DOS breaks at the next `kbhit()` | `pc_int1b()`; INT 23h observer for the `int23` dump |
| `tools/verify/vcompare.py` | the EMS page-map clock fields (86F8, E7AC) were one word short | 4 words |
| `tools/ghidra_run.sh`, `FixSwitch.java` | the RTL's inline switch table 0000:0B08 unresolved; fpupatch.py ran on the previous switch list | form C (inline tables), the pipeline runs a second time when the switch list changed: 46 tables, 0 unresolved |

### The real CD, both editions, DATA / SAVE / VIDEO (2026-11-24)

The user's redumper dump of the game CD arrived (`_references/cd`).  What it settled and what
was built on it:

* **The disc** ([`DISC.md`](DISC.md)): tracks 1 and 2 match redump.org 91232; the data track is
  mastered 48 samples early, which the importer repairs; track 3 lacks its last 48 samples.  The
  data files equal the 2005 release's.  The simulated image's guesses (label, sector counts) held.
* **Two editions** ([`EDITIONS.md`](EDITIONS.md)): the 1996 `IST.EXE` on the disc differs from
  the 2005 one in `main`, two DEMOLAR routines and `ctl_flush_keys`, plus line numbers and file
  names in error calls.  All of it is decompiled behind `ED_CD`; the port runs either.
* **Start-up and import** ([`INSTALL.md`](INSTALL.md)): `DATA/` (a disc as `LALE.CHD`, or the 2005
  release's files), `SAVE/` (everything the game writes, both editions), sources: CD drive,
  cue/bin, ISO, CHD, folder, ZIP, 7Z, RAR.  CHD (v5, cdzl/cdfl) writer and reader, deflate, FLAC,
  Mode 1 EDC/ECC, ISO 9660 and ZIP are the port's own code - no library was added.
* **4K videos**: upscaled copies in `VIDEO/` shown over the game's own player, frame for frame;
  Ctrl+V switches, with the notice `4K Video` / `Orijinal Video`.  Media Foundation on Windows,
  AVFoundation on macOS (the latter not built yet).
* **Found on the way - retrace waits in real time** (`src/platform/pc.c`): the host's idle sleep
  (500 us) stepped the emulated clock over the 64 us vertical-retrace pulse, so a wait for the
  retrace lasted several frames.  The video player ran late and showed about 20 of the intro's
  79 frames; every other retrace wait was stretched the same way.  The sleep now ends before
  the pulse, and between two consecutive reads of 3DAh the clock cannot pass the pulse's start.
  All frames play (79/79, 172/172 measured).  Virtual-time runs (tests, scripted play-throughs)
  were never affected.
* **Tests**: `tests/test_codecs.c` (ctest `codecs`, 288 checks on made-up data); the differential
  suite and the other unit tests pass unchanged.

* **Dialogs in Turkish, in the system's theme** (2026-11-25, the user's request): start-up
  question, import progress, errors and the About box; native on Windows (`ui_win32.c`), light
  and dark, re-themed on the fly; the game window's title bar follows the mode. All texts in
  `src/platform/uitext.h`. See [`INSTALL.md`](INSTALL.md#the-programs-dialogs).
* **Release 1.0** for Windows: `tools/package_windows.sh` -> `dist/Lale64-1.0-Windows.zip`
  (program, SDL3.dll, a READ ME in Turkish then English, the licences, `lale.ini.example`).
  Media Foundation is loaded on first use, so the program also starts on Windows N editions
  (without the 4K videos there).

* **Alt+Enter** (2026-11-25, the user's request): full screen <-> window on every system; the
  program's, it does not reach the game.
* **No console window** (2026-11-25, the user's request): `Lale64.exe` is a Windows windowed
  program (GUI subsystem). Its log goes to a pipe or file when redirected, to the console when
  started from a command prompt, and nowhere visible when opened from Explorer.
* **The Level magazine CD** (2026-11-25, the user's disc): the January 1998 cover CD, dumped with
  redumper (`_references/cd_level`, see [`DISC.md`](DISC.md#the-level-magazine-cd-_referencescd_level)).
  The game's files are byte for byte those of the first CD, but there are no audio tracks. The
  port imports it from the drive and from the disc image and plays it: by default as the 2005
  edition (with music), with `--edition cd` as the disc's 1996 program (no intro and menu music);
  both reached the menu and the game. The CD drive search now also finds a disc not labelled
  `ISTANBUL` that holds the game in its root; the CD edition also runs with a disc without audio
  tracks. In test mode (`--frames`) the window no longer turns "Not Responding" during an import.
* **CD edition against its original** (2026-11-25): `pair.py --cd`, the 1996 `IST.EXE` in
  DOSBox-X; all 1600 samples of the intro match video states of the port (see
  [`VERIFY.md`](VERIFY.md#cd-edition-1996---port-vs-original-2026-11-25)).

Open: audio from a physical drive (no drive here), a complete track 3, the Mac build of the new
parts, a comparison of the CD edition beyond the intro (game play) against the 1996 `IST.EXE`.

### Launcher support (2026-11-26)

The program can be installed and started from two launchers (the user's request). The program
was not changed: it already took the game on its command line (`Lale64 --source <path>`). The
player's side: [`README.md`](../README.md), [`INSTALL.md`](INSTALL.md#launchers).

* **`recomplauncher.json`** (repository root): [RecompLauncher](https://recomplauncher.com) reads
  it from the top of the release archive, next to the program (inside the `Lale64/` folder). It
  holds: the source the player picks (`.iso`, `.cue`, or the folder that holds `3D.CKD`), the
  command *Play* runs (`Lale64.exe` / `Lale64.app`, `--source <pick>`), the saves folder
  (`SAVE`) and what exit code 1 means. Archives and `.chd` are not in its list because that
  launcher refuses them. Guide:
  <https://github.com/elripalda/RecompLauncher/blob/main/docs/DEVELOPERS.md>.
* **Rule: every release archive carries `recomplauncher.json`.** A file that is only in the
  repository does nothing for players. `tools/package_windows.sh` and `tools/package_macos.sh`
  copy it from the repository root into the package's `Lale64/` folder. Release file names must
  keep the word for the system (`Windows`, `macOS`).
* **To be kept in step:** if a command-line option (`--source`), the program's file name
  (`Lale64.exe`, `Lale64.app`), the saves folder (`SAVE`) or a settings key changes,
  `recomplauncher.json` changes in the same release. If the release file's name
  (`Lale64-*-Windows.zip`) or the program's name changes, so do `$Asset` and `$Program` in
  `launcher/setup.ps1`.
* **`launcher/launcher.json`, `launcher/setup.ps1`**: for
  [sp00nznet's recomp-launcher](https://github.com/sp00nznet/recomp-launcher). Its installer
  finds or asks for the game folder, downloads the repository and runs `setup.ps1` (Windows
  only). The script compiles nothing: it checks the game folder (`3D.CKD`, `0GFL.CKD`,
  `TRDOS.DAT`), downloads the newest Windows release from GitHub (checked against GitHub's
  SHA-256), unpacks it into `play\` and writes `Lale Savascilari (recomp).cmd` into the
  repository's root, which starts the program with the game folder. `setup.ps1` is kept in git
  with CR LF line ends.
* **`.github/workflows/installer.yml`**: calls sp00nznet's reusable workflow, pinned to commit
  `005639c34bc2fc3096219fce2bd0a5ea13f8ac5b` (also as `launcher-ref`). A push to `main` that
  changes `launcher/**` or the workflow builds the installer as a workflow artifact. A pushed
  `v*` tag also attaches `Lale64-Installer-windows-x64.exe` and `SHA256SUMS.txt` to that tag's
  release, and creates the release with generated notes if it does not exist yet. So for a new
  version, either create the GitHub release before the tag, or edit the notes afterwards
  (Turkish first, then English). The project is not in sp00nznet's `catalog.json` list.
* **What was done to release 1.0** (26 November 2026): the two zips of the v1.0 release were
  replaced in place. Same tag, the program's files the same byte for byte, no new build; the
  one added file is `Lale64/recomplauncher.json`.

  | File | Size | SHA-256 |
  |---|---|---|
  | `Lale64-1.0-Windows.zip` | 2,663,450 bytes | `3e64cc6a98376296679e10f26624d0758b9769cee52b030bd2c9eb1a8c4ce35f` |
  | `Lale64-1.0-macOS.zip` | 6,257,546 bytes | `a85691bc8b835a97ebdf166d6b82c146b179ca079d8b11189b6b966cb56ecc8b` |

* **Known limit:** a named source is imported again at every start (`src/platform/setup.c`,
  step 1 of `setup_game` is unconditional; `install_import` does not look whether `DATA/`
  already holds the same game). As RecompLauncher passes `--source` at every *Play*, the copy
  is repeated each time: for a folder the data files are copied to `DATA/` again (files already
  in `SAVE/` are left alone), for an `.iso` or `.cue` `DATA/LALE.CHD` is written and verified
  again (about 13 s + 5 s for the cue/bin dump of the real CD, see
  [`INSTALL.md`](INSTALL.md#verification)). The result is correct, only slow. Skipping the
  import when `DATA` already holds the same game would be a small code change for a later
  version. sp00nznet's `.cmd` passes `--source` too, so the same holds there.
* **Checked on the Mac (2026-11-26, night, at the user's request; nothing changed):** the
  published `Lale64-1.0-macOS.zip` downloaded (the same SHA-256 as GitHub's) and unpacked with
  `ditto`; `recomplauncher.json` passes the schema and is the repository's. `Lale64.app` started
  as the launcher does, from another folder, with `RECOMPLAUNCHER=1` and `--source <game
  folder>`: it imported the game into `{install}/DATA`, made `{install}/SAVE` (the manifest's
  saves path) and reached the title screen. It has no launcher settings. No real saved game
  was made; RecompLauncher itself was not run (its Mac build is not notarized). **Risk:** the
  app is ad-hoc signed, not notarized, so Gatekeeper rejects it; if the launcher quarantines
  its download, *Play* may be blocked (not tried).

### macOS port (2026-11-23)

Built and verified on an Apple Silicon Mac (macOS 27.2, Apple clang, Homebrew SDL3
3.4.18, DOSBox-X 2026.10.01, Ghidra 12.2 + openjdk@21).  What was written, and what
the port found in shared code: [`MACOS.md`](MACOS.md).

| Check | Result |
|---|---|
| Build | `cmake --preset macos`: `Lale64`, `Lale64.app`, `lale_testlib.dylib`, no compiler warnings.  `macos-release`: universal app, static SDL3, minimum macOS 11.  `windows-cross` (llvm-mingw 20261104 for macOS): the Windows program still compiles from the same tree, no warnings – not run |
| Analysis pipeline on the Mac | `tools/bootstrap.sh --tools --ghidra`: `ist_fpu.bin` the same on repeated runs and identical to `fpupatch.py`'s sweep; 10,750 relative branches, 0 wrong.  Ghidra's decompiler has no arm64 Mac binary in the release: no draft C (not needed by the port or the tests) |
| `ctest`: `vga`, `cdaudio` | pass |
| Differential suite (original code in Unicorn vs the arm64 library) | 256/256 pass (`ctest`, final build, 2026-11-23; 29 min).  Three failures on the way were real findings (alignment of the emulated memory, a test that relied on Windows turning an access violation into `OSError`, and the `sys_blit` wrap that `cfcacce` fixed upstream at the same time): see MACOS.md "Found while porting" |
| Whole game vs the original in DOSBox-X (`tools/verify/pair.py`) | `play3` (27 checkpoints), `play4` (112), `play4load`, `play5`: every checkpoint identical, `GRP.DAT` and `SAKLA001.SAV` identical.  The only differences are the documented ones, the same as on Windows: the tail of the save description after its NUL (`SAKLA002.SAV` 48 bytes in play4/play4load, 30 in play5; the load menu's list entry A6AB-A6DB in play4load).  `play3` and `play4` repeated on the port side with the final build: all checkpoints identical again |
| arm64 vs x86_64 slice of the release app (`play3`, Rosetta) | all 108 dump files and the written `GRP.DAT` / `SAKLA001.SAV` byte-identical, and identical to the dumps of the `macos` preset's build |
| Start-up (`startup` script) | `s_01000`, `s_05000` as on Windows: DGROUP identical, the picture differs by the intro video's frame only (the clock-timed difference described in VERIFY.md) |
| Audio | SDL's disk driver: 25 s of the intro captured, non-silent throughout (not listened to) |
| App | started through LaunchServices with the game files beside it (found, 1500 frames presented); About box rendered off screen, light and dark |

Not checked by a person: sound by ear, full screen, the folder dialog and the About
box on a screen (the session that did the port could not capture the display).

### Analysis pipeline fixes (2026-11-20)

| Problem | Fix | Effect |
|---|---|---|
| Stock Ghidra wraps real-mode near branches at *linear* 64 KB boundaries; segment-offset wrap (used by TLINK for >32 KB backward calls) was also missing | `ia_rel16.patch` (+`rmseg` context), `FixNear` | 183 wrong branch targets → 0 of 10,738 (checked by `branchcheck.py` on every run) |
| Switch jump tables not followed | `FixSwitch` | 45 tables, 304 case targets disassembled (e.g. the 33 FRPOS opcodes) |
| FixFPU patched `CD 3x` bytes inside operands reached by wrong flows and missed real sites (ovl2700, ~650 bytes) | `tools/fpupatch.py` linear sweep (skipping switch tables) before import; FixFPU kept only for KUR.EXE (2026-11-21) | image now identical to the independent sweep; 10,746 branches still 0 wrong |
| Bogus functions from wrong flows | `PruneFunctions` | 10 removed (2 had been named as "fragments") |
| Total | | resolved call sites 5,034 → 5,573; string refs 951 → 1,019; decompile failures 25 → 4 |
| RTL `__xcvt` (0000:0ADD, printf's float conversion) jumps through a 3-entry inline table with no `CMP` bound (0000:0B08: float / double / long double), so FixSwitch skipped it and `fpupatch.py` swept the table words as code, missing the emulator call `CD 3C` at 0B13 | FixSwitch form C "inline": the table right after the `JMP`, read up to the lowest case target, every entry past the table and mapped, else still reported (2026-11-23) | 46 tables / 307 case targets, 0 unresolved; `__xcvt` decoded in full (48 → 514 bytes); 10,787 branches, 0 wrong; fpupatch 2,022 → 2,023 sites |

No unresolved tables remain (2026-11-23).  Numbers of the run of that day, before → after the
`__xcvt` fix: call sites 5,593 → 5,596, string refs 1,021 → 1,022 (the new one is a false
positive: `MOV CX,4` at 0000:0C5B taken for DS:0004), decompile failures 3.  `fpupatch.py`
skips the table ranges of the *previous* run's `build/ist_switches.tsv`, so on a fresh `build/`
`tools/ghidra_run.sh` then runs the pipeline a second time by itself.
Caller lists written before this date (`analysis/names/*.md`) may miss callers that go
through switches or wrongly-resolved near calls; re-check against the current listings.

### Reference runtime

`C:\DOSBox-X\dosbox-x.exe -conf tools/dosbox/ist.conf` runs the original from `orig/`
(386, VGA, EMS, SB16). It was confirmed booting into the intro on 2026-11-20.
`tools/dosbox/oracle.py` scripts it through the debugger link (breakpoints, DGROUP/VGA/DAC dumps).
