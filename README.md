# ooe-decomp

Decompilação de **Castlevania: Order of Ecclesia** (NDS, USA, `YR9E`) com o objetivo de um port nativo para **Windows e macOS**.

Este repositório não contém nenhum asset nem código da Nintendo/Konami. Você precisa de um dump da sua própria cópia do jogo.

| ROM | SHA1 |
|---|---|
| Castlevania - Order of Ecclesia (USA) (En,Fr).nds | `f9ebf006a489b8c9391e8d1fd05ec6b9cef6fd7b` |

## Setup

```sh
cp "seu_dump.nds" baserom/baserom.nds
python3 -I tools/extract_rom.py          # -> extracted/ (ARM9, ARM7, overlays, filesystem, rom_info.json)
brew install ghidra                      # ou Ghidra 12.x manual + JDK 21
tools/ghidra/analyze.sh                  # -> ghidra_project/ooe.gpr e build/functions.csv
```

## Layout da ROM

- **ARM9** (`0x02000000`, entry `0x02000800`, 1 MB, sem compressão): o jogo em si. NitroSDK `4.1.30003`.
- **Autoloads**: ITCM `0x01FF8000` (0x7280 bytes) e DTCM `0x027E0000`.
- **86 overlays ARM9** em 6 slots de memória que se sobrepõem (`0x021DD280`, `0x021FFDA0`, `0x021FFFC0`, `0x02223E00`, `0x022B73A0`, `0x022C1FE0`). Os de `0x022C1FE0` (40–85) parecem ser as áreas do mapa.
- **ARM7** (`0x02380000`): o firmware padrão do SDK (som, touch, Wi-Fi). Não precisa de decomp: no PC ele é substituído pela camada de plataforma.
- **Filesystem**: `sc/`, `sc2/`, `so/`, `bc/` (formatos `.dat` próprios da Konami, ainda não identificados), `data/sound_data.sdat` (som no formato padrão do SDK), `bm/` (modelo 3D `.nsbmd`/`.nsbtx`), `mv/` (vídeo `.vx`), `jnt/`, `font/`.

## Arquitetura do port

A decompilação aqui é **funcional**, não *matching*: o objetivo é C legível e portável, não reproduzir byte a byte o binário original (isso exigiria o compilador Metrowerks proprietário e não ajudaria no port).

```
src/       código do jogo decompilado (ARM9 + overlays), C portável
include/   tipos e structs do jogo
platform/  substitui o hardware do DS: SDL2 para janela/input/áudio, renderer 2D (BG/OAM) e 3D (GX),
           mixer de SSEQ/SWAR, filesystem lendo direto do baserom
asm/       funções ainda não decompiladas (referência)
```

As duas telas viram um layout configurável numa janela só, e o touch vira mouse.

## Status

- [x] Extração da ROM e mapa de memória
- [x] Projeto do Ghidra com ITCM/DTCM/BSS/overlays (~9000 funções na auto-análise; os overlays de mapa 40–85 ainda têm poucas, porque só são chamados via ponteiro)
- [ ] Identificar as funções do NitroSDK (FS, GX, OS, SND) para trocar pela camada `platform/`
- [ ] Decompilar o boot e o loop principal
- [ ] Camada `platform/` mínima: abrir janela, ler arquivos, desenhar BG
- [ ] Overlays de gameplay
