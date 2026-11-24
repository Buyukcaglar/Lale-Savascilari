# DSMI MCP v3.5 yazılım karıştırıcısı (SDISB16.DSD) / DSMI MCP v3.5 software mixer (SDISB16.DSD)

**Dil / Language:** [Türkçe](#türkçe) · [English](#english)

## Türkçe

`src/dsmi/mcp.c` / `mcp.h` olarak geri derlendi (decompiled); karşılaştırmalı test
(differential test) `tests/py/test_mcp.py` (özgün sürücü `tools/dsdload.py` aracılığıyla
Unicorn içinde çalışır). Adresler SDISB16.DSD imaj ofsetleridir; `d:XXXX` sürücünün veri
segmentidir (imaj +3D30h). SDIPAS16/SDIWSS16 aynı MCP'yi içerir ("MCP v3.5 - (C) 1992,1995
Otto Chrons"); SDIGUS16 donanımda karıştırır.

### Yapı

| İmaj | Ne |
|---|---|
| 0000 | `JMP FAR dsd_main` |
| 001C–00B1 | mcpOpenDDS / mcpCloseDDS (içe aktarımlar, MCPOutputBuffer kaynağı) |
| 00BC–147B | SB/SB Pro/SB16 sürücüsü: BLASTER ayrıştırma, algılama, DSP, IRQ işleyicileri (yalnızca onay verir), initDevice, start/stop/close |
| 147C | dsd_main (DDS giriş noktası) |
| 1675–36EF | **MCP**: tablolar, iç karıştırıcılar 181E–21D8, karıştırma sürücüsü 2210–2833, zaman → kare 2834, başlatma 2A2E, tablolar 2BB7–2D2B, setupChannels 2D2C, başlat/durdur 2DEA/2E3E, CDI yuvaları 2E63–36B1, komut kuyruğu 2FC7–3081, poll 3082 |
| 36F8–3D2F | 8237 DMA yardımcıları, uzun çarpma/bölme yardımcıları (3C20/3C95) |

FPU kodu yoktur. IRQ işleyicileri karıştırma yapmaz: tüm karıştırma **poll** içinde olur.

### Yapılandırma (initDevice 08F6 → mcpInit 2A2E)

`mcp_config_from_sdi()`; kart türü, örnekleme hızı, stereo, örnek boyu ve yükseltme SDI
kaydından gelir (docs/FORMATS.md, SES.CFG). Seçenek sözcüğü: bit0 biriktirme (accumulate),
bit 4–5 yükseltme (amplification) 100/200/350/500 %, bit6 otomatik yükseltme (kanal sayısına
göre: 70, 80, 90, 100, 100, 120 … 500 %). 8 bitlik çıkış (SB, SB Pro) her zaman biriktirir.
Halka (ring) boyu = (2000 × örnek başına bayt << stereo) × hız / 22000, 16'nın katına aşağı
yuvarlanır.

**Dağıtılan SES.CFG**: SB16, 22050 Hz, stereo, işaretli 16 bit, yükseltme yok →
seçenekler 0 ("doğrudan" (direct) kip), halka 8016 bayt = 2004 kare (frame) ≈ 91 ms.

### Kanal kaydı (d:0A65 + ch·28h, 32 kanal)

+00 bayraklar (bit0 çalıyor, bit1 döngüye girdi, bit2 sessiz, bit3 örnek atandı), +02 uzak
(far) örnek işaretçisi, +06 uzunluk, +0A adım (9.23 sabit noktalı: freq·2²³/rate), +0E konum,
+12 döngü başı, +16 döngü sonu (0 = döngü yok – SAMPLEINFO.mode içindeki döngü bayrağı yalnızca
setSample sırasında önemlidir), +1A ses düzeyi 0..32, +1B frekans, +1F/+21 16.16 biçiminde
adım (freq/rate, (freq%rate)·65536/rate), +23 konumun kesir kısmı, +25 sampleID (sanal/EMS
örneği), +27 pan (panning).

### Komut kuyruğu ve poll (zamanlama modeli)

setSample, playSample, setVolume, setFrequency, setPosition ve setPanning, `GetDSMITime()`
(0.1 ms birimleri; IST.EXE onu her 10 ms'de 100 artırır) ile birlikte 350 girdilik bir halkaya
**kuyruğa alınır** (taşma denetimi yok). setSample, SAMPLEINFO başlığını sırayla (round-robin)
kullanılan 32 yuvadan birine kopyalar (0, 31, 30, …). stop, mute, unmute, mute/unmuteAll,
setMasterVolume ve setupChannels **hemen** etki eder. Sonuç: aynı poll aralığında `play`'den
sonra gelen `stop`, kuyruktaki play yürütüldüğünde geri alınmış olur.

`poll` (3082; IST.EXE onu 40 Hz'lik zamanlayıcı yordamı 0D42:0093'ten çağırır):

1. `avail` = yazma konumu ile DMA konumu arasındaki süre (SDIGetBufferPosition):
   `((dma − mixpos) or + ring) / bytes_per_frame · 10000 / rate`.
2. `now − last ≥ 120` olmadıkça hiçbir şey yapmaz (ve saatini korur); 100 Hz'lik saatle bu
   fiilen her 2–3 tıkta (tick) bir demektir.
3. Yazma indisinin bir anlık görüntüsüne (snapshot) kadar kuyruktaki her komut için: `dt` =
   onun zamanı − bir öncekinin zamanı (ilki için: − `last`). `avail −= dt`; bu negatife düşerse
   kalan tüm komutlar karıştırma yapılmadan uygulanır, **sonuncusu iki kez uygulanır** ve DMA
   konumuna kadar kalan kısım karıştırılır. Aksi halde `mix(dt)` (`dt = 0` için atlanır), uygula.
4. DMA konumuna kadar kalanı karıştır; `last = now`.

`mix(dt)` (2834): `bytes = rate·dt/10000·bpf`; 16 bayttan azsa hiçbir şey karıştırılmaz (bu
süre yazma konumu için kaybolur, bir sonraki "kalan" karıştırması açığı kapatır); halka boyuna
göre modülo alınır; kare = `((bytes − 1) & ~15) / bpf`; halkanın sonunu aşan bir blok bölünür
(ilk parça yuvarlanmaz); 8 kareden az bir kalan karıştırılmaz.

Karıştırıcı DMA'nın **arkasına** yazar (kendi konumu ile çalma konumu arasına), bu yüzden
karıştırılan veri bir halka turu sonra duyulur. Bit düzeyinde aynı sesi elde etmek için port
aynı komutları aynı DSMI zamanlarında vermeli ve aynı zamanlarda, aynı DMA konumlarıyla poll
etmelidir; o zaman `mcp_dma_buffer` aynı baytları tutar ve ses arka ucu (audio backend) onu
DMA'nın yapacağı gibi çalar (`mcp_dma_position`'ı ilerleterek).

### Karıştırma (2210 → 2276 → iç döngüler)

Kanal başına (çalmıyorsa veya sessizdeyse atlanır; sanal örnekler önce
`MCPSampleRealAddress(handle, pos)`'u sorar ve dönen işaretçiyi kullanır):

* `end`'den (= döngü sonu veya uzunluk) önce sığan kareler: `rem = end − 1 − pos`, gereken
  `= step·n >> 23`; yetmezse `n = (rem << 23) / step` ve çalıyor bayrağı temizlenir (döngüde:
  bit1 ile yeniden kurulur) – bölmeye 9.23 adım karar verir, ilerletmeyi 16.16 adım yapar.
* Döngüde konum = **tam olarak** döngü başı (aşan kısım atılır; kesir korunur); döngü kalan
  kareler için yinelenir.
* Kaynak işaretçisi: `seg:(off + pos)`, yalnızca ofset FFFFh'yi aşınca normalleştirilir; bir
  blok içinde 16 bitlik `si` **sarar** (wraps), bu yüzden 64 KB sınırını aşan bir blok 64 KB
  önceki veriyi yeniden okur (`mcp_far_offset_hook` ile yeniden üretilir).
* İç döngü (8 kez açılmış, Duff's device): `s = byte[si]`; `frac += step_frac`;
  `si += step_int + carry`; `out += table[vol][s]`. Örnekler **işaretsiz 8 bittir**
  (indis 0 = −128). **Ara değerleme (interpolation) yok.**
* Ses düzeyi indisi = `(vol·master >> 6) − 1` (0..31; ses düzeyi 0 → kanal atlanır ama
  ilerletilir). 31 ve 32 ses düzeyleri aynı seviyeyi verir. Ana ses düzeyi 0..64 (başta 64).
* Pan (yalnız stereo): |pan| < 3 orta (iki tarafa aynı değer); ≥ 63 / ≤ −63 yalnız bir taraf,
  tam seviye; 100 = surround (sol +v, sağ −v); aksi halde uzak tarafın indisi
  `(63 − |pan|)·idx >> 6`.
* Tablolar (setupChannels yeniden kurar): `vt[i] = (i+1)·amp/100` (i = 30, 31 ikisi de 31);
  16 bitlik kipler: `((s << 8)·vt / channels) >> 5` (doğrudan kipte word, biriktirirken
  dword); 8 bit: `(s·vt) >> 5` word'leri artı tab:6000'de 2×4096 baytlık bir çıkış tablosu
  (`min(i·127/div[channels], 127) + 80h`, aynalanmış).
* Doğrudan kip (dağıtılan yapılandırma): kanallar **DMA halkasına 16 bitlik word'ler olarak,
  taşınca sararak toplanır – kırpma (clipping) yok**. Kanal sayısına bölme sayesinde bu nadiren
  taşar.
* Biriktirme kipleri: 2000 (mono) / 1000 (stereo) karelik bloklar tab:4000'e (word) veya
  tab:8000'e (dword), ardından tablo üzerinden 8 bit (indis 64 KB'lık segment içinde sarar)
  ya da −32768..32767 aralığına kırpılmış 16 bit.

### Portun koruduğu tuhaflıklar

* Sessiz kanal (ses düzeyi indisi < 0): konum `step·n >> 16` kadar ilerler ama kesrin üzerine
  bloğun **bayt sayısının** düşük word'ü yazılır (AX'in yeniden kullanımı).
* Konumu zaten `end − 1` olan döngüsüz bir örnek sonsuza dek "çalıyor" kalır (sessizce).
* `loopstart ≥ loopend − 1` özgün programı zamanlayıcı kesmesi içinde kilitler (port 65536
  yinelemeden sonra durur).
* setPosition yalnızca çalma sırasında işe yarar; `pos ≥ length` uzunluğa kırpılır ve kesir
  korunur.
* Her yuvanın kanal denetimi kanal numarasının yalnızca düşük word'ünü karşılaştırır.
* setMasterVolume her zaman FFFFh döndürür; muteAll/unmuteAll kanal yokken −1 döndürür;
  kuyruğa alınan çağrıların çoğu özgün programda anlamsız değer döndürür (port 0 döndürür).
* setLinearRate, download, unload, unloadAll hiçbir şey yapmaz.
* Bir örneğin sonundan bir bayt öteye okuma olabilir (16.16 adım ile 9.23 bölme arasındaki
  fark); port örneğin sonundan bir bayt ötesini okur, daha ötesinde veya bir uzak ofsetin
  öncesinde ise 80h okur.

### Port arayüzü

`mcp_fill_cdi_device()` `CdiDevice`'ı (cdi.h) doldurur. `sdisb16_entry`, `SDISB16.DSD` için
yerel (native) DDS sürücüsüdür (`dds_register_native_driver("SDISB16.DSD", sdisb16_entry)`):
SDIInitDevice/StartOutput/GetBufferPosition/StopOutput/CloseDevice'ı ve MCPOutputBuffer
kaynağını kaydeder; GetDSMITime, MCPSampleRealAddress
(`uint8_t *fn(uint32_t handle, uint32_t pos)`, örneğin başlangıcı) ve
MCPEnable/DisableVirtualSamples'ı (`void fn(void)`) DDS üzerinden çeker. Ses arka ucu
`mcp_dma_buffer[0 .. mcp_buffer_size())`'ı karıştırma hızında döngü halinde çalar (dağıtılan
yapılandırma için işaretli 16 bit stereo) ve `mcp_dma_position`'ı (o anki turda çalınan bayt
sayısı) güncel tutar; SDIStartOutput onu 0'a sıfırlar.

## English

Decompiled to `src/dsmi/mcp.c` / `mcp.h`; differential test `tests/py/test_mcp.py`
(the original driver runs in Unicorn through `tools/dsdload.py`). Addresses are
SDISB16.DSD image offsets; `d:XXXX` is the driver data segment (image +3D30h).
SDIPAS16/SDIWSS16 contain the same MCP ("MCP v3.5 - (C) 1992,1995 Otto Chrons");
SDIGUS16 mixes in hardware.

### Structure

| Image | What |
|---|---|
| 0000 | `JMP FAR dsd_main` |
| 001C–00B1 | mcpOpenDDS / mcpCloseDDS (imports, MCPOutputBuffer resource) |
| 00BC–147B | SB/SB Pro/SB16 driver: BLASTER parsing, detection, DSP, IRQ handlers (only acknowledge), initDevice, start/stop/close |
| 147C | dsd_main (DDS entry) |
| 1675–36EF | **MCP**: tables, inner mixers 181E–21D8, mixing driver 2210–2833, time → frames 2834, init 2A2E, tables 2BB7–2D2B, setupChannels 2D2C, start/stop 2DEA/2E3E, CDI slots 2E63–36B1, command queue 2FC7–3081, poll 3082 |
| 36F8–3D2F | 8237 DMA helpers, long mul/div helpers (3C20/3C95) |

There is no FPU code. The IRQ handlers do not mix: all mixing happens in **poll**.

### Configuration (initDevice 08F6 → mcpInit 2A2E)

`mcp_config_from_sdi()`; card type, rate, stereo, sample size and amplification from the
SDI record (docs/FORMATS.md, SES.CFG). Options word: bit0 accumulate, bits 4–5
amplification 100/200/350/500 %, bit6 automatic amplification (by channel count: 70, 80,
90, 100, 100, 120 … 500 %). 8-bit output (SB, SB Pro) always accumulates. Ring size =
(2000 × bytes per sample << stereo) × rate / 22000, rounded down to 16.

**Shipped SES.CFG**: SB16, 22050 Hz, stereo, signed 16-bit, amplification none →
options 0 ("direct" mode), ring 8016 bytes = 2004 frames ≈ 91 ms.

### Channel record (d:0A65 + ch·28h, 32 channels)

+00 flags (bit0 playing, bit1 has looped, bit2 muted, bit3 sample set), +02 far sample
pointer, +06 length, +0A step (9.23 fixed point: freq·2²³/rate), +0E position, +12 loop
start, +16 loop end (0 = no loop – the loop flag in SAMPLEINFO.mode only matters at
setSample), +1A volume 0..32, +1B frequency, +1F/+21 step as 16.16 (freq/rate,
(freq%rate)·65536/rate), +23 position fraction, +25 sampleID (virtual/EMS sample), +27
panning.

### Command queue and poll (the timing model)

setSample, playSample, setVolume, setFrequency, setPosition and setPanning are **queued**
with `GetDSMITime()` (0.1 ms units; IST.EXE advances it by 100 every 10 ms) in a 350-entry
ring (no overflow check). setSample copies the SAMPLEINFO head into one of 32 slots used
round-robin (0, 31, 30, …). stop, mute, unmute, mute/unmuteAll, setMasterVolume and
setupChannels act **immediately**. Consequence: `stop` after `play` in the same poll
interval is undone when the queued play executes.

`poll` (3082; IST.EXE calls it from the 40 Hz timer routine 0D42:0093):

1. `avail` = time between the write position and the DMA position (SDIGetBufferPosition):
   `((dma − mixpos) or + ring) / bytes_per_frame · 10000 / rate`.
2. Does nothing (and keeps its clock) unless `now − last ≥ 120`; with the 100 Hz clock
   effectively every 2–3 ticks.
3. For each queued command up to a snapshot of the write index: `dt` = its time − the
   previous one (first: − `last`). `avail −= dt`; if that goes negative, all remaining
   commands are applied without mixing, **the last one is applied twice**, and the rest
   up to the DMA position is mixed. Otherwise `mix(dt)` (skipped for `dt = 0`), apply.
4. Mix the remainder up to the DMA position; `last = now`.

`mix(dt)` (2834): `bytes = rate·dt/10000·bpf`; under 16 bytes nothing is mixed (that time
is lost to the write position, the next "remainder" mix catches up); reduced modulo the
ring; frames = `((bytes − 1) & ~15) / bpf`; a block crossing the ring end is split (the
first part unrounded); a remainder under 8 frames is not mixed.

The mixer writes **behind** the DMA (between its own position and the play position),
so mixed data is heard one ring later. To get bit-identical audio the port must issue
the same commands at the same DSMI times and poll at the same times with the same DMA
positions; `mcp_dma_buffer` then holds identical bytes and the audio backend plays it as
the DMA would (advancing `mcp_dma_position`).

### Mixing (2210 → 2276 → inner loops)

Per channel (skipped if not playing or muted; virtual samples ask
`MCPSampleRealAddress(handle, pos)` first and use the returned pointer):

* frames that fit before `end` (= loop end or length): `rem = end − 1 − pos`, needed
  `= step·n >> 23`; if not enough, `n = (rem << 23) / step`, playing is cleared (looping:
  set again with bit1) – the 9.23 step decides the split, the 16.16 step advances.
* On a loop, position = loop start **exactly** (the overshoot is dropped; the fraction is
  kept); the loop repeats for the remaining frames.
* Source pointer: `seg:(off + pos)`, normalised only when the offset exceeds FFFFh; inside
  a block the 16-bit `si` **wraps**, so a block crossing a 64 KB boundary re-reads
  data 64 KB earlier (reproduced via `mcp_far_offset_hook`).
* Inner loop (8× unrolled, Duff's device): `s = byte[si]`; `frac += step_frac`;
  `si += step_int + carry`; `out += table[vol][s]`. Samples are **unsigned 8-bit**
  (index 0 = −128). **No interpolation.**
* Volume index = `(vol·master >> 6) − 1` (0..31; volume 0 → channel skipped but advanced).
  Volumes 31 and 32 give the same level. Master volume 0..64 (init 64).
* Panning (stereo only): |pan| < 3 centre (same value both sides); ≥ 63 / ≤ −63 one side
  only, full level; 100 = surround (left +v, right −v); otherwise the far side's index is
  `(63 − |pan|)·idx >> 6`.
* Tables (rebuilt by setupChannels): `vt[i] = (i+1)·amp/100` (i = 30, 31 both 31);
  16-bit modes: `((s << 8)·vt / channels) >> 5` (words in direct mode, dwords when
  accumulating); 8-bit: `(s·vt) >> 5` words plus a 2×4096-byte output table at tab:6000
  (`min(i·127/div[channels], 127) + 80h`, mirrored).
* Direct mode (shipped config): channels are **added into the DMA ring as 16-bit words
  with wrap-around – no clipping**. With the division by the channel count this rarely
  overflows.
* Accumulate modes: blocks of 2000 (mono) / 1000 (stereo) frames into tab:4000 (words)
  or tab:8000 (dwords), then 8-bit via the table (index wraps in the 64 KB segment) or
  16-bit clipped to −32768..32767.

### Quirks the port keeps

* Silent channel (volume index < 0): the position advances by `step·n >> 16` but the
  fraction is overwritten with the low word of the **byte count** of the block (AX reuse).
* A non-looping sample whose position is already at `end − 1` stays "playing" forever
  (silent).
* `loopstart ≥ loopend − 1` hangs the original inside the timer interrupt (the port stops
  after 65536 iterations).
* setPosition only works while playing; `pos ≥ length` clamps to length and keeps the
  fraction.
* The channel check of every slot compares only the low word of the channel number.
* setMasterVolume always returns FFFFh; muteAll/unmuteAll return −1 with no channels;
  most queued calls return junk in the original (the port returns 0).
* setLinearRate, download, unload, unloadAll do nothing.
* Reads past a sample end by one byte can happen (the 16.16 step vs the 9.23 split); the
  port reads one byte past the sample and 80h beyond that or before a far offset.

### Port interface

`mcp_fill_cdi_device()` fills `CdiDevice` (cdi.h). `sdisb16_entry` is the native DDS
driver for `SDISB16.DSD` (`dds_register_native_driver("SDISB16.DSD", sdisb16_entry)`):
it registers SDIInitDevice/StartOutput/GetBufferPosition/StopOutput/CloseDevice and the
MCPOutputBuffer resource, and pulls GetDSMITime, MCPSampleRealAddress
(`uint8_t *fn(uint32_t handle, uint32_t pos)`, start of the sample),
MCPEnable/DisableVirtualSamples (`void fn(void)`) through DDS. The audio backend plays
`mcp_dma_buffer[0 .. mcp_buffer_size())` in a loop at the mixing rate (16-bit signed
stereo for the shipped config) and keeps `mcp_dma_position` (bytes played in the
current lap) up to date; SDIStartOutput resets it to 0.
