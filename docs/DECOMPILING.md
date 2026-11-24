# Bir modül nasıl geri derlenir (çalışma kılavuzu) / How to decompile a module (working guide)

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Önce `docs/CONVENTIONS.md` (C kuralları) ve `docs/PLAN.md` §2'yi (ikili dosya bilgileri, modül
haritası) okuyun. Bu sayfa, pratik iş akışını ve C kodunun hedef aldığı çalışma zamanı modelini
anlatır. **1:1 davranış en yüksek önceliktir**; okunabilirlik ikinci, taşınabilirlik üçüncü
sıradadır.

### 1. Girdiler

| Ne | Nerede |
|---|---|
| Açıklamalı döküm (listing) (esas alınır) | `analysis/ghidra_ist/asm/<block>.asm` (`seg0C7F.asm`, `ovl4595.asm`, …) |
| Ghidra C taslağı (yalnızca ipucu - türler ve 16 bit anlamı çoğu zaman yanlıştır) | `analysis/ghidra_ist/<block>.c` |
| İşlev adları + notlar | `analysis/names.tsv` (adres, ad, `[module] [confidence] note`) ve `analysis/names/*.md` içindeki uzun notlar |
| Hangi DGROUP konumuna (offset) kim dokunuyor | `analysis/dgroup_refs.tsv` (yeniden üretmek için: `python tools/dgrefs.py`) |
| DGROUP içindeki dizgiler | `analysis/strings_dgroup.txt`, dizgi başvuruları `analysis/strrefs_ist.tsv` |
| Switch tabloları | `build/ist_switches.tsv` |
| Dosya biçimleri | `docs/FORMATS.md` |
| Veri dosyaları | `orig/` (özgün kurulum), `extracted/<ARCHIVE>/` (FLiB üyeleri) |

Adresler **düz** (flat) `SSSS:OOOO` biçimindedir (görüntü segment 0'a, bindirmeler (overlays)
2700 ve sonrasına yüklenmiş sayılır). Bindirme kodu DGROUP'a DS'nin yanı sıra ES üzerinden de
erişir - döküm her ikisini de açıklar (`; DAT_16e0_e74a`). `analysis/names/*.md` içindeki
çağıranlar (callers) listeleri 2026-11-20 iş hattı düzeltmelerinden eskidir; dökümü esas alın.

### 2. Taşımanın (port) çalışma zamanı modeli

#### DGROUP = `dg[]`
Tüm özgün genel (global) veriler, özgün konumlarında `uint8_t dg[0x10000]` içinde
(`src/engine/dgroup.h`) durur ve IST.EXE'den ilklendirilir (`src/gen/dgroup_init.c`,
`tools/gendgroup.py` tarafından üretilir). Adlandırılmış genel değişkenler, sahibi olan modülün
başlık dosyasında **makro** olarak tanımlanır:

```c
#define g_clock_ms      DG32(0xE74A)   /* u32  ms saati, her 100 Hz tıkta +10 (NKONTROL) */
#define g_mouse_shift   DG16(0x7325)   /* u16  fare x kaydırması (sürücünün x aralığı 640 ise 1) */
#define g_zone_rects    ((ZoneRect *)DGP(0xE5F5))   /* 41 x 8 bayt */
```

Erişimciler: `DG8 DGS8 DG16 DGS16 DG32 DGS32 DGF32 DGF64 DGP DGSTR`. dg[] üzerine bindirilen
yapılar `#pragma pack(push,1)` ile tanımlanmalı, boyut/konumları `_Static_assert` ile
denetlenmelidir. Bir ad tanımlamadan önce `grep -rn "0xE74A" src/` çalıştırın - konum için zaten
bir makro varsa onu kullanın (mevcut ad kötü olsa bile koruyun; yeniden adlandırmalar daha sonra
merkezi olarak yapılır). Bir değişken, onu kullanan en alt katmandaki modülün başlık dosyasında
tanımlanır.

**dg[] içinde olmayanlar:** yığına (heap), EMS çerçevesine, VGA belleğine ya da koda işaret eden
uzak (far) işaretçiler. Bunlar, sahibi olan modülde yerel C genel değişkenleridir
(`uint8_t *gr_work_palette; /* 16E0:E5EB far ptr */`). DGROUP'ta saklanan işlev işaretçileri
(geri çağırma (callback) tabloları) de yerel tablolara dönüşür. Özgün kod bir işaretçinin
segmenti/konumu üzerinde aritmetik yapıyorsa, eşdeğer yerel aritmetiği açıkça yazın ve yorumla
açıklayın.

#### Öykünülen PC: `src/platform/pc.h`
Donanım ve sürücü çağrıları argümanlarını ve sıralarını korur, ama `pc_*` işlevlerine gider:

| Özgün | Taşıma |
|---|---|
| `INT 33h` | `pc_int33(&regs)` |
| `INT 67h` (EMS) | `pc_int67(&regs)`, bellek taşıma (move) = `pc_ems_move()`; sayfa çerçevesi = `pc_ems_frame` (E000 segmenti) |
| `INT 10h AH=00h` | `vga_bios_set_mode(&pc_vga, mode)` |
| VGA portları | `vga_out8/out16/in8(&pc_vga, …)`; `in 3DAh` = `pc_in_3da()` |
| zincirli (chained) 13h kipinde A000:xxxx | `pc_a000[]` (tarama çıkışından (scan-out) önce VGA ile eşitlenir) |
| düzlemsel (planar) 12h kipinde A000 | `vga_mem_write8/read8(&pc_vga, 0xA0000 + off, …)` |
| PIT / INT 08h vektörü | `pc_pit_set_divisor`, `pc_set_int08` |
| meşgul bekleme (busy-wait) döngüleri (saat, kbhit, geri dönüş) | döngü gövdesi, sonunda `pc_poll()`'a varan bir şey çağırır - zamanlayıcı "kesmeleri" orada çalışır |
| `kbhit()` / `getch()` | `pc_kbhit()` / `pc_getch()` |
| dosya açma | `pc_fopen(dos_name, mode)` (büyük/küçük harfe duyarsız, oyun klasörü) |

Oyunun kurduğu kesme işleyicileri (zamanlayıcı rutinleri) sıradan C işlevlerine dönüşür; bunlar
`pc_poll()` içinden çağrılır. Özgün kodun bir ISR içinde çalıştırdığı kod `pc_poll()`'u kendisi
çağırmamalıdır.

Var olmayan bir `pc_*` hizmetine ihtiyacınız varsa (örn. EMS işlevi 4Eh), onu `pc.h/pc.c`'ye
**yalnızca iş paketiniz o kısmın size ait olduğunu söylüyorsa** ekleyin; aksi halde raporunuza
bir not düşün.

#### Borland RTL
Davranışın aynı olduğu yerlerde ana makinenin (host) C kütüphanesini kullanın (`memcpy`,
`strlen`, `strcmp`, ASCII üzerinde `toupper`, genişlik belirtilmiş tamsayı `sprintf` biçimleri
`%d %u %x %s %c`). Davranışa duyarlı parçalar, bir karşılaştırmalı testle (differential test)
birlikte `src/rtl/` altına gider (örnek: `src/rtl/bc_rand.c`). Bilinen tuzaklar: `random(n)` →
`bc_random(n)`; Borland `max()` iki kez değerlendirir; `%f/%e/%g` biçimlendirmesi; `itoa/ltoa`;
cp857 üzerinde `stricmp`; `atoi` taşması; `farcoreleft()` değerleri (oyunun ekrana yazdığı ya da
karar verirken kullandığı). Yığın: `farmalloc/farfree` → `malloc/free`; özgün kod **kendi
ilklendirmediği belleği okuduğunda**, onun göreceği değeri yeniden üretin (genellikle 0 - bunu
bir yorumda belirtin).

#### Türler ve çağırma kuralları (calling conventions)
`int`=`int16_t`, `long`=`int32_t`, `char` işaretli. Uzak (far) cdecl işlevler, aynı parametre
sırasıyla sıradan C işlevlerine dönüşür; Pascal kuralını kullanan işlevler (DSMI,
`o5646_*`/`o569A_*` dosya katmanı) *mantıksal* parametre sıralarını korur (ilk bildirilen önce
gelir). `DX:AX` dönüşü → `int32_t`/`uint32_t`. Özgün kodun taşabildiği her yerde 16 bit sarmayı
(wrap-around) açıkça koruyun (`(int16_t)(a * b)`, `uint16_t` indisler).

### 3. Size ait dosyalar

* Bir iş paketi = sabit bir özgün blok/işlev listesi ve `src/<area>/<module>.c/.h` dosyaları
  (+ `tests/py/test_<module>.py`). Alanlar: `engine/` (ALTDUZEY), `game/`, `dsmi/`, `rtl/`.
* Diğer modüllerin dosyalarını, `CMakeLists.txt`'yi, `tests/base_sources.txt`'yi,
  `docs/PROGRESS.md`'yi ya da analiz çıktılarını **düzenlemeyin**. Ortak test altyapısı
  (`tools/emu86.py`, `tests/py/lale.py`, `tests/py/run.py`): yalnızca küçük, ekleme niteliğinde
  yardımcılar, dosyanın sonuna eklenerek; düzenlemeden hemen önce dosyayı yeniden okuyun
  (başkaları da düzenliyor olabilir).
* Her işlev, CONVENTIONS §1'deki başlık yorumunu özgün adresiyle birlikte alır. İşlevleri dosya
  içinde özgün adres sırasında tutun.
* Ölü/başvurulmayan işlevler: onları da geri derleyin (`/* unreferenced */` ile işaretleyin).

### 4. Doğrulama

Bitmiş modüllerle sizin dosyalarınızı içeren özel bir test kütüphanesi derleyin - bu kütüphane
başkalarının yarım kalmış işini hiçbir zaman görmez:

```sh
tools/testlib.sh out/wp-<name> src/engine/foo.c src/engine/bar.c
python tests/py/run.py out/wp-<name>/lale_testlib.dll test_foo
```

`tests/py/test_<module>.py` dosyasını yazın (bkz. `test_fileman.py`, `test_rtl.py`). Kalıp:

```python
from lale import Emu, NAMES, check, fn, dg_from_emu, dg_diff, lib
e = Emu()                       # özgün görüntü 0824h'ye yerleştirilmiş (DOSBox-X'teki gibi)
e.stub_heap()                   # farmalloc ve benzerleri geçici bir yığından
e.attach_pc(lib)                # INT 33h/67h/10h + VGA portları -> taşımanın pc katmanı
seg = e.alloc(n)                # geçici arabellek (çalışma zamanı segmenti, konum 0)
ax, dx = e.call(NAMES['fm_cmp_decode'], e.far(seg, 0), 5, ...)
e.stub('0AC6:0430', lambda e, arg: (0, 0))   # test etmediğiniz bir çağrılan işlevin yerine geçer
dg_from_emu(e); <call the C>; check(not dg_diff(e, [(lo, hi)]), ...)
```

* Girdiler: mümkün olan her yerde `orig/`/`extracted/` içinden gerçek veriler, artı rastgele ve
  uç durumlar (edge cases). Dönüş değerlerini, çıktı arabelleklerini ve işlevin yazdığı DGROUP
  aralıklarını (`dg_diff`) karşılaştırın. Özgün ve C çalıştırmaları arasında durumu sıfırlayın
  (`lib.pc_reset()`, `lale.fn('dg_reset', None)()`).
* `Emu(snapshot='main')`, DGROUP + IVT'yi `main()` anında yakalanmış DOSBox-X durumundan başlatır
  (`build/oracle/MAIN_MEM.BIN`, `tools/dosbox/capture_boot.py`).
* Tek başına çalıştırılamayan işlevler (derin arayüz döngüleri, bütün oyun ekranları) daha sonra
  DOSBox-X'e karşı doğrulanır (`tools/dosbox/oracle.py`); bunları `Verified: no (reason)` olarak
  işaretleyin ve onun yerine parçalarını test edin.
* Son bitte farklılaşan bir x87 sonucu gerçek bir farktır: bkz. CONVENTIONS §5.
* Düzeneğin (harness) kendisi de hatalı olabilir (yenidir). Özgün kod öykünücüde yanlış
  davranıyorsa, C kodunu ona uyacak şekilde "düzeltmeden" önce düzeneği kontrol edin.

### 5. Rapor

İş bittiğinde şunları raporlayın: geri derlenen işlevler (sayı/toplam), hangilerinin
karşılaştırmalı testle doğrulandığı, hangilerinin doğrulanmadığı ve nedeni, başkalarından
ihtiyaç duyduğunuz `pc_*`/RTL eklemeleri, tanımladığınız DGROUP adları (konum → ad) ve özgün
kodda şaşırtıcı bulduğunuz her şey (hatalar, taşımanın koruması gereken tuhaflıklar). Kısa tutun.

## English

Read `docs/CONVENTIONS.md` (C rules) and `docs/PLAN.md` §2 (binary facts, module map)
first. This page is the practical workflow and the runtime model the C code targets.
**1:1 behaviour is the top priority**; readability comes second, portability third.

### 1. Inputs

| What | Where |
|---|---|
| Annotated listing (authoritative) | `analysis/ghidra_ist/asm/<block>.asm` (`seg0C7F.asm`, `ovl4595.asm`, …) |
| Ghidra C draft (a hint only - types and 16-bit semantics are often wrong) | `analysis/ghidra_ist/<block>.c` |
| Function names + notes | `analysis/names.tsv` (address, name, `[module] [confidence] note`) and the long notes in `analysis/names/*.md` |
| Who touches which DGROUP offset | `analysis/dgroup_refs.tsv` (regenerate: `python tools/dgrefs.py`) |
| Strings in DGROUP | `analysis/strings_dgroup.txt`, string refs `analysis/strrefs_ist.tsv` |
| Switch tables | `build/ist_switches.tsv` |
| File formats | `docs/FORMATS.md` |
| Data files | `orig/` (original install), `extracted/<ARCHIVE>/` (FLiB members) |

Addresses are **flat** `SSSS:OOOO` (image loaded at segment 0, overlays at 2700+).
Overlay code reaches DGROUP through ES as well as DS - the listing annotates both
(`; DAT_16e0_e74a`). Callers lists in `analysis/names/*.md` predate the 2026-11-20
pipeline fixes; trust the listing.

### 2. Runtime model of the port

#### DGROUP = `dg[]`
All original global data lives in `uint8_t dg[0x10000]` (`src/engine/dgroup.h`) at its
original offsets, initialised from IST.EXE (`src/gen/dgroup_init.c`, generated by
`tools/gendgroup.py`). Named globals are **macros** in the owning module's header:

```c
#define g_clock_ms      DG32(0xE74A)   /* u32  ms clock, +10 per 100 Hz tick (NKONTROL) */
#define g_mouse_shift   DG16(0x7325)   /* u16  mouse x shift (1 if driver x range is 640) */
#define g_zone_rects    ((ZoneRect *)DGP(0xE5F5))   /* 41 x 8 bytes */
```

Accessors: `DG8 DGS8 DG16 DGS16 DG32 DGS32 DGF32 DGF64 DGP DGSTR`. Structs overlaid on
dg[] must be `#pragma pack(push,1)` with `_Static_assert` on size/offsets.
Before defining a name, `grep -rn "0xE74A" src/` - if the offset already has a macro,
use it (if the existing name is poor, keep it; renames are done centrally later).
A variable is defined in the header of the lowest-layer module that uses it.

**Not in dg[]:** far pointers to heap, EMS frame, VGA memory or code. Those are native
C globals (`uint8_t *gr_work_palette; /* 16E0:E5EB far ptr */`) in the owning module.
Function pointers stored in DGROUP (callback tables) likewise become native tables.
If original code does arithmetic on a pointer's segment/offset, spell out the
equivalent native arithmetic and comment it.

#### The emulated PC: `src/platform/pc.h`
Hardware and driver calls keep their arguments and order but go to `pc_*`:

| Original | Port |
|---|---|
| `INT 33h` | `pc_int33(&regs)` |
| `INT 67h` (EMS) | `pc_int67(&regs)`, move = `pc_ems_move()`; page frame = `pc_ems_frame` (the E000 segment) |
| `INT 10h AH=00h` | `vga_bios_set_mode(&pc_vga, mode)` |
| VGA ports | `vga_out8/out16/in8(&pc_vga, …)`; `in 3DAh` = `pc_in_3da()` |
| A000:xxxx in chained mode 13h | `pc_a000[]` (synced to the VGA before scan-out) |
| A000 in planar mode 12h | `vga_mem_write8/read8(&pc_vga, 0xA0000 + off, …)` |
| PIT / INT 08h vector | `pc_pit_set_divisor`, `pc_set_int08` |
| busy-wait loops (clock, kbhit, retrace) | the loop body calls something that ends in `pc_poll()` - that is where timer "interrupts" run |
| `kbhit()` / `getch()` | `pc_kbhit()` / `pc_getch()` |
| file open | `pc_fopen(dos_name, mode)` (case-insensitive, game folder) |

Interrupt handlers the game installs (timer routines) become plain C functions; they
are invoked from `pc_poll()`. Code the original ran inside an ISR must not call
`pc_poll()` itself.

If you need a `pc_*` service that does not exist (e.g. EMS function 4Eh), add it to
`pc.h/pc.c` **only if your work package says you own that part**; otherwise put a
note in your report.

#### Borland RTL
Use the host C library where behaviour is identical (`memcpy`, `strlen`, `strcmp`,
`toupper` on ASCII, integer `sprintf` formats `%d %u %x %s %c` with widths).
Behaviour-sensitive pieces go to `src/rtl/` with a differential test (example:
`src/rtl/bc_rand.c`). Known traps: `random(n)` → `bc_random(n)`; Borland `max()`
evaluates twice; `%f/%e/%g` formatting; `itoa/ltoa`; `stricmp` on cp857; `atoi`
overflow; `farcoreleft()` values (what the game prints/decides on).
Heap: `farmalloc/farfree` → `malloc/free`; when the original **reads memory it did
not initialise**, reproduce the value it would see (usually 0 - say so in a comment).

#### Types and calling conventions
`int`=`int16_t`, `long`=`int32_t`, `char` signed. Far cdecl functions become ordinary
C functions with the same parameter order; Pascal-convention functions (DSMI, the
`o5646_*`/`o569A_*` file layer) keep their *logical* parameter order (first declared
first). Return `DX:AX` → `int32_t`/`uint32_t`. Keep 16-bit wrap-around explicitly
(`(int16_t)(a * b)`, `uint16_t` indices) wherever the original can overflow.

### 3. Files you own

* One work package = a fixed list of original blocks/functions and the files
  `src/<area>/<module>.c/.h` (+ `tests/py/test_<module>.py`). Areas: `engine/`
  (ALTDUZEY), `game/`, `dsmi/`, `rtl/`.
* Do **not** edit files of other modules, `CMakeLists.txt`, `tests/base_sources.txt`,
  `docs/PROGRESS.md` or the analysis outputs. Shared test infra (`tools/emu86.py`,
  `tests/py/lale.py`, `tests/py/run.py`): only small additive helpers, appended,
  re-read the file right before editing (others may be editing too).
* Every function gets the header comment of CONVENTIONS §1 with its original
  address. Keep functions in original address order inside the file.
* Dead/unreferenced functions: decompile them too (mark `/* unreferenced */`).

### 4. Verifying

Build a private test library containing the finished modules plus your files - this
never sees other people's half-finished work:

```sh
tools/testlib.sh out/wp-<name> src/engine/foo.c src/engine/bar.c
python tests/py/run.py out/wp-<name>/lale_testlib.dll test_foo
```

Write `tests/py/test_<module>.py` (see `test_fileman.py`, `test_rtl.py`). Pattern:

```python
from lale import Emu, NAMES, check, fn, dg_from_emu, dg_diff, lib
e = Emu()                       # original image relocated at 0824h (as DOSBox-X)
e.stub_heap()                   # farmalloc & co. from a scratch heap
e.attach_pc(lib)                # INT 33h/67h/10h + VGA ports -> the port's pc layer
seg = e.alloc(n)                # scratch buffer (runtime segment, offset 0)
ax, dx = e.call(NAMES['fm_cmp_decode'], e.far(seg, 0), 5, ...)
e.stub('0AC6:0430', lambda e, arg: (0, 0))   # replace a callee you don't test
dg_from_emu(e); <call the C>; check(not dg_diff(e, [(lo, hi)]), ...)
```

* Inputs: real data from `orig/`/`extracted/` wherever possible, plus random and edge
  cases. Compare return values, output buffers and the DGROUP ranges the function
  writes (`dg_diff`). Reset state between original and C runs (`lib.pc_reset()`,
  `lale.fn('dg_reset', None)()`).
* `Emu(snapshot='main')` seeds DGROUP + IVT from the DOSBox-X state captured at
  `main()` (`build/oracle/MAIN_MEM.BIN`, `tools/dosbox/capture_boot.py`).
* Functions that cannot run in isolation (deep UI loops, whole game screens) are
  verified later against DOSBox-X (`tools/dosbox/oracle.py`); mark them
  `Verified: no (reason)` and test their pieces instead.
* An x87 result that differs in the last bit is a real difference: see CONVENTIONS §5.
* The harness itself can be wrong (it is new). If the original misbehaves in the
  emulator, check the harness before "fixing" the C to match.

### 5. Report

When done, report: functions decompiled (count/total), which are verified by a
differential test, which are not and why, any `pc_*`/RTL additions you need from
others, DGROUP names you defined (offset → name), and anything surprising in the
original (bugs, quirks the port must keep). Keep it short.
