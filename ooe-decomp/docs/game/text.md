# Texto do jogo e traduções

Todo o texto do jogo fica em dois overlays carregados em `0x021DD280`: o 0 (inglês) e o 1 (francês). Cada um tem 1893 textos seguidos de uma tabela de ponteiros, lida por `Text_GetString` (`02085238`) por um literal em `0208527c` (inglês) ou `02085278` (francês).

O port troca o inglês por uma tradução na memória ao abrir (`ooe --text arquivo.txt`, ou a chave `text=` do `games.ini` do launcher). A ROM no disco não muda.

## Fluxo

```sh
python3 -I tools/text.py extract              # -> text/en.txt (e --lang fr -> text/fr.txt)
cp text/en.txt text/pt.txt                    # traduza em text/pt.txt
python3 -I tools/text.py check text/pt.txt    # erros, códigos diferentes do inglês, espaço usado
./build-port/ooe --rom baserom/baserom.nds --text text/pt.txt
```

A pasta `text/` fica fora do git: o texto vem do seu dump, como `extracted/`. Pelo launcher, a tradução em `ooe-decomp/text/pt.txt` é usada sozinha quando existe.

## Formato

```
# comentários só antes do primeiro texto
[0012]
Primeira linha
segunda linha{E5}{E4}{E7:00}Próxima caixa

[0013]
...
```

- `[NNNN]` abre o texto de número NNNN (0000 a 1892). Os textos que o arquivo não traz continuam em inglês, então dá para traduzir aos poucos.
- Cada quebra de linha do arquivo é uma quebra de linha no jogo. As linhas em branco no fim de um texto são só separação; um texto que termina em quebra de linha a escreve como `{n}`.
- A fonte tem ASCII e as letras do Latin-1 (`á à â ã ç é ê í ó ô õ ú ü`, as maiúsculas, `¡ ¿ °`; `« »` e `…` têm glifo mas ainda não estão mapeados, use `{XX}`). Aspas e travessões tipográficos (`’ “ ” – —`) viram `' " -`. Outro caractere é erro.
- `{XX}` é um byte cru, em hexa. Os de `E0` a `FF` são códigos de controle. `E2`, `E3`, `E7`, `E8` e `E9` levam um byte de argumento, escrito `{E7:00}`. `{EA}` termina o texto e não pode aparecer.
- `{` e `}` do texto aparecem como `{5B}` e `{5D}`.

## Códigos de controle

O significado ainda é palpite pelo uso (`text.py check` avisa quando um texto traduzido não tem os mesmos códigos do inglês, fora as quebras de linha):

| Código | Uso no inglês |
|---|---|
| `E6` | quebra de linha (é a quebra do arquivo) |
| `E5` | quase sempre depois de uma quebra, antes de `E4` ou `E9`: espera o botão |
| `E4` | depois de `E5`: começa outra caixa |
| `E7 xx` | início de fala, com argumento pequeno (quem fala?) |
| `E9 xx` | depois de `E5`, argumento parecido com letra |
| `E3 xx`, `E2 xx`, `E8 xx` | com argumento, raros |
| `EB`–`F4` | sem argumento, raros (ícones de botão?) |

## Limites

- O overlay traduzido não pode passar de 142.112 bytes (`0x021FFDA0 - 0x021DD280`, o início do slot seguinte). O inglês ocupa 127.368, então sobra uns 11% para o português, que costuma ser mais longo. Textos iguais são guardados uma vez só. O `check` mostra quanto já foi usado.
- As caixas de texto não quebram linha sozinhas: as quebras do arquivo são as do jogo, então linhas maiores que as do inglês podem passar da caixa.
- Não é texto: o "GAME START" e o logo do título, os nomes de área (`f_areaNN_*`, imagens por idioma) e o que mais vier pronto como imagem.
