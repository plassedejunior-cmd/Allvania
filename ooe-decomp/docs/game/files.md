# OoE file loading and data formats

Names are in `config/symbols_arm9.csv`. Scripts are in `tools/analysis/`:
`classify_files.py` (magic, size and compression of every file), `asset_lists.py` (dumps the
enemy, object and area id lists), `blscan.py` (BL call sites in ARM9 and all overlays),
`callers.py` (enclosing functions of calls in a decomp .c file).

## Summary

- **No compression at all.** No file in bc/sc/sc2/so/jnt/font/bm starts with an LZ10/LZ11 header,
  and nothing in the ARM9 or any overlay calls `MI_UncompressLZ8`. The only decompressor that
  runs is `MIi_UncompressBackward`, for the crt0 static image and compressed overlays.
- **The game opens files by its own file id, not by FAT id.** A 2537-entry table in the ARM9 holds
  each entry's path, type and a pointer. File ids are indexes into this table (id 0 = /bc/f_aaa00.dat,
  0x345 = font u12, and so on). The game resolves paths with `FS_OpenFile` (path, then
  `FS_ConvertPathToFileID`) each time it opens a file.
- Except for the font headers, every file format is headerless or uses the 0xBEEFF00D
  sprite-definition header or the 0x2301 skeleton (jnt) header.
- Palettes are not files. They live in ARM9 data, and asset lists point to them directly (kind 3).
- Room layouts, entity placement and text are not in the filesystem (no such files exist). They
  must be in the ARM9 or overlay data. Area-name banners are pre-rendered per language as gfx pages
  (`sc2/f_areaNN_{e,f,g,i,j,s,u}.dat`), and so are menu, record and race labels in `sc/` (`_e.._u` suffixes).

## Loading pipeline

```
Sys_Init  -> File_InitFS 02032774            FS_Init(2) (fallback FS_Init(-1))
Sys_Boot  -> File_LoadRomTables 02032798     FS_TryLoadTable -> FAT/FNT copied to 0x0212a9c0 (0xe000 bytes),
                                             File_InitTables 02032f50 clears 32 async slots + 10 overlay slots
id -> File_GetPath 02033204 (table+6) -> FS_OpenFile
  sync : File_ReadSync 02032cbc(id,dst)      open, FS_ReadFile whole file, close, DC_FlushRange
         File_GetSize 02032940(id)           open, end-top, close
         File_LoadAlloc 02033074(id)         GetSize + Heap_Alloc(size,0) + ReadSync
         File_ReadSheetCell 0203299c         seek/read rows (stride 0x200) of one cell (item icons)
  async: File_ReadAsync 02032d54(id,dst)     File_AllocAsyncSlot 02032fb4 -> FSFile in slot, FS_ReadFileAsync
         File_IsAsyncBusy 02032884(id,dst)   slot still allocated?
         File_CancelAsync 020328d0(id,dst)   FS_CancelFile + free slot
         File_CloseFinished 020327f8 (per frame) closes slots whose FSFile stat busy bit (bit0) is clear
```

Async slot table: 0x02129fa0, 32 x 0x50 bytes: `{s32 fileId (-1 free); FSFile file (0x48); void *dest (+0x4c)}`.
The slot is keyed by (id, dest) pair.

Higher layers ("Res_"):
- **Resource registry**: 100 x `{s32 id; void *data}` at 0x02100ccc. Functions: `Res_Register/Unregister/IsLoaded/GetData`.
- **Scene resource list**: 100 x `{id, data}` at `*(0x02148ea8+0x30)`. `Res_SceneTrack` adds to it and
  `Res_SceneFreeAll` 02038908 frees by file type (1: free page buffer, 8: free sprite def, 0x80: Heap_Free + clear table pointer).
- **Entity assets**: `Res_GetEntityAssetList(id, kind)` reads enemy table 0x020f2814 (kind 1) or
  object table 0x020f343c (kind 2). Each list holds `{s32 fileId; s32 kind}` pairs ending with fileId<0.
  Pair kind values: 1 = gfx page, 2 = sprite def / jnt / model, 3 = the "id" field is really a pointer
  to an ARM9 palette, and these pairs are skipped by the loaders. `Res_LoadEntityAssets` 02064c08 switches
  on `File_GetType(id)`:
  type 1 -> `Res_LoadGfxPageOnce`; type 8 -> LoadAlloc+ReadSync+register (fixed up later by
  `Res_FixupEntitySprites` 02065094); 0x10/0x40 -> alloc (size rounded up to 16, plus 0x200) and store in the table pointer;
  0x20 -> alloc and store; 0x80 (jnt) -> `File_LoadAlloc`, then `File_SetEntryPtr`.
  `Res_PreloadRoomEnemies/Objects` (020645e0/0206485c) walk the room's 12-byte entity records
  (byte +5 = 1 enemy / 2 object / 8 special; byte +6 = subtype) and preload each unique subtype.
  Dump: 121 enemy lists (314 sc pages, 121 so defs, 29 jnt, 121 palettes) and 141 object lists.
- **Area streaming**: `Res_GetAreaList(area<20, n<255)` table 0x020ecbb8 lists the bc pages (and for
  area 6, `bm/daa00.nsbmd/.nsbtx`) of each area/sector. `Res_AreaStreamStep` 02039ab0 is a 4-state
  machine for each file: type 1 is read (async) into static buffer 0x02149048, then copied to texture slot n
  (`Res_UploadTexSlot` = GX_LoadTex at slot<<13, or deferred with `Res_QueueTexUpload` /
  `Res_FlushTexUploadQueue`). Types 0x10/0x40 go through `Res_LoadModelBmd/Btx` + `Res_Register3D`.
  `Res_AreaLoadSync` loops it, and `Res_AreaLoadStep` runs one step per frame during room transitions.
- **Gfx page to VRAM helpers** (descriptor = `File_GetEntryPtr(id)`): `Res_LoadGfxPageTex` 0202fc6c
  (64 texture slots, 0x06800000 + slot*0x2000; wrapper 0202e8d4), `Res_LoadGfxPageBg` 0202fac0
  (BG char base from a G2_/G2S_GetBGxCharPtr table, slot*0x2000; wrapper 0202e8f8), `Res_LoadGfxPageObj`
  0202e948 (OBJ VRAM 0x06400000/0x06600000 in a 2D layout, 0x200-byte strips; wrapper 0202e924).
- Fonts: `Text_LoadFont12` 02084018 (id 0x345, LoadAlloc, or static buffer 11 if that fails) and
  `Text_LoadFont8` 020840c4 (id 0x346 into static buffer 0). `Res_GetStaticBuffer(n<12)` is 02027d44.
- Paths not in the table: `/data/sound_data.sdat` (ARM9 0x020f5624, NNS_SndArc), `mv/vamp.vx`
  (ov020, Actimagine), `rom:/dwc/utility.bin` and the `msg/*.NFTR` names (ov021, Wi-Fi/DWC).

## File table (ARM9 0x020d8cec, 2537 x 0x20, ends 0x020eca0c)

```c
typedef struct {
    void *ptr;        // +0x00 type 1: GfxPageDesc*; type 8: SpriteDef** slot in BSS; others: NULL, filled at load
    u16   type;       // +0x04 1 gfx page, 8 sprite def, 0x10 nsbmd, 0x40 nsbtx, 0x80 jnt, 0x100 font
    char  path[26];   // +0x06 "/sc/f_aadoor.dat"
} FileEntry;          // accessors: File_GetEntryPtr / File_GetType / File_GetPath / File_SetEntryPtr

typedef struct {      // 12 bytes, ARM9 data 0x020b7xxx-0x020c8xxx
    u8   slot;        // +0 VRAM slot written by the stream loader
    u8   nPages;      // +1 8 KB units (1 or 2)
    u16  widthTiles;  // +2 16 (128 px) or 32 (256 px)
    s16  heightTiles; // +4 16 or 32; must be a multiple of 4 for the OBJ loader
    u16  pad;
    void *data;       // +8 RAM copy once loaded (Res_LoadGfxPage)
} GfxPageDesc;
```

| range | dir | type | count |
|---|---|---|---|
| 0-834 | bc/f_ | 1 | 835 |
| 835-836 | bm/daa00.nsbmd / .nsbtx | 0x10 / 0x40 | 2 |
| 837-838 | font/LD_font_u12/u8.DAT | 0x100 | 2 |
| 839-870 | jnt/j_ | 0x80 | 32 |
| 871-1746 | sc/f_ | 1 | 876 |
| 1747-1782 | sc/t_ | 1 | 36 |
| 1783-2189 | sc2/f_ | 1 | 407 |
| 2190-2536 | so/p_ | 8 | 347 |

## Formats by directory

Script output (`classify_files.py`): no LZ headers anywhere.

### bc/ — area background tile pages (835 files, all 8192 bytes, raw)
These are raw 4bpp tile data, 128x128 px (16x16 tiles), with no header. Files are named
`f_<area><sector><nn>`. They are only reached through the area lists (`Res_GetAreaList`). The
stream loader copies them to 8 KB VRAM slots with GX_LoadTex. `zz*` pages (66) are shared.

### sc/ — sprite gfx pages (876 f_ + 36 t_)
These have the same raw page format: 786 x 8 KB (16x16 tiles), 88 x 16 KB (nPages=2), and
2 x 32 KB (`f_fk00`, `f_gk00`: 32x32 tiles, nPages=1). Enemy, object, effect, menu and UI graphics
are listed through entity asset lists or loaded directly by id. The `t_` pages are referenced
from enemy lists the same way as `f_` pages (e.g. skeleton: `p_ya_skl` + `t_ya_skl`).

### sc2/ — more raw gfx pages (407 files, 404 x 8 KB, 3 x 16 KB)
Cutscene, ending, staff-roll and prologue art (`rend`, `rplay`, `cdemo`, `prolo`, `dra`, `alb`/`albus`,
`enr`, `raw`...). Also the per-language area name banners `f_areaNN_<lang>` (7 languages e,f,g,i,j,s,u).
The ending loader (020a70c4) reads `sc2/f_rend*` (ids 0x82f+) and queues them as texture uploads.

### so/ — sprite definitions (347 files, magic 0xBEEFF00D)
These are parsed by `Res_FixupSpriteDef` 020330c8, which rewrites offsets into pointers in place.
It returns a pointer to the trailer, and that pointer is stored in `*FileEntry.ptr`.
```c
typedef struct {
    u32 magic;        // 0x00 0xBEEFF00D
    u32 hdrSize;      // 0x04 always 0x40; parts start at hdrSize*2-0x40, offsets are relative to file+hdrSize-0x40
    u32 hitboxOfs;    // 0x08 SprHitbox[] (8 bytes each)
    u32 frameOfs;     // 0x0C SprFrame[nFrames]
    u32 keyOfs;       // 0x10 SprKey[] (8 bytes each)
    u32 animOfs;      // 0x14 SprAnim[nAnims] (0 = none)
    u32 unk18, unk1C; // 0x18 always 0
    u32 trailerOfs;   // 0x20 SprDefTrailer
    u32 nFrames;      // 0x24
    u32 nAnims;       // 0x28
    u32 fileSize;     // 0x2C equals the file length in all 347 files
    u8  pad[0x10];
} SprDefHeader;
typedef struct { s16 x, y; u16 u, v; u16 w, h; u8 unk[4]; } SprPart;  // 16 bytes; u/v/w/h in pixels of the gfx page; unk not decoded
typedef struct { u16 unk; u8 nHitboxes; u8 nParts; SprHitbox *hitboxes; SprPart *parts; } SprFrame; // 12 bytes
typedef struct { u16 frame; u16 duration; u32 unk; } SprKey;           // 8 bytes (from samples)
typedef struct { u16 nKeys; u16 pad; SprKey *keys; } SprAnim;          // 8 bytes, keys laid out back to back
typedef struct { u16 nFrames, nAnims; SprFrame *frames; SprAnim *anims; u32 zero; } SprDefTrailer;
```

### jnt/ — skeletal animation (32 files, magic bytes 01 23)
These are skeletons for large enemies and bosses. They are loaded raw (type 0x80) by
`Res_LoadEntityAssets` and stored in `FileEntry.ptr`. `Jnt_InitSkeleton` 02065a94 parses them.
```c
typedef struct {
    u8  magic[2];     // 0x00 01 23
    u8  nameLen;      // 0x02
    char name[29];    // 0x03 name of the sprite def it uses, e.g. "p_armag00.opd" (the original .opd extension)
    u16 unk20;
    s16 x, y;         // 0x22 offset
    u8  nJoints;      // 0x26 -> joints at +0x30, 4 bytes each
    u8  nInvisible;   // 0x27 (by analogy with DoS/PoR skeleton files)
    u8  nVisible;     // 0x28 -> draw-order bytes after the points
    u8  nHitboxes;    // 0x29 -> 8 bytes each, after poses
    u8  nPoses;       // 0x2A -> (2 + 4*nJoints) bytes each, after joints
    u8  nPoints;      // 0x2B -> 4 bytes each, after hitboxes
    u8  nAnims;       // 0x2C animations follow (not decoded)
    u8  pad[3];
} JntHeader;          // the data order (joints, poses, hitboxes, points, draw order) is the order the parser lays out its pointers
```

### Other
- `font/LD_font_u8.DAT` (2560 B): 1bpp 8x8 glyphs, 10-byte records `{u16 BE index; u8 rows[8]}`.
  `LD_font_u12.DAT` (5824 B): 224 x 26-byte records `{u16 BE index; u16 rows[12]}`. Records are
  index-ordered at the start; the tail is not checked.
- `bm/daa00.nsbmd/.nsbtx`: standard NITRO BMD0/BTX0 (the only 3D model, for area `da`).
- `data/sound_data.sdat` SDAT, `mv/vamp.vx` VXDS (Actimagine), `dwc/utility.bin` (DWC, starts 0x10 but not LZ-sized as a file).
