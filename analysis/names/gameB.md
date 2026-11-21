# gameB — ISOSYS.L battle system, GFL video player, file layer, GRTEXT

Scope: ovl4595 (ISOSYS.L), ovl5313 (GFL), ovl5646, ovl569A and ovl5784.

## 0. Address convention

All addresses are **true runtime offsets** in the corrected `build/ist_flat.bin` (the image after
the exeload overlay-placement fix). A capstone sweep of the fixed image confirms that ISOSYS has
exactly 77 functions, and every one of them is an overlay entry: thunks 1694:0020..019C, all of
them starting with `55 8B EC`. The TSV addresses equal these entry points. Old listings made
before the fix were shifted by −0x79 for ovl4595. Ignore any address taken from them.

ISOSYS uses **no floating point** (no INT 34h–3Dh in the whole overlay). It touches no ports
and has no ISR. Sound, graphics, EMS and mouse all go through the engine (0AC6, 0C7F, 09D4,
1154, 05DC, 0DB7, 5784).

## 1. ISOSYS.L (ovl4595) — isometric turn-based battle ("savaş")

### Entry points / callers
* `iso_battle` 4595:B40A is called from 3DSYS F_303F_8524 (random/fixed encounters) and from the FRPOS
  script interpreter (3FD1:0DF6).
* The 303F character/camp screen (F_303F_4F3A) uses many ISOSYS helpers: text, clamp, pictures,
  stats, items (iso_use_item), and vecize casting outside battle (iso_cast_vecize, iso_speak_vecize).
  CHARGEN (39E5) uses `iso_draw_text_shadow`, `iso_clamp_add`, `iso_show_picture` and `iso_load_battle_gfx`(?).
* INTRFACE (05DC:0B35..0C27) registers 4595 functions as window callbacks (1694:0043/0048/004D/0052/0106).
* `iso_item_effect_*` (6F5F..7233) have no static caller. They are presumably reached through a
  runtime-built function table (open question).

### Battle flow
1. `iso_battle`:
   * SCOPYs 0x48 bytes 962C→stack and 962C→974C (save UI state). Loads `palbat.pal`. MEMMAN mark DE26.
   * Plays battle music: `random(2) ? 0x0C : 0x10` via 3C2D:1F32. **RNG call #1.**
   * Builds the 3 monster groups. Group i=1..3 has type at `5BDA+6i` and count at `5BDC+6i` (count
     is accumulated into D558). A type > 100 means the group fights **on the party's side** (side 1).
   * Template T[g] (g=i+4) is a copy of monster record w2..w19. Its HP is `T+2 = BB39*w0 + w1`
     (BB39 = encounter level byte), and `T+6` (to-hit) is incremented.
   * Up to 8 combatants per group (slots `(i-1)*8+1..`) are filled from the template.
   * Then `iso_load_battle_gfx`, `iso_build_battlefield` and `iso_party_to_battle`.
2. Placement:
   * The party facing comes from the dungeon facing B9F0, and the cells N/E/S/W of the party are read from the 3D map.
   * Each monster rolls `x = random(9) + 9*bx`, `y = random(8) + 9*by` (**2 RNG calls per try**). It
     re-rolls while the cell is occupied or has terrain ≥10, 8 or 6. The number of RNG calls
     therefore depends on the map (fidelity-critical).
   * Party members 25..28 are placed around (D552/D554).
3. Main loop:
   * `iso_next_turn` (4595:1B6D) picks the next actor D5DE and returns −1 when one side is gone
     (632E = 1 win, 2 loss).
   * If the actor's side (+0x0E) is 2, `iso_ai_turn` runs.
   * Otherwise the player gives commands:
     * keypad 47..51 = move or bump-attack (`iso_move_step`; a mouse click on the 3×3 compass maps to the same codes)
     * 'v' / button 0x1E = vecize (spell)
     * Enter / button 0x1F/0x22 = attack
     * other keys = other menu actions
   * AP (D54C) is spent per action. When it reaches 0, the next turn starts.
   * Stun: when D59A was set by an attack, the defender's `+0x22 = random(2)+2` (skipped turns).
     **RNG call.** Counter-attack: D568 set → attacker/defender swapped, `iso_attack` with D5A4=1.
4. End: `iso_battle_to_party` writes HP/stats back to the character records.

### Initiative (`iso_roll_initiative` 4595:1E75), called at the first turn and every new round
```
for c = 1..28:
    order[c] = c                                     // word array DGROUP D4C0 (index c, i.e. D4C0+2c)
    init[c]  = max(0, random(6) + T[g].f0E + 10 - T[g].f08)   // DGROUP D4FA+2c, g = combatant.+06
bubble sort pairs (c, c+1), c = 1..27, swapping when init[c+1] > init[c]; repeat until no swap
```
**Fidelity:** Borland's `max()` macro evaluates the expression twice. `random(6)` is called once.
If that value is < 0 the result is 0 (one call). Otherwise **random(6) is called again** and
the second value is stored. All 28 slots are rolled, including dead or empty ones.
`random(n)` here, and everywhere in ISOSYS, is the inline Borland macro
`(int)((long)rand()*n / 0x8000)`, i.e. `rand → N_LXMUL → LDIV`. random(2), random(4) and random(8) are compiled as
`rand<<k` via LXLSH (0000:138D) or shl/rcl, followed by LDIV.
Turn loop (`iso_next_turn`): index 6330 runs 1..28. At wrap, D54A (round) is incremented,
303F:39FD(1) is called (game clock tick), and the round is re-rolled. Dead combatants (HP ≤ 0) are skipped.
Stunned combatants (+0x22 > 0) have the counter decremented and are skipped.
AP = T.+0x0C; status 0x15 → AP = max(3, AP−3); status 0x1C → AP×2.
Party actor: 2898 = actor−0x18. Battle cry sound = random(4)+0x19 (if char field −0x6656 == 1, female?)
else random(4)+0x1D (**RNG**).

### Attack resolution (`iso_attack` 4595:3F5B), attacker A = D56E, defender D = D56C, gA/gD = their templates
For n = 1..2 (D5B6), with cost = T[gA].(0x0C+2n), i.e. attack 1 = w7 and attack 2 = w8:
* `cost > 100` means cost −= 100 (flag). Attack 1 with cost 0 → 1. Skipped if cost > AP (unless D5A4, a free counter-attack) or cost ≤ 0.
* Party attacker vs monster defender: slayer bonuses.
  * Weapon slayer: weapon w0 == defender group type → +1 hit, +2 dmg.
  * Class table 0x9B92+0x24·g: field +0 == type → +2 hit, +3 dmg; field +2 == type → −1 hit, −2 dmg.
  * The weapon id comes from char slot (−0x6620+0x5C·g+2n). 0x2B maps to 0x1A. Valid weapons are 1..0x19.
* `D548 = D.+0x14` (dodge). `to_hit (D5DA) = A.+0x12 − D5C2 + hitbonus`.
* Damage base:
  * Monsters: `N = T[gA].+4 / 100`, `dmg = random(N+1) + T.+4 − 100N + dmgbonus`. **RNG.**
  * Defender stunned (+0x22 > 0): dmg = D.HP + armour (certain kill).
  * Party, attack #2 or high slots: `iso_calc_weapon` → `dmg = random(D5AA/100+1) + D5A8 + dmgbonus`. **RNG.**
* AP −= cost (unless D5A4).
* `roll = random(D548 + 5)` (**RNG**). If D is stunned, roll = 0.
  * **Hit if roll ≤ to_hit**: final = max(0, dmg − T[gD].+0x0A (armour)). D.+0x24 = 1.
  * Status 0x0F on D → D572 = 1; status 0x13 on A → D56A = 1.
  * Stun weapon: if A is a party member, the weapon %100 == 25 (fıstık), T[gD].+0x12 < 10 and T[gD].+0x14 < 0 (animals),
    then `random(100)+1 < 80` → D59A = 1 (stun, see above). **RNG (conditional).**
  * Miss: if roll−5 > to_hit and some flags are set, D568 = 1 (defender gets a counter-attack).
* HP −= final. `iso_give_xp(A, 1)`.
* On kill: `xp = sgn(T.+0x20)*5 + T.+0x02 + T.+0x12/10 + T.+0x06 + sgn(T.+0x16)*3`.
  `iso_give_xp(A, 10)`; every living party combatant 25..28 gets `max(1, xp/10)`. A party defender's
  record field −0x6654 += 10 and `iso_xp_progress` runs.
* Facing update (`(f+2)%4`, 0 → 4). If A has an on-hit vecize (+0x20) and D is still alive, it is cast
  through `iso_cast_vecize`.
* Afterwards: side-alive flags 2632[side] are recomputed, and 632E = 1 if a side is empty.

### Vecize (spells)
A "vecize" is a spoken Turkish saying. The player **types** it (`iso_cmd_vecize`). Voices are
`vecizeNN.pur` / `fecizeNN.pur` (female) and the answers `cevapNN.pur` / `fevapNN.pur`.
* Names: DGROUP 039F + 0x1F·s (31-byte strings, s = 0..31: üzüm, anten, kelle, "sen bir ceviz ağacısın", ...).
* Effect/answer text: 0779 + 0x25·s (37 bytes).
* Parameter record: **DGROUP 007E + 0x16·s**, 11 words: `w0` flags bitmask (0xC0 damage, 0x54 status, …),
  w1..w10 values. Values ≥ 1000 are formulas decoded by `iso_eval_param` (19 cases, several use random()).
  Status ids are, e.g., 10/11/18/2/8. Vecize s < 12 are blocked by target status 0x1F/0x17 ("desem de duyamaz ki!").
* Casting resolves into D598..D584 (11 params) in `iso_cast_vecize`, then `iso_vecize_effect`.
* D55C = power = T[target].+0x14/50 (+1 with status 0x13). D5BE = caster level.

### AI (`iso_ai_turn` 4595:9668)
The AI scans living opponents (side ≠ own) for the closest or weakest. It moves one step at a time
toward the target with `iso_move_step` (dx, dy in D5CA/D5C8, mapped to keypad codes) while AP remains,
and attacks when adjacent. A "spellcaster" monster with a vecize (BB53) casts instead
(after `iso_trace_line` LOS). It surrenders at low strength ("teslim, teslim!").
There are 14 random() call sites (wander/tie-break/spell choice). A per-branch reading is still needed before porting.

### Experience
There are no level numbers. `iso_give_xp` adds to the char record field −0x6652 (xp).
`iso_xp_progress` compares it with per-char checkpoints (table stride 0x22 at 0x9902/0x9908/0x990A):
* every 1000 xp: max HP += max(0, class field −0x6482)+1;
* every 190 of field −0x6654: stat increments that depend on the class signs (−0x6488 …).

### Data structures (DGROUP 16E0)
| Address | Layout |
|---|---|
| **Monster table** 1287 + 0x2E·t, t = 0..37 (ends at 195B) | 23 words: `w0` HP per encounter level, `w1` base HP, `w2` damage NNMM (= MM + random(NN+1)), `w3` to-hit, `w4` dodge (also −init), `w5` armour, `w6` AP per turn, `w7` attack-1 AP cost (+ initiative bonus), `w8` attack-2 cost (0 = none; birds and bats have one), `w9` xp value/10 and spell points?, `w10` money (−1 = animal, which drops no money and is vulnerable to the stun weapon), `w11..w15` innate status slots, `w16` (xp +5 flag), `w17` boss flag (types 32..37), `w18` max group size?, `w19..w21` drops?, `w22` ? |
| Monster long names | 1EB3 + 0x13·t (19 bytes; t=0 beyaz böcük … 37 belediye dragonu) |
| Short sprite names | 2185 + 9·t (bosch, irospa, … dragon); file = `<short>.cmp/.kor` from SAVAS |
| Party sprite names | 269B + 0xD·k (bosch.cmp, insan1.cmp …); party body types at 2643 + 0xB·k (insan, öğrenci, …) |
| **Weapon table** 0E7F + 0xE·k, k = 0..25 | 7 words: `w0` slayer monster type, `w1` hands/category, `w2` AP cost (+100 = ranged: daş, tüftüf, sapan, tebeşir, fıstık), `w3` ?, `w4` damage NNMM, `w5` to-hit bonus?, `w6` ?. Names at 0CE2 + 0xF·k (cengöt, zopa, sivri daş, …, kılıçartıbeş) |
| Armour/clothes | names 106A + 0x10·k (çötek, megadeth tişört, …, fanila); stats follow (not decoded) |
| **Consumables** | names 10EC + 0xF·k (sperm, simit, padişah macunu, …, ciklet); effect records 1047 + 0xA·k (5 words) used by iso_use_item for item ids 0x20..0x29 |
| Spell table | 007E + 0x16·s (see above) |
| **Combatant** A08A + 0x28·c, c = 1..28 (1–24 monsters, 8 per group; 25–28 party) | +00 HP, +02 vecize/spell value (w9), +04 facing 1..4, +06 template index g, +08 x, +0A y, +0C last target, +0E side (1 party/allies, 2 enemies), +12 to-hit, +14 dodge, +16..+1E 5 status slots (dur·100+id), +20 on-hit vecize, +22 stun turns, +24 hit/acted flag |
| **Template** T[g] = 9F18 + 0x2E·g (g 1–4 party, 5–7 monster groups) | +2k = monster w_k; +02 = max HP |
| Character record | 0x5C bytes per char g (fields addressed as −0x6620+0x5C·g etc. ≈ 99E0 base area): equipment slots −0x6620+2n, xp −0x6652, stats −0x665A+2k. Second per-char table 0x24 bytes at 9B7E/9B92 (class/race), third 0x22 at 9902 (xp checkpoints) |
| Map | occupancy 28×28 words at C2CE (stride 0x38), terrain at C8EE (same layout) |
| Initiative | order D4C0[c], value D4FA[c], turn index 6330, round D54A |
| Globals | D5DE actor, D56E/D56C attacker/defender, D54C AP left, D570 damage, D5E0/D5E2 spell target/id, D57E/D580 view origin, D57A/D578 cursor, 632E result, 2632/2634/2636 side-alive flags, 2898 current party char, BB39 encounter level, BB53 spell selector, 73D0/BB38 sound on |

### RNG call sites (rand = 0000:1779, LCG seed DGROUP 94C4/94C6; true addresses of the `CALLF rand`)
| function | call sites | use |
|---|---|---|
| iso_face_anim_tick 1935 | 1A7B, 1A99 | random(2), then random(2) only if the first returned 0 → face frame |
| iso_next_turn 1B6D | 1E0B / 1E29 (alternative branches) | random(4) battle cry, one call per party-member turn |
| iso_roll_initiative 1E75 | 1E9F, 1F02 | random(6), evaluated twice by max() (see above) |
| iso_draw_combatants 3955 | 3E99 | random(10)+1 → 631A |
| iso_attack 3F5B | 41E4, 4271, 42B8, 43EA | monster damage / weapon damage / to-hit roll / stun 80 % (conditional) |
| iso_vecize_reaction 552C | 5579 | reaction line choice |
| iso_build_battlefield 568C | 609A, 6100, 6243 | random(101) decoration, random(2) ×2 |
| iso_use_item 7A03 | 7DC8 | random(7) |
| iso_eval_param 8269 | 8292, 82B3, 8345, 835C, 837E, 83E5, 83F8, 840D, 843A, 848C | formula params 1001..1019 |
| iso_cast_vecize 84E9 | 8B33, 8C88, 8D7A | spell effect rolls |
| iso_vecize_effect 8E4F | 9105, 9152, 9173 | per-target rolls |
| iso_ai_turn 9668 | 97BE, 97DF, 9BC4, 9BF6, 9C4D, 9C98, 9D63, 9E8E, 9ED9, A05C, A312, A754 | AI decisions |
| iso_battle B40A | B54A music; BDA4, BDC7 placement retry loop; C196 stun random(2)+2 | |

Total: 48 call sites. None calls srand/randomize; the seed is set elsewhere.

### Other fidelity notes
* Several `max(0, expr)` / `min()` macros are double-evaluated (harmless except with RNG, see initiative).
* Placement retries make the RNG stream map-dependent.
* `iso_eval_param` hits a fatal error (559A:0000) for unknown codes. `iso_vecize_effect` reports "Böyle bir büyü bulunmamaktadır: %d".
* Sound sample rates come from a byte class table (1E8D): 4500/6000/6500/7000/7500/8500/20000/22000/44000 Hz.

### Open questions
* Exact meaning of monster w9..w22 and weapon w3/w5/w6. The character-record layout belongs with CHARGEN.
* Callers of iso_item_effect_* (probably a far-pointer table built at runtime).
* A detailed AI decision tree and the full spell-flag semantics still need a function-level pass.

## Note on the former flat-image bug (now fixed by the lead)

`tools/exeload.py` used to place every overlay except 2700 too low: 303F 0x1A, 39E5 0x31, 3C2D 0x48,
3FD1 0x5F, 4595 0x79, 5313 0x8D, 5447 0xAC, 5511 0xC2, 559A 0xD5, 55CC 0xF0, 5646 0x109, 569A 0x119,
5737 0x138, 5784 0x14A. The cause was slice assignment past the end of the bytearray, which appends instead of padding.
All addresses here are the true offsets and match the regenerated image. One consequence of the old shift:
the "m[h][0]:%d,i[h][0]:%d" debug strings belong to 3DSYS (303F:8524), not to ovl5784.

## ovl5313 — GFL video player (`CODE\GFLCODE\GFL.C`, `GFLMAIN.H`, `GFLSTR.H`, `GFLALT.H`)

The overlay contains four source files (taken from the assert `__FILE__` strings):
GFLALT.H (0000-0241, low-level EMS ring and sample-slot helpers), GFLSTR.H (04D0-08E2,
opening and streaming files), GFLMAIN.H (095E-0DE8, per-frame logic), GFL.C (1126-1270).
The only external entry used is **`gfl_play(char far *name)` 5313:1270**. All 7 call sites are
in ovl3C2D DEMOLAR.L (intro, district in/out movies, ending; true call sites 3C2D:05A9,
0618, 3803, 3840, 387D, 394B, 3985). It returns the key that ended
playback (0x1B = Esc). Fatal errors go to 559A:0000 (assert/fatal, `(file, line, msg, code)`).

### File format (verified on all 35 files in `extracted/0GFL`)

**`.GHD`** = header + first second of audio + frame index:

| off | size | field |
|---|---|---|
| 0x000 | 3 | `"GFL"`. Version check: the sum of the 3 signed bytes must be 0xD9, else "GFL versiyon hatası." |
| 0x003 | u16 | nframes (DGROUP D623) |
| 0x005 | u8 | fps (D625), 10 in every file |
| 0x006 | u16 | audio bytes per chunk (D626) = 22050 (1 s of 8-bit unsigned mono, 0x80 = silence); 0 = no audio |
| 0x008 | u32 | 100000 in every file (D628). Never read by the code |
| 0x00C | 4 | 0 |
| 0x010 | 0x300 | default palette, 6-bit VGA DAC RGB (D630) |
| 0x310 | D626 | audio for second 0 |
| 0x310+D626 | 8*nframes | index (farmalloc'd, D61C) |

Index entry (8 bytes, packed): `u8 type; u16 vsize; u32 offset; u8 flags`.
* `type`: 0 = delta frame. 2 = keyframe with a 1-second audio chunk (every `fps`-th frame: 0, 10, 20…).
  1 = keyframe without video decode (the code handles it, no file uses it). Any type≠0 counts as a resync point.
* `offset`: GFL file offset of the frame payload. If `flags & 0x80`, a 0x300-byte palette sits
  **before** it, at `offset-0x300`.
* `flags`: bit7 = palette chunk present. Bits 0-6 = music-sync control: `n = (flags&0x7F)>>1`,
  sign = −1 if bit0 else +1. `gfl_show_frame` calls DSMI 0EF6:1577 (AMP pattern break, ±1
  only) with n, n∓1 … 0. Every shipped file has 0x00 or 0x80 here.

**`.GFL`** = frame payloads, in order: `[palette 0x300 if flags&0x80] [audio D626 bytes if type==2] [video vsize bytes]`.
offset[i+1] == offset[i] + (type==2 ? 22050 : 0) + vsize + (flags[i+1]&0x80 ? 0x300 : 0) holds for every file.

**Video codec** (`gfl_decode_frame` 5313:0390). It decodes into a persistent 320×200 8-bit buffer
(64000 bytes, D5FC) that holds the previous frame. For each byte b:
* `1nnnnnnn`: literal run of `(b&0x7F)+1` bytes copied from the stream.
* `01xxxxxx`: skip (dst += len+1).
* `00xxxxxx`: fill. The next byte is the colour and the run length is len+3.
* `len` from bits 5:4 and the low nibble n: `00`→n, `01`(bit4)→2n+0x10, `10`(bit5)→4n+0x30, `11`→8n+0x70.
  Skips are therefore 1-16, 17-47/2, 49-109/4, 113-233/8 and fills 3-18, 19-49/2, 51-111/4, 115-235/8.
* The loop stops when src reaches src+vsize or dst reaches buf+64000.
Python check: in INTRO, BES-D-IN and SON, every frame consumes exactly `vsize` bytes and ends
exactly at 64000. Keyframes are just frames with no skips; nothing clears the buffer.

### Playback architecture and timing

* **Clock:** the 32-bit millisecond counter DGROUP E74A/E74C is advanced by **+10 in an ISR at
  0DB7:01F4** (NKONTROL, uses 386 `EAX`). It is registered through `10EF:0178(0x2E9B, 0, 0DB7:01F4)`
  (PIT divisor 0x2E9B = 11931 → 100.007 Hz, a timer service in seg10EF). The GFL code never
  programs the PIT itself.
* **Frame pacing:** `gfl_update_frame_clock` sets frame = (E74A − start)·fps/1000 with long
  math (LXMUL/LUDIV). Frames are shown when the clock reaches them, with no waiting between
  frames. A lagging player drops frames: if frames were skipped and the target is a delta frame,
  it is not decoded, and `gfl_stream_fill` seeks the file forward to the next keyframe.
* **Vertical retrace:** after decoding, it polls port 3DAh: it waits *until bit 3 is set*, then *until it
  clears* (end of retrace), then copies the 64000-byte buffer to the screen (09D4:068A → E29F).
* **Streaming:** the GFL is read into an **EMS ring buffer** (D5F4 handle, size D600) whose size is the
  minimum of free EMS (1154:0C4D) and the file length, capped to 2/3 of the file
  (memory mode 8DDA=0) or 1/2 (modes 1, 2). It is preloaded in 0xC000-byte reads. During
  playback `gfl_stream_fill` keeps reading 0x800-byte pieces while `1000/fps − (now − D612) > 10` ms.
  Frames are copied ring → 64K EMS staging block (D5F8) → EMS-mapped and decoded into the
  conventional frame buffer (D5FC). EMS is required (1154:0688 returns NULL without EMS
  (8700≠1) → fatal).
* **Audio:** when sound is on (73D0) and D626>0, NSES sample slot 0x30 (entry E284) gets a
  2×22050-byte EMS double buffer. The GHD audio fills half 0. The audio of keyframe k·fps goes to
  half `(k+1)&1`. The slot starts looping (0D42:014F, volume 0x40) on **the last two mixer
  channels** (73D7−1 and 73D7−2). On every frame shown with arg=1 the expected sample position
  `((now−start)·22050/1000) mod 44100` is compared with the half reported by CDI
  0EAE:0356 (channel position, device fn +0x48). On mismatch both channels are repositioned (0EAE:018A, fn +0x14).
  So audio is re-synced to the clock, but the clock is not driven by the audio. The channels are stopped with 0EAE:00E6.
* **Palette:** frames with flags&0x80 load a palette from the ring into the current palette E5EF and set it
  (0C7F:0050), and D61A=1 records the change. The next frame without one restores the header palette D630.
* **Abort:** `kbhit`/`getch` (0000:2E45/29D7) every loop. Esc (0x1B) ends playback and every key is
  consumed. The loop also ends at frame ≥ nframes.
* **No FP** in the overlay. No self-modifying code. No PIT reprogramming.
* **Fidelity quirk:** `gfl_play` sprintf's "%s.GHD" into the DGROUP string literal
  `"01234567.012"` (6BD9, 13 bytes) and uses it as a scratch buffer. Names over 8 chars are fatal ("İç hata!").

### Globals (DGROUP 16E0)
D5F2 current frame · D5F4/D5F8/D5FC far ptrs: ring EMS handle, staging EMS handle, frame buffer ·
D600 ring size · D604 file length · D608 bytes streamed (file pos) · D60C GFL handle ·
D60E start ms · D612 frame-start ms · D616 last decoded frame · D618 last processed frame ·
D61A palette-changed flag · D61C index far ptr · D620..D62F GHD header · D630 header palette ·
DE26 MEMMAN mark (09D4:0A81 / 0B22) · DE34 sample table (0x17-byte entries; slot 0x30 = E284).
Callees: LXMUL 0000:1242, LDIV 12DF, LUDIV 12E6, LMOD 12EE, LUMOD 12F6, PCMP 1259,
_fmemcpy 4001, _fmemset 4049, strlen 49CC, sprintf 48B4, farfree 24C2, memcpy32/memset32 2599:02AF/028F.
File I/O goes through ovl569A (open/read/seek/tell/close), and filelength through 16B5:0025 (ovl5447:0873).

## ovl569A — virtual file system over FLiB (.CKD) archives (module name unknown, prefix o569A_)

This is a pascal-convention (RETF n) library: `open(name, mode)`, `read(h, buf, long n)`, `seek(h, long, whence)`,
`tell`, `eof`, `close`, `write`, `flush`, plus `init(nhandles)`, `mount_archive(path, tag)` and
`unmount(slot)`. Up to 8 mounted archives (E86C, 12-byte slots: dos handle, nfiles, tag far*,
dir far*). Open looks up the name in all mounted archive directories (case-insensitive,
32-byte names) and falls back to a real file. Handle records (E868, count E866) are 0x38 bytes:
`+0 file handle (−1 free), +2 name[32], +22 base offset, +26 size, +2A ?, +2E pos, +32 ?, +36 in-archive flag`.
The archive header is 12 bytes: `"FLiB"`, u16 nfiles, …, u32 dir offset at +8. Directory entries are 0x41 bytes:
`"FiLE"`, name[32], then longs at +0x31, +0x35 (offset), +0x39 (size), +0x3D (see docs/FORMATS.md).
Quirks that matter for fidelity: read returns **0 on full success** (callers test `!=0` and `==-1`).
When a member is read past its end, pos is set to size−1. eof means pos==size−1.
Callers: main (seg0546: init + mounts), DDS seg0DF2 (driver loading), seg105B, GFL, CHARGEN,
DEMOLAR, FRPOS, 3DSYS, ovl5447/559A/55CC. It is the game-wide file layer for archived data.

## ovl5646 — stdio FILE* handle table (prefix o5646_)

Pascal-convention wrappers that ovl569A uses as its backend: a calloc'd table of FILE* (default 20)
with init, open (fopen with a mode-string table at DS:8ED8 indexed by mode&15), close (flush
then fclose), read/write in 0xFFF0 chunks through huge pointers (seg += 0xFFF), seek, tell, flush and eof.
**Only ovl569A calls it.** Borland RTL callees (from call patterns, [med]): calloc 0000:1F64,
fopen 3968, fclose 35D9, fflush? 3684, fread 3AAD, fwrite 3C95, fseek 3BBD, ftell 3C3D.

## ovl5784 — `ALTDUZEY\GRTEXT.L` (graphics text; proposed prefix grt_)

Its asserts name `T:ALTDUZEY\GRTEXT.L` (8F65/8F79). It contains the bitmap-font text routines:
string width (proportional font 1 from an EMS font block E2B7, 0x8F-byte glyph records;
fixed 4 px font 2), drawing (blits through 0AC6:0430/070B), a one-line text input box
(getch loop, CS switch tables), and **cp857 Turkish case helpers** (toupper/tolower with the
i/İ and ı/I rules, is-upper, accent stripping, placeholder→Turkish decoding). It has **no debug
printf**: the "m[h][0]:%d,i[h][0]:%d" strings are pushed by ovl303F F_303F_8524 (3DSYS). The
earlier attribution came from the shifted listing. Callers: INTRFACE seg05DC (most of them),
303F, 3FD1, 4595, 3C2D, 39E5, 2700. GRTEXT is not in the brief's prefix list, so `grt_` is a proposal.

## Open questions
* 0D42:014F argument meanings (sample start/loop/rate). The 22050 Hz playback rate is assumed from the 22050 B/s chunk size.
* Header u32 100000 at GHD+8: purpose unknown (not read).
* 0EF6:1577 is taken to be DSMI `ampBreakPattern`-like (±1 changes 7C55) [low]. Unused by shipped data.
* Exact semantics of grt_draw_text vs grt_draw_text_alt need a closer read.
