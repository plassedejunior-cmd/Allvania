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

## Port jogável (Windows e macOS)

O executável `ooe` roda o jogo sobre um núcleo de DS carregado pela API libretro: o [melonDS DS](https://github.com/JesseTG/melonds-ds) (compilado do código-fonte oficial pelo `tools/build_core`), com BIOS livre embutida, então nada da Nintendo além do seu dump. O `ooe` cuida de janela, áudio, controles e saves. Normalmente quem o abre é o [launcher](../launcher), passando `--rom` e `--save-dir`.

```sh
brew install cmake sdl3                                   # Windows: vcpkg (vcpkg.json)
tools/build_core.sh                                       # Windows: powershell tools\build_core.ps1
cmake -S . -B build-port -DCMAKE_BUILD_TYPE=Release
cmake --build build-port -j
./build-port/ooe --rom baserom/baserom.nds --save-dir ~/ooe-saves
```

- **Teclado:** setas; `X` = A, `Z` = B, `S` = X, `A` = Y; `Q`/`W` = L/R; `Enter` = Start; `Shift` = Select.
- **Controle:** layout da Nintendo por posição.
- **Touch:** o mouse faz o papel da caneta.
- **Atalhos:** `F5` salva estado, `F9` carrega, `F11` (ou `Alt+Enter`) alterna tela cheia, `Esc` volta ao launcher.
- **Telas:** `--layout original` (padrão) mostra as duas telas juntas numa janela. `--layout split` abre cada tela na sua janela: `Tab` (ou o clique do analógico direito) mostra ou esconde a de baixo, fechar a janela de baixo só a esconde, e o `F11` vale para a janela em foco, então cada tela pode ficar em tela cheia num monitor. O touch funciona só na janela de baixo. O launcher escolhe o modo pelo botão **Telas**.
- **Saves:** o save do cartucho fica em `<save-dir>/ooe.sav` e o estado rápido em `<save-dir>/ooe.state`. As duas gravações passam por um arquivo temporário seguido de renomeação, para o Google Drive não sincronizar um arquivo pela metade.
- **Teste automático:** `--test-frames N [--script arquivo] [--screenshot saida.bmp]` roda N quadros sem limite de velocidade, apertando os botões do roteiro (linhas `quadro botões`, ex.: `900 start`), e salva a tela final (com `--layout split`, a janela de baixo vai para `<saida>.bottom.bmp`).

O núcleo híbrido próprio (interpretador ARM + HLE) está guardado no branch `hybrid-core-wip`, para o caminho de substituir funções por C decompilado mais tarde.

## Layout da ROM

- **ARM9** (`0x02000000`, entry `0x02000800`, 1 MB, sem compressão): o jogo em si. NitroSDK `4.1.30003`.
- **Autoloads**: ITCM `0x01FF8000` (0x7280 bytes) e DTCM `0x027E0000`.
- **86 overlays ARM9** em 6 slots de memória que se sobrepõem (`0x021DD280`, `0x021FFDA0`, `0x021FFFC0`, `0x02223E00`, `0x022B73A0`, `0x022C1FE0`). Os de `0x022C1FE0` (40–85) parecem ser as áreas do mapa.
- **ARM7** (`0x02380000`): o firmware padrão do SDK (som, touch, Wi-Fi). Não precisa de decomp: no PC ele é substituído pela camada de plataforma.
- **Filesystem**: `sc/`, `sc2/`, `so/`, `bc/` (formatos `.dat` próprios da Konami, ainda não identificados), `data/sound_data.sdat` (som no formato padrão do SDK), `bm/` (modelo 3D `.nsbmd`/`.nsbtx`), `mv/` (vídeo `.vx`), `jnt/`, `font/`.

## Mapa do ARM9

| Faixa | Conteúdo |
|---|---|
| `0x02000800–0x02000b9c` | crt0 (`_start`) |
| `0x02000ddc–0x02002a34` | fx (matrizes, `FX_Div`, vetores, trigonometria) |
| `0x02002a38–0x02005608` | gx (`GX_Init`, VRAM, G2, G3, FIFO) |
| `0x0200560c–0x0200821c` | os (IRQ, threads, mensagens, cache, arena/heap, timer/tick/alarm, reset) |
| `0x02008228–0x02008f7c` | mi (DMA, memória, descompressão LZ) |
| `0x02008f94–0x0200a154` | snd, o lado ARM9 do driver de som |
| `0x0200a190–0x0200a394` | pxi (FIFO ARM9↔ARM7) |
| `0x0200a4ac–0x0200c9b4` | fs (comandos, archive, arquivos, rom, overlays) |
| `0x0200c9b4–0x0200fffb` | cp, spi (touch, power), rtc, card |
| `0x0200fffc–0x02016818` | mb (download play), math, ctrdg, dgt (MD5/SHA1), std |
| `0x02017094–0x020246b0` | runtime C da Metrowerks (MSL, soft-float, `_s32_div_f`…) |
| `0x020247b4–0x02024908` | `_fp_init`, `__call_static_initializers` |
| `0x0202490c–0x0208d7c8` | jogo |
| `0x0208d7cc–0x0208e2a8` | NNS fnd (lista, heap expandido, frame heap) |
| `0x0208e2f8–0x02090018` | NNS g2d, só fonte e texto (UTF-16): sem células, OAM ou animação |
| `0x0209002c–0x020954f8` | NNS g3d (render obj, glbstate, sbc, cgtool), sem anim/gfd |
| `0x02095504–0x0209a5a0` | NNS snd (player, stream, capture, sndarc, efeitos, fader) |
| `0x0209a5a0–` | jogo, Wi-Fi (NitroWiFi/DWC) |
| ITCM `0x01FF8000` | `OS_IrqHandler`, reset, `MIi_DmaSetParams*`, depois o codec de vídeo VX da Actimagine |

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
- [x] Identificar as funções do NitroSDK no ARM9 e ITCM: 1021 nomes em [config/symbols_arm9.csv](config/symbols_arm9.csv), aplicados pelo `analyze.sh`
- [x] NitroSystem: 341 funções (fnd, g2d, g3d, snd), totalizando 1362 nomes
- [ ] NitroWiFi/DWC e o codec VX da Actimagine
- [x] Boot, loop principal e sistema de cenas mapeados: ~800 funções do jogo nomeadas, ver [docs/game](docs/game/README.md)
- [ ] Decompilar o boot e o loop principal em `src/`
- [ ] Camada `platform/` mínima: abrir janela, ler arquivos, desenhar BG
- [ ] Overlays de gameplay
