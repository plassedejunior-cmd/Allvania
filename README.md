# Allvania

Castlevania Collection: o launcher e os ports nativos (Windows e macOS) da série.

- [launcher/](launcher): a prateleira de jogos, que verifica a ROM, escolhe a pasta de saves e abre cada port.
- [ooe-decomp/](ooe-decomp): Order of Ecclesia (decompilação e o executável `ooe`).

Nenhum asset nem código da Nintendo/Konami está no repositório: você precisa de um dump da sua própria cópia de cada jogo.

## Instalação

```sh
git clone https://github.com/plassedejunior-cmd/Allvania.git
cd Allvania
```

- macOS: `launcher/tools/setup.sh`
- Windows: `powershell launcher\tools\setup_windows.ps1`, num *Developer PowerShell for VS 2022*, com Git, CMake e vcpkg (`VCPKG_ROOT` definido).

Os detalhes estão no [README do launcher](launcher/README.md) e no [README do ooe-decomp](ooe-decomp/README.md).
