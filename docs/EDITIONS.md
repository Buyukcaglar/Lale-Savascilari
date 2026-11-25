# İki baskı / The two editions

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

`IST.EXE`'nin iki derlemesi (build) vardır ve port bunlardan herhangi biri gibi davranır
(`g_edition`, `src/engine/edition.h`):

| | CD baskısı | 2005 baskısı |
|---|---|---|
| `IST.EXE` | 1996-11-10, 384,560 bayt, oyun CD'sinde (redump.org 91232) | 2005-02-08, 385,328 bayt, bir klasörden çalışan yeniden yayım |
| Gereksinim | MSCDEX ve sürücüde disk | kendi klasöründen başka hiçbir şey |
| Videolar | her birinden önce diski denetler | denetim yok |
| Giriş, menü ve hikâyedeki müzik | CD ses izleri (audio track) 2 ve 3 | `DEMO.AMF` modülü |
| Veri dosyaları | bayt bayt aynı (`*.CKD`, `TRDOS.DAT`, `GRPHAZIR.DAT`, `*.DSD`, …) | |

Portun kodu 2005 derlemesinden geri derlendi (decompiled); `tools/exediff.py` iki
çalıştırılabilir dosyayı yordam yordam karşılaştırır. Şunlar dışında ikisi aynı programdır:

* **`main`** (`src/game/istanbul.c`). CD derlemesi 3D5C bayrağını 1 yapar, `o5737_cd_init()`'i
  çağırır - MSCDEX yoksa ölümcül hata (varsa "CD sürücü tanındı…") - ve diski bekler
  (`o5737_cd_vtoc_ok()` üç kez; olmazsa bir tuşa basılana kadar "CD'yi takın"; Esc çıkar). 2005
  derlemesinde bu yerde `mov es:[3D5C],0` bulunur ve `DEMO.AMF` daha sonra yüklenir.
  3D5C = 1 iken FRPOS video işlem kodu (opcode) her videodan önce diski ister.
* **DEMOLAR'ın iki yordamı** (`src/game/demolar.c`): giriş/menü ve hikâye sahnesi. CD
  derlemesinde orada yüklenecek, karartılacak ya da serbest bırakılacak bir modül yoktur; onun
  yerine CD iz 2 veya 3 çalar. 2005 derlemesi de aynı `o5737_cd_play_track()` çağrılarını yapar,
  ama CD modülü hiç ilklenmediği için bu çağrılar hemen döner.
* **`ctl_flush_keys`** (`src/engine/nkontrol.c`): CD derlemesinde INT 21h AX=0C00h, 2005
  derlemesinde bir `kbhit`/`getch` döngüsü.
* **Farklı kaynak satırlarından doğanlar**: derlemelerin hata yordamlarına geçirdiği satır
  numaraları ve dosya adları (`ED_LINE`, `ED_FILE`; CD derlemesi `T:` sürücüsünden
  derlenmiştir) ve yalnızca CD derlemesinde bulunan birkaç dizgi.

Geri derlenmiş kod `ED_CD`'yi tam olarak bu yerlerde sorgular; geri kalan her şey ortaktır.
Veri segmenti yansısı (mirror) her ikisi için de 2005 derlemesininkidir.

### Hangisi çalışır

`edition = auto` (varsayılan), oyun ses izleri olan bir diskten (`DATA/LALE.CHD`) içe
aktarıldıysa CD baskısını, dosya olarak içe aktarıldıysa 2005 baskısını seçer. `--edition cd`
ya da `--edition 2005` (`lale.ini` içinde `edition`) mümkün olduğunda başka türlü karar verir:
2005 baskısı CHD'den de çalışır - veri dosyalarını diskte bulur - CD baskısı ise bir disk ister.
Ses izleri olmayan bir disk de olur; o zaman CD iz 2 ve 3 bulunamaz ve giriş, menü ve hikâye
müziksiz kalır. Level dergisinin Ocak 1998 kapak CD'si böyledir: üzerinde aynı 1996 `IST.EXE`
vardır, ama ses izleri yoktur (bkz. [DISC.md](DISC.md#level-dergisinin-cdsi-_referencescd_level)).
`auto` böyle bir disk için müzikli 2005 baskısını seçer; `--edition cd`, o diskteki özgün
programın davranışını verir. Bkz. [INSTALL.md](INSTALL.md).

CD baskısında disk `DATA/LALE.CHD`'dir ve sanal MSCDEX'in D: sürücüsü olarak bağlanır
(`src/platform/cdrom.c`, `chd.c`); oyunun kendi CD modülü (`src/game/cd5737.c`, 1:1 geri
derlenmiş) onunla PC'dekiyle aynı aygıt istekleri üzerinden konuşur.

Kayıtlı oyunlar, `GRP.DAT` ve `SES.CFG` her iki baskıda da `SAVE/` içinde tutulur ve iki baskı
arasında birbirinin yerine kullanılabilir.

## English

Two builds of `IST.EXE` exist, and the port behaves as either (`g_edition`,
`src/engine/edition.h`):

| | CD edition | 2005 edition |
|---|---|---|
| `IST.EXE` | 1996-11-10, 384,560 bytes, on the game CD (redump.org 91232) | 2005-02-08, 385,328 bytes, the re-release that runs from a folder |
| Needs | MSCDEX and the disc in the drive | nothing but its folder |
| Videos | checks for the disc before each one | no check |
| Music in the intro, menu and story | CD audio tracks 2 and 3 | the module `DEMO.AMF` |
| Data files | the same, byte for byte (`*.CKD`, `TRDOS.DAT`, `GRPHAZIR.DAT`, `*.DSD`, …) | |

The port's code was decompiled from the 2005 build; `tools/exediff.py` compares the two
executables routine by routine. They are the same program except for:

* **`main`** (`src/game/istanbul.c`). The CD build sets the flag 3D5C to 1, calls
  `o5737_cd_init()` - fatal without MSCDEX ("CD sürücü tanındı…" otherwise) - and waits for the
  disc (`o5737_cd_vtoc_ok()` three times, else "insert the CD" until a key; Esc quits). The 2005
  build has `mov es:[3D5C],0` in that place and loads `DEMO.AMF` later on. With 3D5C = 1 the
  FRPOS video opcode asks for the disc before every video.
* **Two routines of DEMOLAR** (`src/game/demolar.c`): the intro/menu and the story scene. The CD
  build has no module to load, fade or free there; CD track 2 or 3 plays instead. The 2005 build
  still makes the same `o5737_cd_play_track()` calls, but its CD module is never initialised and
  they return at once.
* **`ctl_flush_keys`** (`src/engine/nkontrol.c`): INT 21h AX=0C00h in the CD build, a
  `kbhit`/`getch` loop in the 2005 build.
* **What follows from the different source lines**: the line numbers and file names the builds
  pass to their error routine (`ED_LINE`, `ED_FILE`; the CD build was compiled from drive `T:`),
  and a few strings only the CD build has.

The decompiled code asks `ED_CD` at exactly these places; everything else is shared. The data
segment mirror is the 2005 build's for both.

### Which one runs

`edition = auto` (the default) takes the CD edition when the game was imported from a disc with
audio tracks (`DATA/LALE.CHD`), the 2005 edition when it was imported as files. `--edition cd`
or `--edition 2005` (`edition` in `lale.ini`) decides otherwise where that is possible: the 2005
edition runs from the CHD as well - it finds its data files on the disc - while the CD edition
needs a disc. One without audio tracks will do; CD tracks 2 and 3 are then not found, and the
intro, menu and story have no music. The cover CD of the Level magazine, January 1998, is such a
disc: it carries the same 1996 `IST.EXE`, but no audio tracks (see
[DISC.md](DISC.md#the-level-magazine-cd-_referencescd_level)). `auto` takes the 2005 edition,
with its music, for such a disc; `--edition cd` gives the behaviour of the original program on
it. See [INSTALL.md](INSTALL.md).

In the CD edition the disc is `DATA/LALE.CHD`, mounted as drive D: of the virtual MSCDEX
(`src/platform/cdrom.c`, `chd.c`); the game's own CD module (`src/game/cd5737.c`, decompiled
1:1) talks to it with the same device requests as on the PC.

Saved games, `GRP.DAT` and `SES.CFG` are kept in `SAVE/` in both editions and are
interchangeable between them.
