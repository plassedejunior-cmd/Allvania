# Gameplay scene (scene 8): frame flow, steps, subsystems

Names are in `config/symbols_arm9.csv`. `G` = global game state at `0x020ffc58` (0xb80 bytes).
Fixed point is 20.12 (`0x1000` = 1 px). Room screens are 256x192 (`0x100` x `0xc0`).

## 1. Scene 8 lifecycle

- `Scene08_Init` (0x02037094): loads overlay 22 (unless wireless/DL play or `G+0xb38==3`),
  `Game_InitRuntimeState` (clears `G[0..0x4ec)` only, so the save block survives),
  `Game_LoadCommonResources` (resource ids 1000..0x3f5 into slots 8..15), `Msg/text init` (0x02083b80),
  `Hud_Create(1)`, `Room_Unload`, `Game_StartFromSave` (creates the player via `Player_Init`, picks the
  start room from the save block or the new-game default, then `Area_LoadSectorSync` + `Room_Load`).
- `Scene08_Update` (0x02037128): `step_table_020eca18[Scene_GetStep()]()`.
- `Scene08_Exit` (0x02037144): menu cleanup, `Hud_Destroy`, `Room_Unload`, unloads overlay 22, frees the scene work.

Scene sub-state helpers (scene struct at 0x020d88c4): `+0x10` previous step, `+0x14` step,
`+0x8` previous sub-step, `+0xc` sub-step. `Scene_SetStep` tail-jumps to a helper (0x0202d934 literal,
not checked) that very likely resets the sub-step, since every step handler starts at sub-step 0.
`Scene_GetPrevStep` 0x0202d96c, `Scene_SetSubStep` 0x0202d97c, `Scene_NextSubStep` 0x0202d994,
`Scene_GetSubStep` 0x0202d9b0.

## 2. Step table 0x020eca18 (12 entries)

| step | addr | name | what it does / who enters it |
|---|---|---|---|
| 0 | 0x02037738 | Game_StepRoomEnter | after any room load: resets pause and shake state, copies player pos (G+0xa68..), map cell (G+0xa5c..), area (G+0xa74) and sector/room (G+0xa75/0xa76) into the save block; -> 1 |
| 1 | 0x020378bc | Game_StepMain | **per-frame gameplay** (see 3) |
| 2 | 0x02037a68 | Game_StepDoorTransition | entered from step 1 when `Room_CheckExit` fires; `Room_Unload` + `Room_EnterViaDoor` until the async load finishes; -> 0 |
| 3 | 0x02037a84 -> 0x020371a0 | Game_StepMapScreen | sub-step 0: debug touch-map teleport (`G+0x60`); 1: warp-point destination select (entered by the warp-point object ov22:022a501c when UP is pressed); 2: pause map with 5 markers (from pause menu) -> 4; 3: world-map destination select -> 6; 99: resume -> 1 |
| 4 | 0x02037a90 | Game_StepPauseMenu | START in step 1 (`Game_CheckPauseButton`). Runs `PauseMenu_Update` (ov22:0222d808, menu handler table ov22:022b5540). Menu result flags in `menu+0x370`: 0x100 return to scene 7 (title?), 0x200 map screen (step 3 sub 2), 0x400 text viewer (8), 0x80 -> 9, else 0 -> resume |
| 5 | 0x02037b64 | Game_StepEventMenu | ov19 menu dispatcher (ov19:02203028, table ov19:022235c0). Set by the ov19 object ov19:0221962c (UP pressed near positions in a list, a town/NPC-style trigger) and by ov22:022334ec |
| 6 | 0x02037b80 | Game_StepWarpFade | ov22:0223c518 fade/sub-screen sequence (sub-steps 0..5), then `Game_RequestRoomLoad` |
| 7 | 0x02037b9c | Game_StepLoadRoom | `Room_LoadPending`: loads room [G+0x61][G+0x4a0][G+0x4a1] at the spawn pos set by `Game_SetWarpDestination`; -> 0 |
| 8 | 0x02037be0 | Game_StepTextViewer | 29 entries (title text 0x584+i, body 0x567+i) unlocked by bits at G+0x780; B returns to step 4 |
| 9 | 0x02037adc | Game_StepWarpToArea1 | pause result 0x80 (step 4 maps 7 -> 9). Reloads the room, sets destination area 1 sector 1 at (0x230,0x160), -> 7. Purpose unclear (low) |
| 10 | 0x02038144 | Game_StepDebugStatusInit | debug: prints `BEAT=%05d` (G+0x990), `DAMAGE=%06d` (G+0x994) |
| 11 | 0x02038248 | Game_StepDebugStatus | debug: edits 4 `LEVEL` shorts at G+0x668; -> 1 |

Room-load chain: anything that wants a new room calls `Game_SetWarpDestination(area,sector,room,x,y)` and
`Game_RequestRoomLoad` (step 7) -> `Room_LoadPending` -> step 0 -> step 1.
Door chain: step 1 `Room_CheckExit` -> `Scene_SetStep(2)` -> `Room_EnterViaDoor` -> step 0.

## 3. Per-frame update (step 1, `Game_StepMain` 0x020378bc)

```
Area_PrefetchNextSector();            // async stream of the neighbouring sector
if (!wireless && G.b38 != 3) Game_CheckPauseButton();   // START -> step 4
Game_TickPlayTime();                  // G+0x2c, G+0x71c
Camera_UpdateShake();
Game_AnimatePalettes();
Camera_ResetTargetPriority();         // G+0x1c = 0
Entity_UpdateAll();                   // ITCM 01ff85b8: every active slot's +0 update callback
Camera_Update();                      // eases camera to target set by entities this frame
Entity_DrawAllSorted();               // ITCM 01ff8670: +4 draw callbacks z-sorted with 4 BG layers
if (G.b38==0 && !paused && SELECT-ish key) SubScreen_SetMode(map/status toggle 5/7/8);
if (SubScreen_GetMode()!=5) Map_UpdateExplored();
if (Room_CheckExit(G.player)) { ...; G.flags34 |= 0x100000; Scene_SetStep(2); }
```

There is no direct "player update" call: the player, HUD, enemies and objects are all entities whose
callbacks run inside `Entity_UpdateAll`. Order is by slot index:

| slot(s) | object | update cb |
|---|---|---|
| 4 | player (Shanoa; char table 0x020b6260 stride 0x5c, char id G+0xb3a) | `Player_Update` 0x02049c58 (draw `Player_Draw` 0x02049708) |
| 6 | player stats applier | `PlayerStats_Update` 0x0204eb20 |
| 0x2a | player helper object (draw 0x0204dbbc) | 0x0204ddb4 |
| 0x2d..0x7c | room entities (enemies, objects, items) | per-type tables (see 5) |
| 0x7d..0xe8 | menus / viewers / misc | |
| 0xec | HUD | `Hud_Update` 0x0209b484 (draw 0x0209c608) |
| 0xee | dummy (02063f84) | `Entity_NullUpdate` |
| 0xf0 | player post-update: collision + camera follow | `PlayerCam_Update` 0x0204d870 |

`Player_Update`: `Player_ReadInput` -> `Player_UpdateState` (switch on `ent+0xd`, states 0..0x15:
0 init, 1 stand/walk, 3/4 crouch-ish, 6 hurt timer, 8/9 jump/air, 10, 0xb, 0xc, 0x10, 0x12, 0x14 ...)
-> `Player_UpdateBodyHitbox`. Tile collision runs later from slot 0xf0 (`Player_UpdateCollision` ->
`Player_CollideRoom` 0x0204c3ac), which then calls `Camera_SetTarget(player pos)`.

## 4. Structs

```c
// Global game state, 0x020ffc58, 0xb80 bytes. [0x000,0x4ec) runtime (cleared by Game_InitRuntimeState),
// [0x4ec,0xb7c) = 0x690-byte save block written to backup slots.
struct GameState {
  /*000*/ s32 camTargetX, camTargetY;   // Camera_SetTarget
  /*00c*/ s32 camDeltaX, camDeltaY;
  /*018*/ s32 camEase;                  // eases from 0x1000
  /*01c*/ s32 camTargetPriority;        // reset every frame
  /*020*/ s32 camSmoothDiv;
  /*02c*/ u16 frameCounter;             // not counted while paused
  /*030*/ u32 flags30;
  /*034*/ u32 flags34;                  // 0x100000 room transition, 0x200 loading, 0x80 menu, 0x8000000 menu open, 0x20000000 creating player
  /*044*/ Room *curRoom;
  /*048*/ Room *exitDestRoom;           // from door
  /*04c*/ Door *exitDoor;
  /*050*/ u32 roomDoorFlags;
  /*054*/ u16 roomMapX, roomMapY;       // from room->packed
  /*058*/ u16 mapCellX, mapCellX2, mapCellY, mapCellY2;
  /*060*/ u8  debugMapFlag;
  /*061*/ u8  area;                     // 0..19
  /*06c*/ s32 exitDir;                  // 1 left 2 up 3 right 4 down
  /*0c8*/ Entity *player;
  /*0cc*/ Entity *cameraFocus;          // = player normally
  /*104*/ s16 stats[..];                // per char stride 0x50; +0x118 pending deltas
  /*464*/ s32 camX, camY;               // top-left scroll, 20.12
  /*470*/ s32 camXInt, camYInt;
  /*47c*/ s32 camMoveX, camMoveY;
  /*490*/ s32 doorOffsX, doorOffsY;
  /*49c*/ u8  paused, pauseAllowed, pauseKind, pause49f;
  /*4a0*/ s8  destSector, destRoom;     // with area: Game_SetWarpDestination
  /*4c8*/ u8  shakeOn, shakeTimer; s8 shakeX, shakeY;
  /*4dc*/ u32 spawnedBits[4];           // per-room spawn ids already spawned
  // ---- save block (0x4ec..0xb7b) ----
  /*600*/ PlayerStats stats;            // +0x5c HP (G+0x65c), +0x64 MP (G+0x664) shown by HUD
  /*668*/ s16 levels[4];                // debug LEVEL editor
  /*71c*/ u32 playTimeFrames;
  /*720*/ u32 flagsA[4]; /*730*/ u32 eventFlags[4]; /*740*/ u32 itemFlags[..];   // 0x1a0 pickups
  /*780*/ u32 textUnlockBits;           // step 8 viewer
  /*990*/ u32 beatCount; /*994*/ u32 damageTotal;
  /*a5c*/ u32 saveMapCell[3]; /*a68*/ s32 savePos[3];
  /*a74*/ u8  saveArea, saveSector, saveRoom;
  /*a77*/ u8  mapMarkers[area][5][2];   // x,y per marker, stride 10
  /*b35*/ u8  subScreenMode;
  /*b38*/ u8  gameMode;                 // 0 normal, 2 resume from snapshot, 3 wireless, 4 ...
  /*b3a*/ u8  playerChar;
  /*b7c*/ s8  saveSlot;                 // -1 new game, 0..2
};

// Entity slot: array at 0x021092a0, 0xf2 slots, stride 0x160
struct Entity {
  /*000*/ void (*update)(Entity*);
  /*004*/ void (*draw)(Entity*);
  /*00c*/ u8  flags;          // 1 active, 2 z-sorted draw, 4/8 misc
  /*00d*/ u8  state, substate, state3;
  /*010*/ u16 class;          // 2 enemy, 5 special object, 0xb player group; Entity_FreeByClass
  /*012*/ u16 uid;
  /*018*/ u16 timer18, timer1a;
  /*024*/ s32 screenX, screenY, drawZ;   // drawZ used by Entity_DrawAllSorted
  /*030*/ s32 x, y, z;        // world, 20.12
  /*03c*/ s32 vx, vy;
  /*06c*/ s16 spriteSet;  /*084*/ s16 anim; /*086*/ s16 palette;
  /*0a4*/ u8  hitType; u8 pad; u8 hitFlags; // a6 bit0 = hitbox active
  /*0a8*/ s16 hitDamage; /*0ac*/ u32 attackFlags; /*0b8*/ u16 hitParam; /*0ba*/ u16 kbTime; /*0bc*/ s16 kbX, kbY;
  /*0c8*/ void (*onHit)(Entity*, Entity*, void*);    // Hitbox_OnContact
  /*0cc*/ void (*onHitOther)(Entity*, Entity*, void*);
  /*0d0*/ u32 stateFlags, d4, d8, dc;               // player: 0x10 on ground, 4 facing ...
  /*0ec*/ s8  charIndex;
  /*128*/ s32 enemyData;     // from enemy table +0xe
  /*132*/ u8  subtype, spawnId;
  /*13c*/ u16 varA, varB;    // from room entity entry
  /*158*/ void *work;
};

// Room header (in overlay 22), stride 0x30 in a sector's room array
struct Room {
  /*08*/ BgLayerDef *layers;     // 4 x 0x10, tile data pointers into the AREA overlay
  /*0c*/ void *gfxList;          // null-terminated {u8 id, u8 size} pairs
  /*10*/ void *paletteList;
  /*14*/ EntityEntry *entities;  // in the AREA overlay, terminated by x==0x7fff
  /*18*/ Door *doors;            // 0x10 each, s8 x at +4, s8 y at +5
  /*1c*/ u32 packed;             // bits0-6 door count, 7-13 map x, 14-20 map y, 23-27 palette idx
};

struct EntityEntry {   // 12 bytes
  s16 x, y;
  u8  spawnId;   // bit in G+0x4dc; 0 = always respawn
  u8  type;      // 1 enemy, 2 special object, 3 ?, 4 item pickup, 6 flagged pickup, 7 ov22 hook, 8 condition group, 9 flag gate
  u8  subtype;   // enemy id / object id
  u8  extra;     // type 8: low nibble = number of entries to skip
  u16 varA, varB;
};
```

BG layers: 4 x 0x70 structs at 0x0213a5c0 (`+0x10` z used for draw interleave; `+0x38/+0x3a` size).

## 5. Entity type -> handler tables (`Room_SpawnEntity` 0x020642b0)

Slot range 0x2d..0x7c. Position, vars and spawn id are copied, then by `type`:
- 1 enemy: table **0x020b6364**, 121 entries x 0x24: `+0` init fn (called), `+4` update fn (stored in `ent+0`),
  `+0xe` u16 -> `ent+0x128`, `+0x20` flags. Code: 16 in ov19, 86 in ov22, 19 in the 0x022b73a0 slot.
  `Enemy_LoadBossOverlay` table 0x020f1164 maps enemy id -> overlay: 108->24, 119->25, 110->26, 111->27,
  116->28, 103->29, 109->30, 118->31, 112->32, 113->33, 120->34, 117->35, 114->36, 115->37, and
  60/65/78/94/22 -> 38 (bosses / large enemies).
- 2 special object: init table **0x020f370c**, update table **0x020f3940**, 141 entries each, class 5.
  66 of them point into the area-overlay slot 0x022c1fe0.
- 3: update 0x02064148, init 0x020640b4.
- 4 item pickup (flag bits G+0x740, < 0x1a0) -> 0x0206427c, slot freed.
- 6 normal-mode-only flagged pickup (0x02063268/0x020633f0), slot freed.
- 7: ov22 0x0229a2d0. 8: `Room_CheckSpawnCondition`. 9: `Room_CheckFlagGates` (event flags G+0x730).

Collision: hitboxes live inline at `ent+0xa4`; contacts call `Hitbox_OnContact` (0x02031e90) which
calls `+0xc8/+0xcc` of both entities. ITCM `Game_CheckHitboxPair`/`Game_RectOverlap` are the pair tests.
Tile collision for the player is `Player_CollideRoom`.

## 6. Rooms, areas and the per-area overlays (40-85)

```
Room_Get(area, sector, idx) = table_020ecddc[area][sector][idx]
  020ecddc (ARM9)  -> per-area sector lists  (ov19 data, 0x02222180..)
  sector list      -> Room* headers          (ov22 data, e.g. 0x022a6428)
  Room->layers / Room->entities              (AREA overlay data at 0x022c1fe0 slot)
Area_GetSectorOverlayId(area, sector) = table_020b6148[area][sector]   (ARM9)
```

20 areas (0..19). Sectors per area (from the room table): 13,2,1,1,1,1,2,2,3,1,2,2,1,1,1,1,2,1,2,6.
Overlay ids per area (sector order): 0: 65,66,68,69,70,71,72,73,76,77,74,75,67; 1: 40,41; 2: 42; 3: 43;
4: 44; 5: 45; 6: 46,47; 7: 48,49; 8: 50,51,52; 9: 53; 10: 54,55; 11: 56,57; 12: 58; 13: 59; 14: 60;
15: 61; 16: 62,63; 17: 64; 18: 78,79; 19: 80..85. (The per-area lists overlap in memory and end at -1.)

Loading: `Room_Load` -> `Area_LoadSectorAsync` (async per-sector file streaming via
`Area_LoadSectorFilesStep`) and, when the room's sector differs from the loaded one, `Area_LoadSectorOverlay`
(`Overlay_Load(id)`; same-slot overlays are unloaded by `Overlay_Load`). `Area_LoadSectorSync` does it
blocking (start of game, map teleports). `Area_PrefetchNextSector` (step 1) pre-streams the neighbouring
sector's files based on player x.

Entry points: the area overlays have no static initializer (`sinit` table holds one 0 entry) and no exported
init. Layout is `[code: ram_addr .. sinit_start) [data: room BG layer defs + entity lists]`. The code is reached
only through the global special-object tables (0x020f370c / 0x020f3940): 66 object ids have init/update pointers
into 0x022c1fe0.. (e.g. object 35 init = 0x022c1fe0, the first function of whichever area overlay is loaded).
So an object id's meaning there depends on the loaded sector overlay. The data part is reached through
`Room->layers` and `Room->entities` pointers stored in ov22.

Overlays 24-38 (slot 0x022b73a0) are boss/large-enemy code, loaded by `Enemy_LoadBossOverlay` when the room's
entity list contains that enemy. ov22:02232d68 loads other overlays in that slot from a table (not mapped).

## 7. Save data

Backup layout (offsets in the save chip): 0x000 header (0x10), 0x010 system data (0x1b4,
`Save_Read/WriteSystem`), 0x1c4 checksum block (0x24, rewritten after every write), then other blocks
(0x40 and 0x364 bytes at addresses from literal pools), game slots at `0x1e8 + slot*0x690` (3 slots).
A game slot is a raw copy of `G+0x4ec .. G+0xb7c`: `Save_SaveCurrentGame` 0x020ad1f0 /
`Save_LoadCurrentGame` 0x020ad160 use slot `G+0xb7c`. Low level: `Backup_Begin` / `Backup_Read` /
`Backup_Write` / `Backup_End` on top of the SDK CARD backup API. Callers: the frontend (ov20) and the
in-game save object (ov22:0223ae3c, ov19:02218b84).

## 8. Other subsystems

- Camera: `Camera_SetTarget` (priority-based, entities fight for it), `Camera_Update` (ease), `Camera_SetPosition`
  (clamp to room, update BG parallax, writes G+0x464..0x480), shake at G+0x4c8.
- Map: 2 bits per cell explored (`Map_UpdateExplored`), cell lookup per area (`Map_*`), pause map with 5 markers
  per area, warp select and world-map select screens (step 3/6).
- HUD: entity slot 0xec, 0x7b sprite elements; numbers from G+0x65c (4 digits) and G+0x664 (3 digits).
- Top screen: `SubScreen_GetMode/SetMode` (0x020f3674 state), modes 5/7/8/10, stored in G+0xb35.

## Open questions

- Step 5 trigger objects (ov19:0221962c, ov22:022334ec) and step 9 purpose need checking.
- Which overlay is loaded in the 0x022b73a0 slot outside boss rooms (overlay 39).
- `G+0xb38` mode values beyond 0/2/3.
