# OoE ARM9 engine layer: graphics, objects, resources

Scope: the engine code in the ARM9 main binary, roughly 0x02024a30-0x02036a00 plus the
sub-screen manager at 0x02065674. Names are in `config/symbols_arm9.csv`, which has 376
entries (176 high, 162 medium, 38 low). Names that were already in `config/symbols_arm9.csv`
(Sys_*, Scene_*, Heap_Alloc/Free/ResetAll, Gfx_TransferOam, Gfx_ApplyBgRegs, Gfx3d_ApplyFog,
Gfx3d_ApplyLights, File_CloseFinished) are not repeated in the CSV.

The most important finding for the port is that **characters, enemies, effects and room tile
layers are drawn with the DS 3D engine**: each one is a set of textured quads on main-screen
BG0. OAM sprites are only used for HUD and menu elements. Each "object" is a 0x160-byte
entity that knows how to issue geometry-engine commands for itself.

## Subsystem map

| Prefix | Range | Role |
|---|---|---|
| `Vram_` | 02025708-02025740, 0202eed4 | Fixed bank layout. Tex=A-D, TexPltt=G, BG=E, OBJ=F, SubBG=H, SubOBJ=I. Also initializes the slot tables. |
| `Gfx_` | 02027620-020278fc | Display init (GX_SetGraphicsMode(1,0,1): BG0 is 3D) and the LCD swap bit (POWCNT1 bit 15). |
| `Gfx3d_` | 02025758-02026d9c, 02027604, 020277e0, 0202791c, 0202856c-020289b4 | Projection state, ortho/perspective setup, flat and textured quad helpers, light/material/fog shadows, global tint, and dual-screen 3D. |
| `Mdl_` | 02024f40-0202566c | NNS G3D model and texture loading through the game's VRAM and palette slots. |
| `Fade_` | 02026f38-0202755c | Brightness fades for 2 screens x 5 channels. |
| `Heap_`, `HeapTag_` | 02027960-020280ac | NNS expanded heaps (1 or 2), a deferred-free queue, a tag table and 12 fixed system blocks. |
| `Obj_`, `ObjWork_` | 020289e0-0202bea0 | Object pool (0xf2 x 0x160), think/draw passes, 3D quad rendering of objects, and a per-object work pool. |
| `SprRes_` | 0202bbac-0202bd8c | 256-entry table of sprite graphics handles: (sprite data, texture slot). |
| `Anim_` | 0203035c-020307bc | Keyframe animation state machine. It is embedded in objects at +0x78. |
| `Pal_`, `TexPal_` | 0202d9c0-0202e8ac | OBJ/BG palette slot allocators (16 per screen), plus the 512-entry texture-palette shadow with dirty bits and vblank upload. |
| `Tex_`, `BgChr_`, `ObjChr_`, `VramSlot_` | 0202e8d4-0202fd6c | VRAM slot allocators for texture (64 x 8KB), BG char (7 main / 3 sub x 8KB) and OBJ char (8 or 64 slots per screen), plus a deferred texture upload queue. |
| `Bg_` | 020307cc-02031688 | Shadow of DISPCNT layer enables, BGxCNT, BG priority and BG scroll, with dirty bits. Committed after vblank. |
| `BgMap_` | 02034d78-02035430 | 2 x 4 x 4KB BG screen-map shadow, metatile renderer and dirty-map upload. |
| `Bg3d_` | 0203464c-02034cd0 | 4 room tile layers drawn as 3D quads. Interleaved with objects by depth. |
| `Oam_` | 0203553c-02036920 | Software sprite manager per screen: 160 sprites, layer lists, depth heap, affine and 3D projection, and an OAM shadow. |
| `Hit_` | 020316f4-020317d0 | Hitbox component (+0xa4 in objects). Only the entry points are named. |
| `File_`, `Res_` | 02032774-0203324c, 02028094 | File table by id, sync/async reads, sprite-data relocation (magic 0xbeeff00d). |
| `SubScreen_` | 02065674-02065890 | Touch-screen mode manager (init/update/exit table at 020f3690, fade on screen 1). |

## Per-frame pipeline (NitroMain 0x02000c78)

```
Sys_UpdateLidSleep
Input_ReadKeys / Input_UpdateTouch
Heap_FlushDeferredFrees            frees the 100-slot deferred queue (02100b3c)
Net_*
TexPal_ClearDirty      0202e250    clears 512 tex-palette dirty bits + flag 0211f120
Scene_Update           0202d48c    scene logic; scenes call Obj_RunAll =
                                     Game_RunEntityCallbacks (think pass, obj+0x00)
                                     Game_UpdateEntities     (draw pass, obj+0x04; depth-sorted
                                       objects (obj+0xc bit1) and Bg3d_DrawLayer interleaved in
                                       z steps 0x1000..0x7000 by 0x300)
                                   draw callbacks -> Obj_Draw -> Obj_DrawOrtho / Obj_DrawPersp
                                   (direct G3 FIFO register writes: MTX_*, POLYGON_ATTR,
                                    TEXIMAGE_PARAM, PLTT_BASE, VTX/TEXCOORD/COLOR)
SubScreen_Update       02065890
Oam_BuildAll           02035580    builds OAM shadows for both screens
Gfx3d_ResetFrameFlags  020257fc
File_CloseFinished
*0x04000540 = 1 (SWAP_BUFFERS, inline in NitroMain) and 020ffc44+8 |= 1 (frame ready)
Sys_FrameNop, Net_*, Sound_Main
OS_WaitVBlankIntr
  [VBlank IRQ Sys_VBlankIrq: if 020ffc44+8 bit0 -> Tex_FlushUploadQueue, TexPal_Commit]
Gfx_TransferOam        020355a0    DMA 0x400 OAM shadow per screen unless transfer disabled
Gfx_ApplyBgRegs        020307f8    Bg_CommitDispCnt, Bg_CommitOffsets, Bg_CommitControls,
                                   Bg_CommitPriorities, then clears the dirty word
BgMap_CommitAll        020351cc    uploads dirty, visible BG maps (GX_LoadBGxScr table 020b5c58)
Fade_StepAll           0202705c    steps 2x5 fade channels and writes BLDY/MASTER_BRIGHT
Gfx3d_ApplyLights      020287dc    dirty light vectors/colors
Gfx3d_ApplyMaterial    020285b8    DIF_AMB / SPE_EMI
Gfx3d_ApplyFog         02027594
(extra vblank waits)
```

Objects write geometry commands straight to the 0x04000400+ ports while the scene is
updating. NitroMain then writes SWAP_BUFFERS once per frame and sets the frame-ready bit. Only
then does the VBlank IRQ flush queued texture uploads and dirty texture palettes, so 3D
resources change atomically with the swapped frame.

Boot order (Sys_Boot): Heap_ClearState, File_LoadFatTable, **Gfx_Init** (Vram_InitBanks,
Gfx_SetLcdSwap(0), Gfx_InitMainDisplay, Gfx_InitSubDisplay, Gfx3d_Init, fog table,
Gfx3d_InitDepthRange, Gfx3d_SetOrtho, Gfx3d_InitLights, Gfx3d_InitMaterial, Bg3d_Init,
BgMap_Init, Obj_InitPool, SprRes_Init, Vram_InitSlots, Pal_InitAll, Oam_InitAll), Bg_Init,
Fade_ClearAll, Hit_ResetAll, Coll_ResetState, display on, input clear, first scene and
SubScreen_Start.

## Data structures

### Object (pool 0x021092a0, 0xf2 entries of 0x160; generation counter at 0x021011f4)

```c
typedef struct Obj {               // size 0x160
/*000*/ void (*think)(struct Obj*);     // run by Game_RunEntityCallbacks
/*004*/ void (*draw)(struct Obj*);      // run by Game_UpdateEntities
/*008*/ void (*destroy)(struct Obj*);   // Obj_SetDestructor; called by Obj_Destroy
/*00c*/ u8   flags;                     // b0 active, b1 depth-sorted draw (by z)
/*010*/ u16  level;                     // 1 on create; Obj_DestroyByLevel(<=n)
/*012*/ u16  genId;                     // unique id from 021011f4
/*01e*/ s8   blinkPhase; /*01f*/ s8 blinkPeriod; // <0 = global frame parity
/*020*/ u8   polyId;                    // POLYGON_ATTR id (bits 24-29)
/*021*/ s8   alpha;                     // 0..31 (0x1f default)
/*024*/ fx32 x, y, z;                   // z default 0x6000; depth range 0x1000..0x8000
/*054*/ fx32 scaleX, scaleY, scaleZ;    // used when flags74 & 0x8
/*060*/ u16  rotX, rotY, rotZ;          // used when flags74 & 1/2/4
/*06c*/ s16  sprRes;                    // SprRes index, 0xfff = unbound
/*06e*/ u8   partIndex; /*06f*/ u8 partFirst; /*070*/ u8 partCount; // single/range part draw
/*071*/ u8   lightMask;                 // POLYGON_ATTR light enables; also enables tint
/*074*/ u32  flags74;   // 1/2/4 rot XYZ, 8 custom scale, 0x20 ignore part palette,
                        // 0x40 ignore part tex page, 0x80 hflip, 0x100 vflip,
                        // 0x200 no z-stack offset, 0x400 per-part alpha, 0x800 inset uv,
                        // 0x1000 perspective (world 3D), 0x2000 (persp scale), 0x4000 (cleared on destroy),
                        // 0x8000 hidden, 0x10000 (persp rot tweak)
/*078*/ AnimState anim;                 // see below (0x78..0x85)
/*086*/ s16  palette;                   // TexPal index (TexPal_Alloc)
/*088*/ s16  paletteOrig;               // backup of palette
/*08a*/ s16  paletteOfs;                // added to part palette
/*08c*/ u16  polyMode;                  // POLYGON_ATTR bits 4-5
/*08e*/ u16  polyAttrExtra;             // OR-ed into POLYGON_ATTR (0x8000 = fog)
/*090*/ s8   vtxColor[4][4];            // r,g,b,(pad) 0..31 per quad corner
/*0a0*/ s16  drawOfsX, drawOfsY;        // added to part positions
/*0a4*/ HitComp hit;                    // 0xa4..0xcf: type, enable(+0xa6), box set idx(+0xa7), ...
/*158*/ void *work; /*15c*/ s32 workBlock; // Obj_AllocWork (256-byte blocks from 02101200)
} Obj;
```

Pass control is at 0x021011e0: +0x0 draw list ptr, +0x4 think count, +0x8 think list ptr,
+0xc mode bits (b0 think all, b1 draw all, b2 think list, b3 draw list), and +0x10 draw count.

### Animation state (embedded at Obj+0x78)

```c
typedef struct { u16 frame; u16 duration; u32 _pad; } AnimKey;   // 8 bytes
typedef struct { u16 nKeys; u16 _pad; AnimKey *keys; } AnimDef;  // 8 bytes
typedef struct AnimState {
/*0*/ AnimDef *anims;   // = SprData.anims
/*4*/ s16 animId;
/*6*/ s16 key;
/*8*/ u16 flags;        // b0 finished, b1 hold last key, b2 reverse, b3 paused
/*a*/ s16 timer;
/*c*/ s16 frame;        // current SprData frame (Obj+0x84)
} AnimState;
```

### Sprite graphics data (file, relocated by Res_RelocateSprData)

```c
typedef struct {            // 0x10 bytes; same format is used for OAM cells
  s16 x, y;                 // offset from origin (pixels)
  s16 u, v;                 // source pixel in the texture/char sheet
  u16 w, h;
  u8  texPage;              // +0xc  texture page (x slots-per-page)
  u8  flip;                 // +0xd  b0 vflip, b1 hflip
  u16 palette;              // +0xe
} SprPart;
typedef struct { s8 ox, oy; u8 _2; u8 nParts; void *hitboxes; SprPart *parts; } SprFrame; // 0xc
typedef struct { u16 nFrames; u16 nAnims; SprFrame *frames; AnimDef *anims; } SprData;
```

### SprRes table (0x0211e0c0, 256 x 0x10, free/used list heads at 0x0211df80)

```c
typedef struct { s16 next, prev; u32 _4; s32 texSlot; SprData *data; } SprRes;
```

Obj_Bind(obj, sprData, texSlot) allocates one of these and stores the index in Obj+0x6c.
The draw code reads the texture format from `TexSlot[texSlot].bpp` (1 = 4bpp/16-color, else
256-color) and the size from `TexSlot[texSlot].slotsPerImage` (4 = 256x256, else 128x128).

### VRAM slot tables (Tex/BgChr/ObjChr)

```c
typedef struct VramSlot {   // 0x10 bytes
/*0*/ s32  head;            // -1 free; otherwise index of first slot of the run
/*4*/ u16  bpp;             // image header byte 1 (1 = 4bpp)
/*6*/ u8   multi;           // image set (data+4 -> image ptr array)
/*7*/ u8   tag;
/*8*/ u8   slotsPerImage;
/*9*/ u8   runLength;       // total slots used
/*a*/ u16  flags;           // b0 force reload, b1 persistent (kept by Tex_FreeAllTransient),
                            // b2 deferred upload (Tex_QueueUpload), b3 reserve only
/*c*/ void *data;           // image header / file table data
} VramSlot;
```

| Table | Address | Slots | Unit | VRAM |
|---|---|---|---|---|
| texture | 0x02128660 | 64 | 0x2000 | Tex image A-D (written through LCDC at 0x06800000) |
| BG char main | 0x021285e0 | 7 | 0x2000 | BG char base from G2_GetBGxCharPtr table 020b5b80 |
| BG char sub | 0x021285a0 | 3 | 0x2000 | sub BG |
| OBJ char main | 0x02128520 / 0x021280a0 | 8 / 64 | 0x800 in 4 x 0x200 strips (ObjChr_SlotToOffset) | 0x06400000 |
| OBJ char sub | 0x021284a0 / 0x02127ca0 | 8 / 64 | same | 0x06600000 |

The texture upload queue is at 0x02128a60 (16 x {src, busy, vramOfs, size}), with its count
at 0x02127c80. The "current table" pointer used by VramSlot_Find* is at 0x02127c80+4.

### Palettes

- OBJ palette slots: 0x0211f9a0 (main) and 0x0211f8e0 (sub). BG palette slots: 0x0211fb20 and
  0x0211fa60. Each table has 16 entries of 0xc bytes: `{s16 head; u16 count; u8 type; u8 _; void *data;}`.
  Palette data format: `{u8 _; u8 type (1=16-color sets, 2=256); u8 count; u8 _; u16 colors[]}`.
- Texture palettes: 512 x 16 colors. The CPU shadow is at 0x0211fc60 (0x4000 bytes), with a
  staging copy at 0x02123c60. The used bitmap is at 0x0211fbe0 (16 words) and the dirty bitmap
  is at +0x40. The any-dirty flag is at 0x0211f120. Indices below 0xb7 are fixed (Pal_InitAll
  loads them from 020c8e54 and others). Indices 0xb7-0xff are allocated by TexPal_Alloc from
  the 73-entry table at 0x0211f560, which uses the same 0xc-byte entry layout. TexPal_Commit
  uploads dirty palettes in the VBlank IRQ.

### BG register shadow (0x02128b64)

```c
typedef struct BgShadow {
/*00*/ u32 dirty;          // b0-3 main ofs, b4-7 sub ofs, b8-11 main cnt, b12-15 sub cnt,
                           // b16/17 priority main/sub, b18/19 DISPCNT layers main/sub
/*04*/ u32 visible[2];     // layer mask: bit0-3 BG0-3, bit4 OBJ (DISPCNT bits 8-12)
/*0c*/ struct { u8 size, color256, screenBase, charBase; } cnt[2][4];
/*2c*/ u8  priority[2][4];
/*34*/ struct { s32 hofs, vofs; } ofs[2][4];
} BgShadow;
```

The setter tables at 020b5bc0 (offsets) and 020b5be0/020b5c00 (BGCNT) are indexed by
screen*4+bg. The BG0/BG1 main entries both point at 02030ec4. Main BG0 is the 3D layer, so
its BGCNT is never written. Its scroll offsets the 3D ortho camera (Gfx3d_SetOrtho uses
Bg_GetHofs/Vofs(0,0)).

### BG map shadow (0x0213a7c0) and flags (0x0213a7a0)

`map[screen][bg]` is 0x1000 bytes each (screen stride 0x4000). The flags word holds dirty
bits bg+screen*4, b8-15 (set together with the clears), and the upload lock b16/b17.

### 3D BG layers (0x0213a5c0, 4 x 0x70)

The 0x70-byte record holds: +0x00 the room layer data (u8 width-in-screens, u8 height, ...,
+4 the metatile table, +0xc the tile grid), +0x08/+0x0c scroll x/y (fx32), +0x10 z, +0x34..0x36
the tint color, +0x51 a no-subpixel flag, and +0x60 flags (0x100 visible, 0x20 drawn this
frame). Bg3d_EmitTileQuad draws one 16x16 tile. Tile attributes are 0x3fff index, 0x4000
hflip and 0x8000 vflip.

### OAM manager (main 0x021427e0, sub 0x02145b2c; 0x334c bytes each)

```c
typedef struct OamSprite {   // 0x3c bytes, pool of 160
/*00*/ u16 attr;     // b6-7 OBJ mode (3=bitmap), b4-5 priority, b10 mosaic, b11-14 flips, b15 256-color
/*02*/ u16 flags;    // b0 blink, b1 min-scale clamp, b2 screen-space, b3 force affine,
                     // b4 3D projected, b5 visible, b6 allocated, b7 sub screen
/*04*/ u32 charOfs;  // added VRAM byte offset
/*08*/ u8  charSlot; /*09*/ u8 palOfs; /*0a*/ u8 bitmapAlpha; /*0b*/ u8 sortLow;
/*0c*/ fx32 scaleX, scaleY; /*14*/ u16 rot;
/*18*/ fx32 effScaleX, effScaleY; /*20*/ u16 effRot;   // after projection
/*24*/ fx32 x, y, z;   // z<=0: layer list (z&15); z>0: depth-sorted heap
/*30*/ s16 sx, sy;     // computed screen position
/*34*/ SprData *cells; // default 020b5c78
/*38*/ s16 _38; /*3a*/ u16 cell;
} OamSprite;

typedef struct OamMgr {
/*0000*/ u16 oam[128][4];               // OAM shadow incl. affine pa..pd
/*0400*/ struct { s32 sx, sy; u16 rot; } affSrc[32];
/*0580*/ OamSprite *pool; u32 poolCount;
/*0588*/ u32 *heap; u32 heapCount; void *nodes; u32 nodeCount; u32 nodeMax;
/*059c*/ void *layer[16];               // linked lists; layer[defLayer] = 1 marks heap insert point
/*05dc*/ u32 oamUsed, affineUsed;
/*05e4*/ OamSprite sprites[160];
/*2b64*/ u8 nodeBuf[160*8]; /*3064*/ u32 heapBuf[...];
/*32e8*/ u32 defLayer; /*32ec*/ u32 ctrl; // b0 build off, b1 transfer off
/*32f0*/ fx32 *camX, *camY; /*32f8*/ fx32 camXv, camYv;
/*3300*/ s32 projOfsX, projOfsY; /*3308*/ fx32 cam3d[3]; /*3314*/ u16 camAng[3];
/*3328*/ s32 camMtx[9];
} OamMgr;
```

### Fade state

| Address | Contents |
|---|---|
| 0x02100a9c | active bits (16 + screen*5 + ch) |
| 0x02100aa0 | step[2][5] |
| 0x02100ac8 | frames left[2][5] |
| 0x02100af0 | level[2][5] (fx, -16..16) |

Channels 0-3 apply G2x_SetBlendBrightness to one BG-layer mask each. Channel 4 is the master
brightness and also covers OBJ and backdrop. Scenes call
Fade_Start(screen, ch, from, to, frames) and poll Fade_IsActive.

### 3D state shadows

| Address | Contents |
|---|---|
| 0x02100a94 | projection mode (b0-1: 1 ortho, 2 persp; b3 scaled ortho; b4/b5 per frame) |
| 0x020d88a0 | near/far (0x1000/0x8000) |
| 0x02101194 | dirty: b0-3 lights, b4-7 material |
| 0x021011a0 | material diffuse/ambient/specular/emission |
| 0x021011a8 | light vectors[4] |
| 0x021011c0 | light colors[4] |
| 0x0210119e | global tint (rgb555 + enable b15) |
| 0x02100b1c..0x02100b2c | fog shadow |
| 0x021011f6 | the 4 vertex colors of the current quad |
| 0x0211dfa0 | 65 per-depth stacking counters (reset each draw pass) |

### Files and heap

- File table 0x020d8cec, 0x20 bytes per id: +0 data pointer (resident or loaded),
  +4 u16 type, +6 path. The async read table at 0x02129fa0 holds 32 x 0x50 entries
  {id, FSFile, ..., dst}.
- Heaps are at 0x02100b34 ([0] main, [1] optional 256KB system heap), with the mode at
  0x020d88a8. The deferred-free queue at 0x02100b3c holds 100 entries. The tag table at
  0x02100ccc holds 100 {tag, ptr}. The fixed blocks at 0x02100fec are 12 entries.

## What a PC port has to replace

1. **The G3 FIFO emitters.** These are Obj_DrawOrtho/Persp/SinglePart, the Obj_EmitPart*
   functions, Bg3d_DrawLayer/EmitTileQuad, the Gfx3d_*Quad helpers, and Mdl_* (NNS G3D). They
   write MTX_*, POLYGON_ATTR, TEXIMAGE_PARAM, PLTT_BASE, VTX_16/XY, TEXCOORD and COLOR
   directly. The simplest route is a small software "geometry port" that records these
   register writes into a quad list. The cleaner route is to replace the emitters with a
   quad batcher that takes (texture slot, palette index, uv rect, 4 colors, depth, alpha,
   polyId) and draws with orthographic or perspective projection. Depth is the remapped
   object z from Gfx3d_ZToDepth.
2. **Texture VRAM and palettes.** These are the Tex_* slots (64 x 8KB, 4bpp or 8bpp paletted,
   128 or 256 wide) and TexPal (512 x 16 colors with a dirty bitmap). Keep the slot allocator
   logic and back each slot with a GPU texture. Either decode 4bpp/8bpp plus palette at draw
   time in a shader, or re-decode when a palette goes dirty.
3. **The 2D engine shadows.** These are BgShadow, the BG map shadow, OBJ/BG char slots,
   OBJ/BG palette slots and the OAM shadow. The game already does everything through RAM
   shadows plus commit functions, so the port can ignore the commit functions and rasterize
   from the shadows. The relevant functions are Gfx_TransferOam, Gfx_ApplyBgRegs,
   BgMap_CommitAll, TexPal_Commit and Tex_FlushUploadQueue. Oam_Build/Oam_EmitSprite can be
   kept as-is to produce standard OBJ attributes.
4. **Fades, blending and fog.** Replace Fade_Apply, Gfx3d_ApplyLights/Material/Fog and the
   DISPCNT writes with renderer uniforms: per-layer brightness, master brightness, the
   alpha-blend setup from Gfx_InitMainDisplay, fog and the light model.
5. **Special modes.** Gfx3d_EnableDualScreen/DualScreenFlip renders 3D on both screens by
   alternating the LCD swap with display capture. A port can render two 3D views instead.
   Gfx_SetLcdSwap must also be handled.
6. **Files.** The File_* functions use FS on the NDS ROM; replace them with host file reads by
   path (File_GetPath). The async variants can be synchronous.

## Open points / low-confidence

- 02030ec4 is used for both main BG0 and BG1 BGCNT in the setter table. This looks harmless
  because BG0 is 3D.
- The exact semantics of `SprFrame+4` (hitbox list) and the `Obj_*Custom*` and
  `Obj_DrawPartRange` draw paths are only partly understood.
- The hit component (+0xa4..0xcf) and the tile-collision state (0x021389e0) are only named at
  their entry points.
