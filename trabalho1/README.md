# Trabalho 1 — Arquivos de dados com índices parciais

Algoritmos e Estruturas de Dados II — organização sequencial-indexada.

**Dupla:** Nicholas e William

## Contexto

Comportamento de clientes no e-commerce de uma loja de vários departamentos, a partir do dataset
[eCommerce behavior data from multi category store](https://www.kaggle.com/datasets/mkechinov/ecommerce-behavior-data-from-multi-category-store).

Cada linha do CSV é um evento: um usuário visualizou (`view`), colocou no carrinho (`cart`) ou comprou
(`purchase`) um produto. Usamos as primeiras 1.000.000 de linhas do arquivo `2019-Nov.csv`
(eventos de 01/11/2019, das 00:00 às 15:32).

## Perguntas e hipóteses

1. **Qual é a marca mais comprada?**
   - Hipótese A: uma marca de eletrônicos (como samsung ou apple) é a líder.
   - Hipótese B: nenhuma marca tem mais de 50% das compras; há equilíbrio entre as principais.
2. **Que fração das visualizações se transforma em compra (taxa de conversão)?**
   - Hipótese: menos de 5% dos eventos são compras.
3. **Qual categoria de produto recebe mais visualizações?**
   - Hipótese: `electronics.smartphone`.

## Arquivos de dados

Os dois arquivos são **binários**, com **registros de tamanho fixo**, e cada registro termina com `\n`.
Textos menores que o campo são completados com espaços. Como todos os registros têm o mesmo tamanho,
o registro N começa no byte `N × tamanho do registro`, e qualquer registro é lido diretamente com `fseek`.

Os dois arquivos estão **ordenados pela chave**.

### `produtos.dat` — 113 bytes por registro (66.001 registros)

| Campo | Tipo | Bytes | Posição |
|---|---|---|---|
| `id_produto` (**chave**, não se repete) | inteiro sem sinal | 4 | 0 |
| `id_categoria` | inteiro sem sinal | 8 | 4 |
| `codigo_categoria` (se repete) | texto | 64 | 12 |
| `marca` (se repete) | texto | 32 | 76 |
| `preco` (em centavos) | inteiro | 4 | 108 |
| `\n` | | 1 | 112 |

No CSV, o mesmo produto aparece em todos os eventos dele. Este arquivo guarda **um registro por produto**.
Quando o produto aparece mais de uma vez, fica o preço mais recente.

### `acessos.dat` — 84 bytes por registro (1.000.000 de registros)

| Campo | Tipo | Bytes | Posição |
|---|---|---|---|
| `id_acesso` (**chave**, não se repete) | inteiro sem sinal | 4 | 0 |
| `data_hora` | texto | 19 | 4 |
| `tipo_evento` (se repete) | texto | 16 | 23 |
| `id_produto` (se repete) | inteiro sem sinal | 4 | 39 |
| `id_usuario` (se repete) | inteiro sem sinal | 4 | 43 |
| `sessao_usuario` | texto | 36 | 47 |
| `\n` | | 1 | 83 |

O dataset não tem um identificador único por evento, então a chave `id_acesso` é **gerada por um número
sequencial** (1, 2, 3...). O `id_produto` liga cada acesso ao seu produto em `produtos.dat`.

### Como os arquivos foram gerados (`separa_dataset.c`)

O programa lê o CSV uma linha por vez e cada linha gera um registro de acesso e um de produto.

- **Acessos:** cada um recebe o próximo número sequencial e é gravado na hora. Como as chaves são
  crescentes, o arquivo já nasce ordenado, e nenhum acesso fica guardado na memória.
- **Produtos:** uma tabela hash identifica os produtos repetidos, e só os produtos distintos ficam
  na memória. No final, eles são ordenados por `id_produto` com `qsort` e gravados.

## Arquivos de índice

Cada arquivo de dados tem um **índice parcial** para o campo chave:

| Índice | Entradas | Tamanho |
|---|---|---|
| `indice_produtos.idx` | 661 | 5.292 bytes |
| `indice_acessos.idx` | 10.000 | 80.004 bytes |

**Formato:** um inteiro com o total de registros do arquivo de dados e, em seguida, as entradas.
Cada entrada tem 8 bytes: a chave e o número do registro onde ela está.

**Intervalo:** uma entrada a cada **100 registros**. O arquivo de dados fica dividido em blocos de 100
registros, e o índice guarda a primeira chave de cada bloco.

**Níveis:** **um nível**. O maior índice tem 10.000 entradas (80 KB) e cabe facilmente na memória.
Um segundo nível só compensaria se o próprio índice ficasse grande demais para a memória.

**Busca pelo índice:**
1. Pesquisa binária no índice, que está na memória, para achar o bloco onde a chave pode estar.
   São no máximo 14 comparações para 10.000 entradas, sem nenhum acesso ao disco.
2. Pesquisa binária dentro desse bloco de 100 registros, usando `fseek` no arquivo de dados.
   São no máximo 7 leituras.

A pesquisa binária direto no arquivo inteiro faz até 20 leituras no disco para 1.000.000 de registros.

**Carga e gravação:** o índice é carregado quando o programa abre e gravado quando o programa termina.
Ao abrir, o total de registros guardado no índice é comparado com o do arquivo de dados. Se não bater,
os dados foram gerados de novo, e o índice é reconstruído.

## Inserção e remoção

Estão implementadas nos dois arquivos.

- **Inserção de produto:** o arquivo é copiado para um arquivo temporário, e o produto novo é
  colocado na posição certa durante a cópia. Assim o arquivo continua ordenado. Uma chave
  que já existe é recusada.
- **Inserção de acesso:** o acesso novo recebe o maior ID (último + 1). A posição dele já é o fim
  do arquivo, então ele é só acrescentado, sem reescrever o arquivo.
- **Remoção (nos dois arquivos):** física. O arquivo é copiado sem o registro removido.
- **Reconstrução do índice:** acontece **a cada inserção ou remoção**. Depois de uma alteração, os
  registros mudam de posição e as entradas do índice deixam de valer. Reconstruir custa ler um registro
  a cada 100, o que é barato perto de reescrever o arquivo de dados. Após reconstruir, o índice é
  gravado em disco na hora, para o `.idx` nunca ficar desatualizado.

Em todas as operações, só os registros necessários vão para a memória, um de cada vez. O arquivo de dados
nunca é carregado inteiro.

## Como compilar e executar (Windows)

É preciso ter o compilador **gcc** (MinGW-w64). Uma forma de instalar:

1. Instale o [MSYS2](https://www.msys2.org/).
2. No terminal **MSYS2 UCRT64**, rode `pacman -S mingw-w64-ucrt-x86_64-gcc`.
3. Adicione `C:\msys64\ucrt64\bin` à variável de ambiente `Path` do Windows.

Depois disso, abra o **Prompt de Comando** ou o **PowerShell** na pasta do projeto:

```
gcc -std=c11 -Wall -o separa_dataset.exe separa_dataset.c
gcc -std=c11 -Wall -o trabalho.exe Trabalho_Algoritmos_II.c

.\separa_dataset.exe 2019-Nov.csv 1000000
.\trabalho.exe
```

O primeiro programa gera `produtos.dat` e `acessos.dat` a partir do CSV. O segundo abre o menu com todas
as operações, e os quatro arquivos de dados e de índice precisam estar na mesma pasta dele.

## Arquivos do repositório

| Arquivo | Conteúdo |
|---|---|
| `separa_dataset.c` | gera os arquivos de dados a partir do CSV |
| `Trabalho_Algoritmos_II.c` | mostrar dados, pesquisa binária, busca pelo índice, inserção e remoção |
| `produtos.dat`, `acessos.dat` | arquivos de dados |
| `indice_produtos.idx`, `indice_acessos.idx` | arquivos de índice |
