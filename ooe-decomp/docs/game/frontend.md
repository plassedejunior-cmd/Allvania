# Frontend scenes (overlay 20)

## The scene table has 12 entries, not 11

`020d88e8` holds 12 entries of `{overlay, init, update, exit}`. Entry 11 is
`ov20 02215080 / 02215634 / 022155d0`, and scene 7 reaches it with
`Scene_FadeOutTo(0xb, ...)`. The table ends at `020d89a8`, where the palette data for
Scene00_Init begins.

| id | overlay | what it is | evidence |
|----|---------|------------|----------|
| 0 | ov20 | Boot: save-data check, then English/French language select | f_lang00/01, p_lang00; 0202bf1c/0202c1d8/... backup checks; OS_GetOwnerInfo language; loads text overlay 0 (EN) or 1 (FR) |
| 1 | none | Error / save-error screen ending in OS_Halt | already named (Scene01_*) |
| 2 | ov20 | Boot logo sequence (Konami and related screens) | f_konami0..5, p_konami; timed steps, no input |
| 3 | ov20 | Game Over | f_gam00..23 (3 variants x 4 BGs), p_gam00; reached from GameOver_Start 02037054 |
| 4 | ov20 | Prologue text crawl before a new game | p_prolo0; Start skips; ends in scene 8 |
| 5 | ov20 | Ending staff roll (credits) | p_enr00 ("ending roll"), p_enbg; reached from ov22 ending event code |
| 6 | ov20 | Intro movie, then a short wireless probe | loads ov23 + ov39, opens "mv/vamp.vx" |
| 7 | ov20 | Title screen and all front-end menus (OM_* windows) | menu state table 02217da0, window descriptors 022172b0.. (GameStart, SelData, NameEntry, SoundMode, Record00/01, WifiMode, WirelessMode, CourseSel, SelShopPlay, FriendList, ...) |
| 8 | ov19 | Gameplay | not part of this task |
| 9 | ov20 | Records screen (top 3 names with times or scores, in pages) | p_record, f_record00/01/02_u/_f |
| 10 | ov20 | Nintendo WFC settings utility | Wfc_RunSettingsUtility 0208c998 loads ov21 (DWC, nintendowifi.net strings) and runs the utility with 0x40000 bytes of work, then re-inits GX |
| 11 | ov20 | Wii link screen ("wiids"): confirm, find up to 3 partners, transfer | f_wiids00/01/02_u/_f, f_signbd, p_wiids, p_signbd; uses ov39 (WM) |

Language global `020ffc44`: 1 = English (`_u` files, overlay 0), 2 = French (`_f` files,
overlay 1). Scene 0 sets it from the save file, or from OS owner info if the save file
is missing.

## Scene graph

Main and sub transitions are listed together. `Scene_FadeOutTo` (0202d7a0) is the usual
call. `Scene_Change` (0202d710) is only used by Sys_Boot.

```
Sys_Boot ── first power-on ──> 0 (sub 2)       Sys_Boot ── soft reset ──> 2 (sub 2)
                     (Sys_Boot: global flag bit 2 set -> scene 0 and clears the bit; clear -> scene 2)

0 Boot/save/language
   save OK / after language select ──> 2 (logos)         if 0203695c()==0
                                   ──> 7 + sub 10        otherwise (skip logos)
   save repaired (flag 1)          ──> ErrorScreen_RequestNotice ──> 1
   read/format error               ──> ErrorScreen_RequestCode4/8 ──> 1 (sub 4 or 9)
2 Logos: frame0 180f ─> frame1 180f ─> frame2 120f ─> frame3/4(lang) 120f ──> 6 + sub 4
6 Movie: end or A/B/Start/X/Y ─> fade ─> wireless probe (<=20 frames) ──> 7 + sub 10
7 Menus (exit flags at menu ctx +0x370, checked when menu state == 1):
   0x4000  ──> 10 (WFC settings)
   0x10000 ──> 4  (new game: set by name entry confirm 02201e28)
   0x8000  ──> 9  (records: set by 022033c0)
   0x20000 ──> 11 (Wii link)
   none    ──> 8  (load game / gameplay; Scene07_Exit writes start room/mode into 020ffc58)
4 Prologue ── text done or Start ──> sub 2, 8 (gameplay)
8 Gameplay ── death ──> 3 (GameOver_Start, sub 4);  ending event (ov22) ──> 5
3 Game over ── A/B/Start/X/Y ──> 7 + sub 10
5 Credits ── roll + 300 frame hold, Start skips roll ──> 0 + sub 2 (back to boot)
9 Records ── B ──> Menu_ReturnFromRecords: resume state 0xf (Record00), 7
10 WFC ── utility returns ──> Menu_ReturnFromWfcSettings: resume state 6 (WifiMode), sub 9, 7
11 Wii link ── "no" / done ──> 7 + sub 10; Wi-Fi error codes ──> 020aaa9c / 020aa980 (error screen)
```

Scene 6's probe after the movie (steps 3 to 5) calls `0208b618(p_head00, work+4, 3)`
(the same wireless start call that scene 11 uses). If `0208b7ac()` reports a partner
within 20 frames it calls `02024928(1)`, which sets bit 0x20 in a system global;
otherwise it clears that bit. The purpose of the bit is unconfirmed. Plausibly it shows
a link option, since the "wiids" link scene uses the same call.

## Scene manager details learned

- Manager `020d88c4`: +0 fade frames, +4 mode, +8/+0xc previous and current sub-step
  (`Scene_SetSubStep` 0202d97c / `Scene_GetSubStep` 0202d9b0), +0x10/+0x14 previous and
  current step, +0x1c outgoing scene (`Scene_GetLast` 0202d844, set by
  `Scene_FadeOutTo`), +0x20 current/next scene.
- `Scene_FadeOutTo(id, frames, dir)` sets mode 3 and fades from the current brightness
  to +-16 (dir 1 = -16).
- A second, independent sub-screen scene manager lives at `020f3674`
  (`SubScene_Change` 02065798, `SubScene_FadeOutTo` 020657f8, `SubScene_GetCurrent`
  02065880). Its 12-byte `{?, ?, ?}` table is at `020f3690`. Front-end scenes usually
  pair a main change with a sub change, e.g. `(main 7, sub 10)` and `(main 8, sub 2)`.
- Fades: `Fade_Start(screen, layer, from, to, frames)` 020270d4,
  `Fade_IsBusy(screen, layer)` 02027184, `Fade_GetLevel` 020270b0. Scenes wait on
  `Fade_IsBusy(0, 4)`.
- Resource ids index the file table at `020d8cec`, which has 0x20-byte entries: +0 is
  the loaded pointer and +6 the path. `020331e0` is the identity, so the id is the
  index. This is how the /sc/, /so/ and /sc2/ names above were resolved.

## Step tables and work buffers

### Scene 0: `Scene00_Update` table at 02217d04 (9 entries), work 0xa1c bytes
| step | fn | notes |
|---|---|---|
| 0 | 022130a4 CheckSaveHeader | 0 goes to 1, 2 (bad) goes to 4 (format), 3 (error) sets language from owner info and goes to 5 |
| 1 | 02213174 CheckSaveSlots | 3 slots, plus 020ac098 block; errors go to 4/5; then 2 |
| 2 | 022132f8 CheckSaveData | blocks at work+0x9c0/+0x65c; L+R held goes to 8; loads text overlay; then 3 |
| 3 | 02213844 Finish | see graph |
| 4 | 02213498 FormatSave | then 7 |
| 5 | 022138cc SaveError4 | scene 1, code 4 |
| 6 | 022138d8 SaveError8 | scene 1, code 8 |
| 7 | 02213530 RecheckHeader | 0 goes to 8 |
| 8 | 0221357c LanguageSelect | own sub-steps 0..4 via Scene_SetSubStep: 0 fade in, 2 up/down choose and A confirm, 4 save language (0202c14c) then 3 or 6 |

Work: +2 language (1/2), +0x1c4 cursor, +0x1c8 bit0 = "data was repaired",
+0x65c/+0x9c0 save scratch, +0xa00..0xa0c four sprites, +0xa10 cell data, +0xa14 char
slot, +0xa18 palette slot.

### Scene 2: `Scene02_Update` table at 02217d28 (9 entries), work 0x18 bytes
Steps 0..7 come in show/wait pairs: (0,1) frame 1 for 180 frames, (2,3) frame 2 for 120,
(4,5) frame 0 for 180 and the entry point, (6,7) frame 3 (English) or 4 (French) for 120.
Step 8 = wait for fade, then go to the queued step (work+0x14). `Logo_GotoStep(step, mode)`:
mode 2 = immediate, mode 0/1 = fade out over 60 frames to +16 or 0, then park in step 8.
Work: +0 sprite, +4 cells, +8 char slot, +0xc palette slot, +0x10 frame counter,
+0x14 queued step.

### Scene 3 (game over), work 0x1c
+0/+4 sprites, +8 cells, +0xc chars, +0x10 palette, +0x14 timer, +0x18 phase (0 wait
fade, 1 wait 30 frames, 2 alpha ramp, 3 idle). The variant comes from `020ffc58+0xb38`
(==1 selects gam2x) and `+0x61` (gam0x vs gam1x).

### Scene 4 (prologue), work 0x20
Steps: 0 init text, 1 crawl (Start + `Prologue_CanSkip` goes to 2), 2 stop BGM and start
fade over 120 frames, 3 count down then `Scene_FadeOutTo(8)`, 4 idle. Work +8 scroll y,
+0x10 scroll speed, +0x14 lines drawn, +0x18 text cursor, +0x1c countdown, +0x1e line
latch.

### Scene 5 (credits), work 0x38
+0..+0x24 ten roll sprites, +0x28/+0x2c BG sprites, +0x30 scroll y (starts 0x300000,
-0x594 per frame until < -0x1340000), +0x34 phase 0..6 (local, not Scene_SetStep),
+0x36 timer (300 frames hold).

### Scene 6 (movie), work 8
Steps: 0 wait fade, then start movie (0222444c, ov23); 1 playing; 2 fade out; 3 start
wireless probe; 4 wait up to 20 frames; 5 wait for net off, then scene 7.

### Scene 7 (menus)
Menu context from `Menu_GetContext` (02087ba4). The state handler table at 02217da0 has
26 entries. Handlers take the ctx, return the next state or 100 to stay, and use
`Menu_GetState/SetState(ctx, level)` for nested levels 0..2. State 1 (`Menu_StateLeave`)
lets Scene07_Update pick the next scene from the exit flags at ctx+0x370.
`Menu_StateEnter` (0) resumes at `Menu_GetResumeState()`, default 2 (title). States were
mapped to window names through the 0x70-byte window descriptors at 022172b0..02217cd0:
each descriptor's name pointer is read from the handler's literal pool.

### Scene 9 (records), work 0x74
+0 local phase (0 open, 1 zoom in, 2 input, 3 page flip), +0x10 category (Records_GetCategory),
+0x14 page, +0x18..+0x28 sprites, +0x2c..+0x4f glyph indices (3 rows x 8 name chars,
3 rows x 8 digits), +0x60..+0x6c files, +0x70 shared helper state (0220ca0c/0220ca1c).

### Scene 11 (Wii link), work 0x80
Steps (Scene_SetStep): 0 confirm, 1 fade, 2 fade in, 3 session (sub-steps 0 list, 1
slide, 2 transfer, 3/4 shut down, 5 wait for A, then scene 7), 4..6 fade back to step 0.
Work: +0x18 cursor, +0x20 bit0 message dirty, +0x28 transfer state, +0x2c/+0x30 message
id new/shown, +0x38..+0x4a resources, +0x4c..+0x7c thirteen sprites.
