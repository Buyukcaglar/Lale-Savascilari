# DSMI MCP v3.5 software mixer (SDISB16.DSD)

Decompiled to `src/dsmi/mcp.c` / `mcp.h`; differential test `tests/py/test_mcp.py`
(the original driver runs in Unicorn through `tools/dsdload.py`). Addresses are
SDISB16.DSD image offsets; `d:XXXX` is the driver data segment (image +3D30h).
SDIPAS16/SDIWSS16 contain the same MCP ("MCP v3.5 - (C) 1992,1995 Otto Chrons");
SDIGUS16 mixes in hardware.

## Structure

| Image | What |
|---|---|
| 0000 | `JMP FAR dsd_main` |
| 001C–00B1 | mcpOpenDDS / mcpCloseDDS (imports, MCPOutputBuffer resource) |
| 00BC–147B | SB/SB Pro/SB16 driver: BLASTER parsing, detection, DSP, IRQ handlers (only acknowledge), initDevice, start/stop/close |
| 147C | dsd_main (DDS entry) |
| 1675–36EF | **MCP**: tables, inner mixers 181E–21D8, mixing driver 2210–2833, time → frames 2834, init 2A2E, tables 2BB7–2D2B, setupChannels 2D2C, start/stop 2DEA/2E3E, CDI slots 2E63–36B1, command queue 2FC7–3081, poll 3082 |
| 36F8–3D2F | 8237 DMA helpers, long mul/div helpers (3C20/3C95) |

There is no FPU code. The IRQ handlers do not mix: all mixing happens in **poll**.

## Configuration (initDevice 08F6 → mcpInit 2A2E)

`mcp_config_from_sdi()`; card type, rate, stereo, sample size and amplification from the
SDI record (docs/FORMATS.md, SES.CFG). Options word: bit0 accumulate, bits 4–5
amplification 100/200/350/500 %, bit6 automatic amplification (by channel count: 70, 80,
90, 100, 100, 120 … 500 %). 8-bit output (SB, SB Pro) always accumulates. Ring size =
(2000 × bytes per sample << stereo) × rate / 22000, rounded down to 16.

**Shipped SES.CFG**: SB16, 22050 Hz, stereo, signed 16-bit, amplification none →
options 0 ("direct" mode), ring 8016 bytes = 2004 frames ≈ 91 ms.

## Channel record (d:0A65 + ch·28h, 32 channels)

+00 flags (bit0 playing, bit1 has looped, bit2 muted, bit3 sample set), +02 far sample
pointer, +06 length, +0A step (9.23 fixed point: freq·2²³/rate), +0E position, +12 loop
start, +16 loop end (0 = no loop – the loop flag in SAMPLEINFO.mode only matters at
setSample), +1A volume 0..32, +1B frequency, +1F/+21 step as 16.16 (freq/rate,
(freq%rate)·65536/rate), +23 position fraction, +25 sampleID (virtual/EMS sample), +27
panning.

## Command queue and poll (the timing model)

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

## Mixing (2210 → 2276 → inner loops)

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

## Quirks the port keeps

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

## Port interface

`mcp_fill_cdi_device()` fills `CdiDevice` (cdi.h). `sdisb16_entry` is the native DDS
driver for `SDISB16.DSD` (`dds_register_native_driver("SDISB16.DSD", sdisb16_entry)`):
it registers SDIInitDevice/StartOutput/GetBufferPosition/StopOutput/CloseDevice and the
MCPOutputBuffer resource, and pulls GetDSMITime, MCPSampleRealAddress
(`uint8_t *fn(uint32_t handle, uint32_t pos)`, start of the sample),
MCPEnable/DisableVirtualSamples (`void fn(void)`) through DDS. The audio backend plays
`mcp_dma_buffer[0 .. mcp_buffer_size())` in a loop at the mixing rate (16-bit signed
stereo for the shipped config) and keeps `mcp_dma_position` (bytes played in the
current lap) up to date; SDIStartOutput resets it to 0.
