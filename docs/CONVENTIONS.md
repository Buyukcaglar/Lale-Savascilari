# Geri derlenmiş kaynak kod için C kuralları / C conventions for the decompiled source

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

Bu kurallar, okunabilirliği korurken **1:1 davranışı** sürdürmek için vardır. Çeliştiklerinde
aslına uygunluk (fidelity) kazanır ve kod, nedenini açıklayan bir yorum taşır.

### 1. Her işlev izlenebilir

```c
/*
 * iso_apply_damage - bir savaşçının canından hasarı düşer ve ölümü ele alır.
 *
 * Original: IST.EXE ovl4595:1A2C (ISOSYS.L), far cdecl.
 * Verified: tests/iso_damage_test.py (unicorn karşılaştırmalı test), 2026-11-xx.
 */
int16_t iso_apply_damage(Combatant *c, int16_t dmg)
```

* Başlık yorumu; özgün adresi (düz görüntüde `seg:off`, bkz. `tools/README.md`), özgün modülü ve
  işlevin nasıl doğrulandığını verir.
* Açık olmayan deyimler, özgün kodun amacını ve yararlı olduğunda adresi veren kısa bir yorum
  taşır (`/* ovl4595:1A60 - clamps at 0, original uses jl not jle */`).

### 2. Tamsayı anlamı Borland'ınkidir

| Borland C++ 3.x | Taşıma (port) |
|---|---|
| `char` (işaretli) | `int8_t` ya da metin için `char` |
| `unsigned char` | `uint8_t` |
| `int`, `short` | `int16_t` |
| `unsigned` | `uint16_t` |
| `long` | `int32_t` |
| `unsigned long` | `uint32_t` |
| `float` / `double` / `long double` | `float` / `double` / bkz. §5 |

* Özgün veriler için asla düz `int` kullanmayın. Onu yalnızca taşamayan ve oyun durumuna geri
  beslenmeyen, ana makine (host) tarafındaki döngü sayaçları için kullanın.
* C'nin tamsayı yükseltmesi (integer promotion) farklıdır: özgün kod `int16_t a*b` ifadesini 16
  bitte hesaplayıp keser, C ise 32 bit `int`'e yükseltir. Bir ifade 16 biti taşabiliyorsa kesmeyi
  açıkça yazın: `(int16_t)(a * b)`. İşaretli 16 bit sarma (wrap) taşımada tanımlıdır: `-fwrapv`
  ile derliyoruz ve `int16_t`'ye daraltma her hedef derleyicide ikiye tümleyendir (two's
  complement).
* Negatif sayıların bölme ve kalan işlemleri her ikisinde de sıfıra doğru keser, yani bir
  değişiklik yoktur. Negatif değerlerin kaydırılması özgün kodda aritmetiktir (`sar`); taşıma
  işaretli türlerde `>>` kullanır, bu da clang/gcc/MSVC'de aritmetiktir. Mantıksal kaydırmalar
  işaretsiz türler kullanmalıdır.
* `long` çarpma/bölme/kaydırma yardımcıları (`N_LXMUL@`, `N_LDIV@`, …) düz `int32_t`
  aritmetiğine dönüşür; anlamları standarttır.

### 3. Bellek modeli

* **Uzak (far) işaretçiler yerel işaretçilere dönüşür.** Özgün kodun dayandığı yerlerde segment
  aritmetiği (bir segment içinde 16 bit konum sarması, huge işaretçi normalleştirmesi,
  `FP_SEG`/`FP_OFF` hileleri) bir yorumla birlikte açıkça yazılır.
* **EMS**, özgün MEMMAN API'sinin arkasındaki yığın (heap) bloklarıyla değiştirilir. Oyun *sayfa
  çerçevesi penceresi* anlamına (aynı anda yalnızca 4 × 16 KB eşlenmiştir) dayanıyorsa, örneğin
  eskimiş bir eşleme üzerinden yazarak, bu anlam korunur.
* **Oyun durumu ya da tablo olan DGROUP verileri**, yerleşimi özgün bayt yerleşimiyle aynı olan
  yapılara (`#pragma pack(push, 1)`) konur ve özgün DGROUP konumlarına karşı
  `_Static_assert(offsetof(...))` denetimleri yapılır. Böylece DOS oyunlarının sıkça yaptığı,
  bitişik genel değişkenlere taşan sınır dışı okumalar da aynı şekilde davranır.
  Uzak işaretçi genel değişkenleri (yalnızca çalışma zamanında, asla kaydedilmez) bu yapıların
  dışında durur.
* DGROUP tablolarının **ilk değerleri** bir araç tarafından `IST.EXE`'den üretilir
  (`src/generated/dgroup_init.c`), asla elle yazılmaz.
* Özgün kodun okuduğu **ilklendirilmemiş bellek** (BSS `c0` tarafından sıfırlanır; yığın
  sıfırlanmaz): taşıma, özgün kodun göreceği değeri (genellikle sıfır ya da DOSBox-X referansında
  (oracle) gözlenen değer) bir yorumla birlikte yeniden üretir.

### 4. Dosyalar ve veriler

* Tüm dosya biçimleri açık little-endian yardımcılarıyla (`rd16le`, `rd32le`, …) ayrıştırılır.
  Yapılar asla doğrudan `fread` ile okunmaz.
* Yollar: özgün kod geçerli dizindeki büyük harfli 8.3 adları kullanır. Taşıma bunları oyun veri
  klasörü içinde büyük/küçük harfe duyarsız olarak çözümler.
* Metin cp857'dir (Türkçe DOS kod sayfası) ve bayt olarak saklanır. UTF-8'e dönüşüm yalnızca ana
  makine sınırında (pencere başlığı, günlükler) yapılır, asla oyun mantığında yapılmaz.

### 5. Kayan nokta

* Özgün x87/öykünücü kodu 80 bit ara değerlerle çalışır. Her FP noktası gözden geçirilir. Bir
  `double` değerlendirmesi son bitte farklılaşabiliyor ve bu bir tamsayı sonucu besliyorsa
  (`(int)(x*k)`), şunlardan biri yapılır:
  - platformun onu 80 bit yaptığı yerlerde `long double` kullanmak (MSVC/arm64'te değil), ya da
  - özgün sonucu yeniden üreten kesin tamsayı/sabit noktalı (fixed-point) hesaba geçmek ve bunu
    girdi alanının tamamında eksiksiz doğrulamak, ya da
  - önceden hesaplanmış tabloyu bir referans dökümünden (oracle dump) yüklemek.
* **Referans FPU = x87 donanımı.** IST.EXE kayan nokta işlemlerini ya gerçek bir x87'de (486DX
  ve sonrası: öykünücü kesmeleri çalışma zamanında gerçek işlem kodlarına yamalanır) ya da
  yardımcı işlemci (coprocessor) yoksa, aşkın (transcendental) işlevleri son bitte
  farklılaşabilen Borland yazılım öykünücüsünde çalıştırır. Taşıma **x87** yolunu yeniden üretir;
  bu yüzden DOSBox-X referansı (`tools/dosbox/ist.conf`) `cputype=486`, `fpu=true` ile çalışır
  (2026-11-20'de değiştirildi; `386` ile oyunun `_8087` değeri 0'dı, yani öykünücü yolu
  kullanılıyordu). `src/game/q3d.c`, 80 bit x87 aritmetiğinin küçük bir yazılım modelini içerir;
  model 2 milyon rastgele işlemde x87'ye karşı bit düzeyinde birebir doğrulanmıştır; diğer FP
  noktaları için onu yeniden kullanın.
* Borland'ın `(int)` dönüşümü keser (RTL, `FIST` için yuvarlama kipini ayarlar), bu da C ile
  örtüşür.

### 6. Donanım erişimi

* VGA yazmaç ve bellek erişimi, özgün yazma sırasını yeniden üreterek `src/platform/vga.h`
  üzerinden geçer (`vga_out16(VGA_PORT_SEQ_INDEX, …)`).
* Donanım durumu üzerindeki meşgul bekleme (busy-wait) döngüleri (3DAh geri dönüşü, PIT sayaç
  okumaları), özgün zamanlama anlamını koruyan platform bekleme temel işlemlerine (primitives)
  yapılan çağrılara dönüşür.
* Kesme işleyicileri (zamanlayıcı, klavye), platformun özgün hızda ve ana döngüye göre özgün
  sırada çağırdığı geri çağırmalara (callbacks) dönüşür.

### 7. Adlandırma

* İşlev ve değişken adları `analysis/names.tsv`'yi izler (`iso_`, `s3d_`, `gr_`, … gibi modül
  önekleri). Özgün kodun açıkça kullandığı yerlerde Türkçe adlar korunur (`savas`, `kamp`,
  `sakla`) ve yorumda İngilizce bir açıklama alır.
* Türler `PascalCase` (`Combatant`, `GflHeader`), sabitler ve enum'lar `UPPER_CASE` kullanır.

## English

These rules exist to keep **1:1 behaviour** while staying readable. If they conflict,
fidelity wins, and the code carries a comment explaining why.

### 1. Every function is traceable

```c
/*
 * iso_apply_damage - subtract damage from a combatant and handle death.
 *
 * Original: IST.EXE ovl4595:1A2C (ISOSYS.L), far cdecl.
 * Verified: tests/iso_damage_test.py (unicorn differential), 2026-11-xx.
 */
int16_t iso_apply_damage(Combatant *c, int16_t dmg)
```

* The header comment gives the original address (`seg:off` in the flat image, see
  `tools/README.md`), the original module, and how the function was verified.
* Non-obvious statements carry a short comment with the original's intent, and the
  address when that helps (`/* ovl4595:1A60 - clamps at 0, original uses jl not jle */`).

### 2. Integer semantics are Borland's

| Borland C++ 3.x | Port |
|---|---|
| `char` (signed) | `int8_t` or `char` for text |
| `unsigned char` | `uint8_t` |
| `int`, `short` | `int16_t` |
| `unsigned` | `uint16_t` |
| `long` | `int32_t` |
| `unsigned long` | `uint32_t` |
| `float` / `double` / `long double` | `float` / `double` / see §5 |

* Never use plain `int` for original data. Use it only for host-side loop counters that
  can't overflow and don't feed back into game state.
* C's integer promotion differs: the original computes `int16_t a*b` in 16 bits and
  truncates, while C promotes to 32-bit `int`. When an expression can overflow 16 bits,
  write the truncation explicitly: `(int16_t)(a * b)`. Signed 16-bit wrap is defined
  in the port: we compile with `-fwrapv`, and narrowing to `int16_t` is two's complement on
  every target compiler.
* Division and modulus of negative numbers truncate toward zero in both, so there is no
  change. Shifts of negative values are arithmetic (`sar`) in the original; the port uses
  `>>` on signed types, which is arithmetic on clang/gcc/MSVC. Logical shifts must
  use unsigned types.
* `long` multiply/divide/shift helpers (`N_LXMUL@`, `N_LDIV@`, …) become plain `int32_t`
  arithmetic; their semantics are standard.

### 3. Memory model

* **Far pointers become native pointers.** Segment arithmetic is spelled out where
  the original depends on it (16-bit offset wrap inside a segment, huge-pointer
  normalisation, `FP_SEG`/`FP_OFF` tricks), with a comment.
* **EMS** is replaced by heap blocks behind the original MEMMAN API. The *page-frame
  window* semantics (only 4 × 16 KB mapped at a time) are kept if the game relies on
  them, for example by writing through a stale mapping.
* **DGROUP data that is game state or a table** goes into structs whose layout is the
  original's byte layout (`#pragma pack(push, 1)`), with `_Static_assert(offsetof(...))`
  checks against the original DGROUP offsets. Out-of-bounds reads of adjacent
  globals, which DOS games often do, then behave the same.
  Far-pointer globals (runtime-only, never saved) live outside these structs.
* **Initial values** of DGROUP tables are generated from `IST.EXE` by a tool
  (`src/generated/dgroup_init.c`), never typed by hand.
* **Uninitialised memory** the original reads (BSS is zeroed by `c0`; heap is not): the
  port reproduces whatever the original would have seen (usually zero, or the value
  observed in the DOSBox-X oracle), with a comment.

### 4. Files and data

* All file formats are parsed with explicit little-endian helpers (`rd16le`,
  `rd32le`, …). Structs are never `fread` directly.
* Paths: the original uses upper-case 8.3 names in the current directory. The port
  resolves them case-insensitively inside the game data folder.
* Text is cp857 (Turkish DOS code page), stored as bytes. Conversion to UTF-8 happens
  only at the host boundary (window title, logs), never in game logic.

### 5. Floating point

* The original's x87/emulator code runs with 80-bit intermediates. Each FP site is
  reviewed. If a `double` evaluation could differ in the last bit and that feeds an
  integer result (`(int)(x*k)`), either
  - use `long double` where the platform makes it 80-bit (not on MSVC/arm64), or
  - restructure to exact integer/fixed-point maths that reproduces the original result,
    verified exhaustively over its input domain, or
  - load the precomputed table from an oracle dump.
* **Reference FPU = x87 hardware.** IST.EXE runs its floating point either on a real
  x87 (486DX and later: the emulator interrupts are patched into real opcodes at run
  time) or, without a coprocessor, in Borland's software emulator, whose transcendental
  functions can differ in the last bit. The port reproduces the **x87** path; the
  DOSBox-X oracle (`tools/dosbox/ist.conf`) therefore runs `cputype=486`, `fpu=true`
  (changed 2026-11-20; with `386` the game's `_8087` was 0, i.e. the emulator path).
  `src/game/q3d.c` carries a small software model of 80-bit x87 arithmetic, verified
  bit-exact against x87 on 2 million random operations; reuse it for other FP sites.
* Borland's `(int)` conversion truncates (the RTL sets the rounding mode for `FIST`),
  which matches C.

### 6. Hardware access

* VGA register and memory access goes through `src/platform/vga.h`
  (`vga_out16(VGA_PORT_SEQ_INDEX, …)`), reproducing the original sequence of writes.
* Busy-wait loops on hardware state (3DAh retrace, PIT counter reads) become calls to
  platform wait primitives that keep the original timing semantics.
* Interrupt handlers (timer, keyboard) become callbacks the platform invokes at the
  original rate and in the original order relative to the main loop.

### 7. Naming

* Function and variable names follow `analysis/names.tsv` (module prefixes such as
  `iso_`, `s3d_`, `gr_`, …). Turkish names stay where the original clearly used them
  (`savas`, `kamp`, `sakla`), and get an English gloss in the comment.
* Types use `PascalCase` (`Combatant`, `GflHeader`), constants and enums use `UPPER_CASE`.
