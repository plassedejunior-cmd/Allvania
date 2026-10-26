# Castlevania Collection: launcher

Launcher dos ports da série. Cada jogo é um executável separado, listado em [games.ini](games.ini). O launcher só cuida de três coisas: achar e verificar o dump da ROM (SHA1), escolher a pasta de saves e abrir o executável do jogo.

## Build

### macOS

```sh
brew install cmake sdl3 sdl3_ttf sdl3_image
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/launcher
```

### Windows

Requer Visual Studio 2022 e [vcpkg](https://vcpkg.io); as dependências estão em `vcpkg.json`.

```bat
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
build\Release\launcher.exe
```

## Saves no Google Drive

Não existe integração com a API do Drive: os saves ficam numa pasta normal, e quem sincroniza é o **Google Drive para computador**.

1. Instale o Google Drive para computador e faça login.
2. No launcher, clique em **Saves** (ou aperte `S`) e escolha uma pasta dentro do Drive, por exemplo `Meu Drive/Castlevania Saves`. O diálogo já abre no Drive quando ele é encontrado.
3. Cada jogo grava em `<pasta>/<id do jogo>/` (`ooe/`, `dos/`…). O jogo recebe esse caminho com `--save-dir`.

Sem pasta escolhida, os saves ficam na pasta de dados do usuário (`~/Library/Application Support/CastlevaniaCollection/saves` no Mac, `%APPDATA%\CastlevaniaCollection\saves` no Windows).

## Arte dos cards

Coloque uma imagem em 16:9 com o nome `<id>.png` (ou `.jpg`/`.webp`) na pasta de arte do usuário:

- macOS: `~/Library/Application Support/CastlevaniaCollection/launcher/art/`
- Windows: `%APPDATA%\CastlevaniaCollection\launcher\art\`

Os ids são `ooe`, `dos`, `por` e `aos`. Sem imagem, o launcher gera uma cena noturna com as cores do jogo (`color1`/`color2` no `games.ini`). Nenhuma arte da Konami fica no repositório.

## Adicionar um jogo

Crie uma nova seção no `games.ini`, com `status=available`, o `exe` relativo à pasta do launcher e o `rom_sha1` do dump esperado.

## Atalho direto

`launcher --launch ooe` abre o jogo direto, sem passar pela prateleira. Serve para criar um atalho na área de trabalho.

## Controles

Setas ou d-pad para escolher; `Enter`, `A` ou `Start` para jogar; `R` para trocar a ROM; `S` para escolher a pasta de saves.

Fontes: Cinzel e Inter, sob a SIL Open Font License (os arquivos `assets/fonts/*-OFL.txt` acompanham as fontes).
