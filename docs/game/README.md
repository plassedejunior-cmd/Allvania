# Mapa do jogo

Visão geral do código do jogo (não-SDK). Os nomes estão em [config/symbols_arm9.csv](../../config/symbols_arm9.csv) (`lib=game`); endereços de overlay usam o formato `ov020:02212dd4`. Detalhes por área:

- [engine.md](engine.md): objetos, sprites, VRAM, paletas, BG, OAM, fade, 3D e o pipeline de render por quadro
- [files.md](files.md): carregamento de arquivos e formatos de `bc/ sc/ sc2/ so/ jnt/`
- [frontend.md](frontend.md): as cenas de menu (overlay 20) e o grafo de cenas
- [gameplay.md](gameplay.md): a cena de gameplay, entidades, salas, área/overlays e save

Ferramentas: `tools/ghidra/decompile.sh <endereço>[,...]` decompila sob demanda, `tools/peek.py` lê words do ARM9, e `tools/analysis/` tem scripts de classificação de arquivos, listas de assets e busca de chamadas.

## Boot

`_start` (crt0) → `NitroMain` (`0x02000c78`, chamado por ponteiro, o Ghidra não achava):

1. `Sys_Init`: OS/tick/alarm/RTC/FX/GX, limpa VRAM/OAM/paletas, IRQs de VBlank/HBlank, zera o estado global `G` (`0x020ffc58`, 0xb80 bytes, inclui o save).
2. Laço externo `while (Sys_Boot())`: carrega as tabelas de arquivos, liga a tela e escolhe a primeira cena: cena 0 no primeiro boot, cena 2 depois de um soft reset.
3. Laço de quadros `while (!Sys_CheckSoftReset())` (L+R+Start+Select chama `OS_ResetSystem`).
4. `Sys_Shutdown` ao sair.

## Quadro

```
Sys_UpdateLidSleep        tampa fechada -> PM_GoSleepMode
Input_ReadKeys            KEYINPUT + botões X/Y do ARM7
Input_UpdateTouch
Heap_FlushDeferredFrees
Net_* (Wi-Fi / Download Play, só quando ativos)
Scene_Update              máquina de cenas (abaixo)
File_CloseFinished        leituras assíncronas
Sound_Main                NNS_SndMain
OS_WaitVBlankIntr
Gfx_TransferOam, Gfx_ApplyBgRegs, mapas de BG, paletas,
Gfx3d_ApplyLights, Gfx3d_ApplyFog        commits pós-VBlank
```

## Cenas

`Scene_Update` percorre a tabela em `0x020d88e8`: 12 entradas de `{overlay, init, update, exit}`. O `init` roda uma vez e termina com `Scene_InitDone`, que dispara o fade-in. O `update` roda a cada quadro, e cada cena tem a própria máquina de passos (`Scene_SetStep`/`Scene_GetStep`). A troca de cena é feita por `Scene_Change` e `Scene_FadeOutTo`.

| # | Overlay | Cena |
|---|---|---|
| 0 | 20 | boot: checa o save e seleciona o idioma (EN/FR) |
| 1 | — | erro fatal (Wi-Fi/backup), termina em `OS_Halt` |
| 2 | 20 | logos da Konami |
| 3 | 20 | game over |
| 4 | 20 | prólogo |
| 5 | 20 | créditos finais |
| 6 | 20 | vídeo de abertura (`mv/vamp.vx`, overlays 23/39) |
| 7 | 20 | título e todos os menus (26 estados) |
| 8 | 19 (+22) | **gameplay** |
| 9 | 20 | recordes |
| 10 | 20 | configuração da Nintendo WFC (overlay 21) |
| 11 | 20 | conexão com o Wii ("wiids") |

## Overlays

| Overlay | Endereço | Conteúdo |
|---|---|---|
| 0–1 | `0x021dd280` | textos do jogo (0 = inglês, 1 = francês) |
| 2–18 | `0x021ffda0` | blocos de 0x220 bytes, ainda não identificados |
| 19 | `0x021fffc0` | gameplay: menus de evento/NPC, tabela área→setor |
| 20 | `0x021fffc0` | todas as cenas de menu |
| 21 | `0x02223e00` | Wi-Fi (DWC) |
| 22 | `0x02223e00` | núcleo do jogo: salas, objetos, menu de pause |
| 23, 39 | `0x02223e00` / `0x022b73a0` | vídeo de abertura |
| 24–38 | `0x022b73a0` | chefes, carregados por inimigo (tabela `0x020f1164`) |
| 40–85 | `0x022c1fe0` | áreas: tiles, entidades e código dos objetos da área (tabela `[área][setor]` em `0x020b6148`) |

## O que isso significa para o port

- **Renderização:** personagens, inimigos, efeitos e camadas de tiles das salas são **quads 3D** no BG0, não sprites de OAM. O OAM só é usado em HUD e menus. O port precisa de um batcher de quads com textura paletizada e de um rasterizador de BG/OAM que leia os buffers-sombra em RAM.
- **Arquivos:** nenhum é comprimido, e todos são abertos por um ID do jogo via tabela de caminhos (`0x020d8cec`). No PC, isso vira leitura direta de arquivos.
- **Salas e entidades:** ficam nos overlays de área, então o port precisa carregar esse código e esses dados como módulos.
