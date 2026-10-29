/*
 * Arquivos usados:
 *   acessos.dat:  84 bytes por registro
 *   produtos.dat: 113 bytes por registro
 *
 * O programa mantem os registros no formato binario original.
 * A chave de cada registro e um int de 4 bytes no inicio do registro.
 *
 *
 * Execute na mesma pasta de acessos.dat e produtos.dat.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define TAM_ACESSO 84
#define TAM_PRODUTO 113
#define INTERVALO 100

#define ARQ_ACESSOS "acessos.dat"
#define ARQ_PRODUTOS "produtos.dat"
#define IDX_ACESSOS "indice_acessos.idx"
#define IDX_PRODUTOS "indice_produtos.idx"

/* Posicoes dos campos dentro dos registros binarios. */
#define DATA_POS 4
#define DATA_TAM 19
#define EVENTO_POS 23
#define EVENTO_TAM 16
#define PRODUTO_POS 39
#define USUARIO_POS 43
#define SESSAO_POS 47
#define SESSAO_TAM 36

#define CATEGORIA_ID_POS 4
#define CATEGORIA_POS 12
#define CATEGORIA_TAM 64
#define MARCA_POS 76
#define MARCA_TAM 32
#define PRECO_POS 108

/* Cada item do indice guarda uma chave e a posicao do registro. */
typedef struct {
    int chave;
    int registro;
} ItemIndice;

/* Indice parcial mantido na memoria enquanto o programa roda. */
typedef struct {
    ItemIndice *itens;
    int quantidade;
    int totalRegistros;
} Indice;

/* Le uma linha do teclado e tira o Enter do final. */
void lerTexto(char texto[], int tamanho, const char pergunta[]) {
    int n;
    printf("%s", pergunta);

    if (fgets(texto, tamanho, stdin) == NULL) {
        texto[0] = '\0';
        return;
    }

    n = (int)strlen(texto);
    if (n > 0 && texto[n - 1] == '\n') {
        texto[n - 1] = '\0';
    } else {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {
            /* Limpa o restante da linha. */
        }
    }
}

/* Le um numero inteiro simples. */
int lerInteiro(const char pergunta[], int *valor) {
    char linha[100];
    char extra;

    lerTexto(linha, sizeof(linha), pergunta);

    if (sscanf(linha, "%d %c", valor, &extra) != 1) {
        printf("Digite um numero inteiro valido.\n");
        return 0;
    }
    return 1;
}

/* Copia um int de/para os bytes do registro sem alterar o formato binario. */
int lerChave(const unsigned char registro[]) {
    int chave;
    memcpy(&chave, registro, sizeof(int));
    return chave;
}

void gravarInt(unsigned char registro[], int posicao, int valor) {
    memcpy(registro + posicao, &valor, sizeof(int));
}

void gravarCampoTexto(unsigned char registro[], int posicao,
                      int tamanho, const char texto[]) {
    int n = (int)strlen(texto);
    memset(registro + posicao, ' ', (size_t)tamanho);
    if (n > tamanho) n = tamanho;
    memcpy(registro + posicao, texto, (size_t)n);
}

/* Conta quantos registros existem no arquivo. */
int contarRegistros(FILE *arquivo, int tamanhoRegistro) {
    long bytes;

    if (fseek(arquivo, 0, SEEK_END) != 0) return -1;
    bytes = ftell(arquivo);

    if (bytes < 0 || bytes % tamanhoRegistro != 0) return -1;
    return (int)(bytes / tamanhoRegistro);
}

/* Le um registro pela sua posicao: 0 e o primeiro registro. */
int lerRegistro(FILE *arquivo, int tamanho, int numero,
                unsigned char registro[]) {
    long posicao = (long)numero * tamanho;

    if (fseek(arquivo, posicao, SEEK_SET) != 0) return 0;
    return fread(registro, 1, (size_t)tamanho, arquivo) == (size_t)tamanho;
}

/* Monta o indice parcial na memoria: guarda uma chave a cada 100 registros. */
int construirIndice(const char nomeDados[], int tamanhoRegistro, Indice *indice) {
    FILE *dados = fopen(nomeDados, "rb");
    unsigned char registro[TAM_PRODUTO];
    int total, i, quantidade;

    if (dados == NULL) {
        printf("Nao foi possivel abrir %s.\n", nomeDados);
        return 0;
    }

    total = contarRegistros(dados, tamanhoRegistro);
    if (total < 0) {
        printf("Tamanho invalido no arquivo %s.\n", nomeDados);
        fclose(dados);
        return 0;
    }

    quantidade = (total + INTERVALO - 1) / INTERVALO;
    free(indice->itens);
    indice->itens = malloc((size_t)(quantidade > 0 ? quantidade : 1) * sizeof(ItemIndice));
    indice->quantidade = 0;
    indice->totalRegistros = total;
    if (indice->itens == NULL) {
        fclose(dados);
        return 0;
    }

    for (i = 0; i < quantidade; i++) {
        if (!lerRegistro(dados, tamanhoRegistro, i * INTERVALO, registro)) {
            fclose(dados);
            return 0;
        }
        indice->itens[i].chave = lerChave(registro);
        indice->itens[i].registro = i * INTERVALO;
    }

    indice->quantidade = quantidade;
    fclose(dados);
    return 1;
}

/* Grava o indice: primeiro o total de registros do arquivo de dados, depois os itens. */
int salvarIndice(const char nomeIndice[], const Indice *indice) {
    FILE *arquivo = fopen(nomeIndice, "wb");
    int ok;

    if (arquivo == NULL) {
        printf("Nao foi possivel criar %s.\n", nomeIndice);
        return 0;
    }

    ok = fwrite(&indice->totalRegistros, sizeof(int), 1, arquivo) == 1 &&
         fwrite(indice->itens, sizeof(ItemIndice), (size_t)indice->quantidade, arquivo)
             == (size_t)indice->quantidade;

    fclose(arquivo);
    return ok;
}

/* So aceita o indice salvo se ele foi montado para o arquivo de dados atual. */
int carregarIndice(const char nomeIndice[], int totalAtual, Indice *indice) {
    FILE *arquivo = fopen(nomeIndice, "rb");
    int totalSalvo, quantidade;

    if (arquivo == NULL) return 0;

    if (fread(&totalSalvo, sizeof(int), 1, arquivo) != 1 || totalSalvo != totalAtual) {
        fclose(arquivo);
        return 0;
    }

    quantidade = (totalSalvo + INTERVALO - 1) / INTERVALO;
    indice->itens = malloc((size_t)(quantidade > 0 ? quantidade : 1) * sizeof(ItemIndice));
    if (indice->itens == NULL ||
        fread(indice->itens, sizeof(ItemIndice), (size_t)quantidade, arquivo) != (size_t)quantidade) {
        free(indice->itens);
        indice->itens = NULL;
        fclose(arquivo);
        return 0;
    }

    indice->quantidade = quantidade;
    indice->totalRegistros = totalSalvo;
    fclose(arquivo);
    return 1;
}

/* Reconstroi o indice depois de uma alteracao e ja grava, para o arquivo .idx nunca ficar desatualizado. */
int reconstruirIndice(const char nomeDados[], const char nomeIndice[],
                      int tamanhoRegistro, Indice *indice) {
    return construirIndice(nomeDados, tamanhoRegistro, indice) &&
           salvarIndice(nomeIndice, indice);
}

/* Ao abrir o programa: usa o indice salvo se ele ainda vale; senao reconstroi. */
void abrirIndice(const char nomeDados[], const char nomeIndice[],
                 int tamanhoRegistro, Indice *indice) {
    FILE *dados = fopen(nomeDados, "rb");
    int total;

    if (dados == NULL) return;
    total = contarRegistros(dados, tamanhoRegistro);
    fclose(dados);

    if (total >= 0 && carregarIndice(nomeIndice, total, indice))
        printf("Indice %s carregado do arquivo.\n", nomeIndice);
    else if (reconstruirIndice(nomeDados, nomeIndice, tamanhoRegistro, indice))
        printf("Indice %s criado a partir dos dados.\n", nomeIndice);
}

/* Pesquisa binaria diretamente no arquivo de dados. */
int buscaDireta(const char nomeDados[], int tamanhoRegistro,
                int chave, int *posicaoEncontrada) {
    FILE *arquivo = fopen(nomeDados, "rb");
    unsigned char registro[TAM_PRODUTO];
    int total, inicio = 0, fim, meio, atual;

    if (arquivo == NULL) return 0;
    total = contarRegistros(arquivo, tamanhoRegistro);
    if (total <= 0) {
        fclose(arquivo);
        return 0;
    }

    fim = total - 1;
    while (inicio <= fim) {
        meio = inicio + (fim - inicio) / 2;

        if (!lerRegistro(arquivo, tamanhoRegistro, meio, registro)) break;
        atual = lerChave(registro);

        if (atual == chave) {
            *posicaoEncontrada = meio;
            fclose(arquivo);
            return 1;
        } else if (atual < chave) {
            inicio = meio + 1;
        } else {
            fim = meio - 1;
        }
    }

    fclose(arquivo);
    return 0;
}

/* Pesquisa no indice parcial e depois apenas no intervalo correspondente. */
int buscaPorIndice(const char nomeDados[], const Indice *indice,
                   int tamanhoRegistro, int chave, int *posicaoEncontrada) {
    FILE *dados = fopen(nomeDados, "rb");
    unsigned char registro[TAM_PRODUTO];
    int total, inicio, fim, meio, escolhido = -1, atual;
    int inicioIntervalo, fimIntervalo;

    if (dados == NULL) return 0;
    total = contarRegistros(dados, tamanhoRegistro);
    if (total <= 0 || indice->quantidade == 0) {
        fclose(dados);
        return 0;
    }

    /* Acha o ultimo item do indice cuja chave seja menor ou igual a buscada. */
    inicio = 0;
    fim = indice->quantidade - 1;
    while (inicio <= fim) {
        meio = inicio + (fim - inicio) / 2;
        if (indice->itens[meio].chave <= chave) {
            escolhido = meio;
            inicio = meio + 1;
        } else {
            fim = meio - 1;
        }
    }

    if (escolhido == -1) {
        fclose(dados);
        return 0;
    }

    inicioIntervalo = indice->itens[escolhido].registro;
    if (escolhido + 1 < indice->quantidade)
        fimIntervalo = indice->itens[escolhido + 1].registro - 1;
    else
        fimIntervalo = total - 1;

    inicio = inicioIntervalo;
    fim = fimIntervalo;
    while (inicio <= fim) {
        meio = inicio + (fim - inicio) / 2;
        if (!lerRegistro(dados, tamanhoRegistro, meio, registro)) break;
        atual = lerChave(registro);

        if (atual == chave) {
            *posicaoEncontrada = meio;
            fclose(dados);
            return 1;
        } else if (atual < chave) {
            inicio = meio + 1;
        } else {
            fim = meio - 1;
        }
    }

    fclose(dados);
    return 0;
}

void mostrarAcesso(const unsigned char r[], int numero) {
    char data[20], evento[17], sessao[37];

    memcpy(data, r + DATA_POS, DATA_TAM);
    data[DATA_TAM] = '\0';
    memcpy(evento, r + EVENTO_POS, EVENTO_TAM);
    evento[EVENTO_TAM] = '\0';
    memcpy(sessao, r + SESSAO_POS, SESSAO_TAM);
    sessao[SESSAO_TAM] = '\0';

    printf("Registro %d | ID %d | data %s | evento %.16s | produto %d | usuario %d | sessao %.36s\n",
           numero, lerChave(r), data, evento,
           lerChave(r + PRODUTO_POS), lerChave(r + USUARIO_POS), sessao);
}

void mostrarProduto(const unsigned char r[], int numero) {
    unsigned long long categoriaId;
    unsigned int preco;
    char categoria[CATEGORIA_TAM + 1], marca[MARCA_TAM + 1];

    memcpy(&categoriaId, r + CATEGORIA_ID_POS, sizeof(categoriaId));
    memcpy(&preco, r + PRECO_POS, sizeof(preco));
    memcpy(categoria, r + CATEGORIA_POS, CATEGORIA_TAM);
    categoria[CATEGORIA_TAM] = '\0';
    memcpy(marca, r + MARCA_POS, MARCA_TAM);
    marca[MARCA_TAM] = '\0';

    printf("Registro %d | produto %d | categoria ID %llu | categoria %.64s | marca %.32s | preco R$ %.2f\n",
           numero, lerChave(r), categoriaId, categoria, marca, preco / 100.0);
}

/* Mostra os primeiros 20 registros ou todos, conforme a opcao. */
void mostrarArquivo(const char nome[], int tamanho, int ehAcesso, int todos) {
    FILE *arquivo = fopen(nome, "rb");
    unsigned char registro[TAM_PRODUTO];
    int total, limite, i;

    if (arquivo == NULL) {
        printf("Nao foi possivel abrir %s.\n", nome);
        return;
    }

    total = contarRegistros(arquivo, tamanho);
    if (total < 0) {
        printf("Arquivo invalido.\n");
        fclose(arquivo);
        return;
    }

    printf("\n%s tem %d registros.\n", nome, total);
    limite = todos ? total : (total < 20 ? total : 20);

    for (i = 0; i < limite; i++) {
        if (!lerRegistro(arquivo, tamanho, i, registro)) break;
        if (ehAcesso) mostrarAcesso(registro, i);
        else mostrarProduto(registro, i);
    }

    if (!todos && total > limite)
        printf("Foram mostrados apenas os primeiros %d registros.\n", limite);

    fclose(arquivo);
}

/* Insere um registro na posicao certa para manter as chaves ordenadas. */
int inserirOrdenado(const char nome[], int tamanho,
                    const unsigned char novo[], int chave) {
    FILE *entrada = fopen(nome, "rb");
    FILE *saida;
    char temporario[100];
    unsigned char atual[TAM_PRODUTO];
    int total, i, inseriu = 0;

    if (entrada == NULL) {
        printf("Nao foi possivel abrir %s.\n", nome);
        return 0;
    }

    total = contarRegistros(entrada, tamanho);
    if (total < 0) {
        fclose(entrada);
        printf("Arquivo invalido.\n");
        return 0;
    }

    snprintf(temporario, sizeof(temporario), "%s.tmp", nome);
    saida = fopen(temporario, "wb");
    if (saida == NULL) {
        fclose(entrada);
        return 0;
    }

    for (i = 0; i < total; i++) {
        if (!lerRegistro(entrada, tamanho, i, atual)) {
            fclose(entrada);
            fclose(saida);
            remove(temporario);
            return 0;
        }

        if (lerChave(atual) == chave) {
            printf("Ja existe um registro com essa chave.\n");
            fclose(entrada);
            fclose(saida);
            remove(temporario);
            return 0;
        }

        if (!inseriu && lerChave(atual) > chave) {
            fwrite(novo, 1, (size_t)tamanho, saida);
            inseriu = 1;
        }
        fwrite(atual, 1, (size_t)tamanho, saida);
    }

    if (!inseriu) fwrite(novo, 1, (size_t)tamanho, saida);

    fclose(entrada);
    fclose(saida);

    if (remove(nome) != 0 || rename(temporario, nome) != 0) {
        printf("Erro ao atualizar o arquivo. Verifique o arquivo temporario %s.\n", temporario);
        return 0;
    }

    printf("Registro inserido com sucesso.\n");
    return 1;
}

/* Um acesso novo sempre recebe o maior ID, entao seu lugar ja e o fim do arquivo: nao e preciso reescreve-lo. */
int acrescentarNoFim(const char nome[], int tamanho, const unsigned char novo[]) {
    FILE *arquivo = fopen(nome, "ab");
    int ok;

    if (arquivo == NULL) {
        printf("Nao foi possivel abrir %s.\n", nome);
        return 0;
    }

    ok = fwrite(novo, 1, (size_t)tamanho, arquivo) == (size_t)tamanho;
    fclose(arquivo);

    if (ok) printf("Registro inserido com sucesso.\n");
    return ok;
}

/* Cria um registro de acesso usando os campos conhecidos do arquivo original. */
int criarAcesso(unsigned char r[], int id) {
    char data[100], evento[100], sessao[100];
    int produto, usuario;

    memset(r, ' ', TAM_ACESSO);
    gravarInt(r, 0, id);

    lerTexto(data, sizeof(data), "Data/hora (AAAA-MM-DD HH:MM:SS): ");
    lerTexto(evento, sizeof(evento), "Evento (view/cart/remove/purchase): ");
    if (!lerInteiro("ID do produto: ", &produto)) return 0;
    if (!lerInteiro("ID do usuario: ", &usuario)) return 0;
    lerTexto(sessao, sizeof(sessao), "Sessao (ate 36 caracteres): ");

    if (strlen(data) != DATA_TAM || strlen(evento) > EVENTO_TAM ||
        strlen(sessao) > SESSAO_TAM) {
        printf("Confira o tamanho da data (19), evento (ate 16) e sessao (ate 36).\n");
        return 0;
    }

    memcpy(r + DATA_POS, data, DATA_TAM);
    gravarCampoTexto(r, EVENTO_POS, EVENTO_TAM, evento);
    gravarInt(r, PRODUTO_POS, produto);
    gravarInt(r, USUARIO_POS, usuario);
    gravarCampoTexto(r, SESSAO_POS, SESSAO_TAM, sessao);
    r[83] = '\n';
    return 1;
}

/* Cria um registro de produto usando o formato binario original. */
int criarProduto(unsigned char r[], int id) {
    char categoria[100], marca[100], linha[100], extra;
    unsigned long long categoriaId;
    unsigned int preco;
    int centavos;

    memset(r, ' ', TAM_PRODUTO);
    gravarInt(r, 0, id);

    printf("ID da categoria (numero): ");
    lerTexto(linha, sizeof(linha), "");
    if (sscanf(linha, "%llu %c", &categoriaId, &extra) != 1) {
        printf("ID de categoria invalido.\n");
        return 0;
    }

    lerTexto(categoria, sizeof(categoria), "Codigo da categoria: ");
    lerTexto(marca, sizeof(marca), "Marca: ");
    if (!lerInteiro("Preco em centavos (30114 = R$ 301,14): ", &centavos))
        return 0;

    if (strlen(categoria) > CATEGORIA_TAM || strlen(marca) > MARCA_TAM ||
        centavos < 0) {
        printf("Categoria/marca muito longa ou preco negativo.\n");
        return 0;
    }

    memcpy(r + CATEGORIA_ID_POS, &categoriaId, sizeof(categoriaId));
    gravarCampoTexto(r, CATEGORIA_POS, CATEGORIA_TAM, categoria);
    gravarCampoTexto(r, MARCA_POS, MARCA_TAM, marca);
    preco = (unsigned int)centavos;
    memcpy(r + PRECO_POS, &preco, sizeof(preco));
    r[112] = '\n';
    return 1;
}

/* Opcoes 9 e 10: insercao pelo teclado. */
void menuInserir(int ehAcesso, Indice *indice) {
    const char *nome = ehAcesso ? ARQ_ACESSOS : ARQ_PRODUTOS;
    const char *nomeIndice = ehAcesso ? IDX_ACESSOS : IDX_PRODUTOS;
    int tamanho = ehAcesso ? TAM_ACESSO : TAM_PRODUTO;
    unsigned char registro[TAM_PRODUTO];
    FILE *arquivo;
    int total, id, ultimo = 0, ok, inseriu;

    if (ehAcesso) {
        arquivo = fopen(nome, "rb");
        if (arquivo == NULL) {
            printf("Arquivo %s nao encontrado.\n", nome);
            return;
        }
        total = contarRegistros(arquivo, tamanho);
        if (total > 0) {
            if (!lerRegistro(arquivo, tamanho, total - 1, registro)) {
                fclose(arquivo);
                return;
            }
            ultimo = lerChave(registro);
        }
        fclose(arquivo);

        if (ultimo == INT_MAX) {
            printf("Nao ha mais IDs disponiveis.\n");
            return;
        }
        id = ultimo + 1;
        printf("O ID deste acesso sera %d.\n", id);
        ok = criarAcesso(registro, id);
    } else {
        if (!lerInteiro("ID do produto: ", &id)) return;
        ok = criarProduto(registro, id);
    }

    if (!ok) return;

    if (ehAcesso)
        inseriu = acrescentarNoFim(nome, tamanho, registro);
    else
        inseriu = inserirOrdenado(nome, tamanho, registro, id);

    if (inseriu)
        reconstruirIndice(nome, nomeIndice, tamanho, indice);
}

/* Remove um registro e copia os outros para um arquivo temporario. */
int removerRegistro(const char nome[], int tamanho, int chave) {
    FILE *entrada = fopen(nome, "rb");
    FILE *saida;
    char temporario[100];
    unsigned char r[TAM_PRODUTO];
    int total, i, removeu = 0;

    if (entrada == NULL) return 0;
    total = contarRegistros(entrada, tamanho);
    if (total < 0) {
        fclose(entrada);
        return 0;
    }

    snprintf(temporario, sizeof(temporario), "%s.tmp", nome);
    saida = fopen(temporario, "wb");
    if (saida == NULL) {
        fclose(entrada);
        return 0;
    }

    for (i = 0; i < total; i++) {
        if (!lerRegistro(entrada, tamanho, i, r)) {
            fclose(entrada);
            fclose(saida);
            remove(temporario);
            return 0;
        }
        if (!removeu && lerChave(r) == chave) {
            removeu = 1;
        } else {
            fwrite(r, 1, (size_t)tamanho, saida);
        }
    }

    fclose(entrada);
    fclose(saida);

    if (!removeu) {
        remove(temporario);
        printf("Registro nao encontrado.\n");
        return 0;
    }

    if (remove(nome) != 0 || rename(temporario, nome) != 0) {
        printf("Erro ao atualizar o arquivo.\n");
        return 0;
    }

    printf("Registro removido.\n");
    return 1;
}

void menuBusca(int ehAcesso, int comIndice, const Indice *indice) {
    const char *nome = ehAcesso ? ARQ_ACESSOS : ARQ_PRODUTOS;
    int tamanho = ehAcesso ? TAM_ACESSO : TAM_PRODUTO;
    int chave, posicao;
    unsigned char r[TAM_PRODUTO];
    FILE *arquivo;
    int encontrou;

    if (!lerInteiro("Informe a chave/ID: ", &chave)) return;

    if (comIndice)
        encontrou = buscaPorIndice(nome, indice, tamanho, chave, &posicao);
    else
        encontrou = buscaDireta(nome, tamanho, chave, &posicao);

    if (!encontrou) {
        printf("Registro nao encontrado.\n");
        return;
    }

    arquivo = fopen(nome, "rb");
    if (arquivo == NULL) return;
    if (lerRegistro(arquivo, tamanho, posicao, r)) {
        if (ehAcesso) mostrarAcesso(r, posicao);
        else mostrarProduto(r, posicao);
    }
    fclose(arquivo);
}

void mostrarMenu(void) {
    puts("\n===== SISTEMA DE ARQUIVOS INDEXADOS =====");
    puts("1. Mostrar primeiros acessos");
    puts("2. Mostrar todos os acessos");
    puts("3. Mostrar primeiros produtos");
    puts("4. Mostrar todos os produtos");
    puts("5. Busca binaria direta em acessos");
    puts("6. Busca binaria direta em produtos");
    puts("7. Busca por indice parcial em acessos");
    puts("8. Busca por indice parcial em produtos");
    puts("9. Inserir acesso");
    puts("10. Inserir produto");
    puts("11. Remover acesso");
    puts("12. Remover produto");
    puts("13. Recriar e salvar os indices");
    puts("0. Sair");
}

int main(void) {
    char linha[50];
    int opcao, chave;
    Indice indiceAcessos = {NULL, 0, 0};
    Indice indiceProdutos = {NULL, 0, 0};

    /* Confere se os dois arquivos de dados estao na pasta. */
    FILE *arquivo = fopen(ARQ_ACESSOS, "rb");
    if (arquivo == NULL) {
        printf("Coloque %s na pasta do programa.\n", ARQ_ACESSOS);
        return 1;
    }
    fclose(arquivo);

    arquivo = fopen(ARQ_PRODUTOS, "rb");
    if (arquivo == NULL) {
        printf("Coloque %s na pasta do programa.\n", ARQ_PRODUTOS);
        return 1;
    }
    fclose(arquivo);

    /* O enunciado pede que o indice seja carregado quando o programa e aberto. */
    abrirIndice(ARQ_ACESSOS, IDX_ACESSOS, TAM_ACESSO, &indiceAcessos);
    abrirIndice(ARQ_PRODUTOS, IDX_PRODUTOS, TAM_PRODUTO, &indiceProdutos);

    do {
        mostrarMenu();
        lerTexto(linha, sizeof(linha), "Opcao: ");
        opcao = atoi(linha);

        switch (opcao) {
            case 1: mostrarArquivo(ARQ_ACESSOS, TAM_ACESSO, 1, 0); break;
            case 2: mostrarArquivo(ARQ_ACESSOS, TAM_ACESSO, 1, 1); break;
            case 3: mostrarArquivo(ARQ_PRODUTOS, TAM_PRODUTO, 0, 0); break;
            case 4: mostrarArquivo(ARQ_PRODUTOS, TAM_PRODUTO, 0, 1); break;
            case 5: menuBusca(1, 0, &indiceAcessos); break;
            case 6: menuBusca(0, 0, &indiceProdutos); break;
            case 7: menuBusca(1, 1, &indiceAcessos); break;
            case 8: menuBusca(0, 1, &indiceProdutos); break;
            case 9: menuInserir(1, &indiceAcessos); break;
            case 10: menuInserir(0, &indiceProdutos); break;
            case 11:
                if (lerInteiro("ID do acesso a remover: ", &chave) &&
                    removerRegistro(ARQ_ACESSOS, TAM_ACESSO, chave))
                    reconstruirIndice(ARQ_ACESSOS, IDX_ACESSOS, TAM_ACESSO, &indiceAcessos);
                break;
            case 12:
                if (lerInteiro("ID do produto a remover: ", &chave) &&
                    removerRegistro(ARQ_PRODUTOS, TAM_PRODUTO, chave))
                    reconstruirIndice(ARQ_PRODUTOS, IDX_PRODUTOS, TAM_PRODUTO, &indiceProdutos);
                break;
            case 13:
                if (reconstruirIndice(ARQ_ACESSOS, IDX_ACESSOS, TAM_ACESSO, &indiceAcessos) &&
                    reconstruirIndice(ARQ_PRODUTOS, IDX_PRODUTOS, TAM_PRODUTO, &indiceProdutos))
                    puts("Indices salvos com sucesso.");
                break;
            case 0: puts("Programa encerrado."); break;
            default: puts("Opcao invalida."); break;
        }
    } while (opcao != 0);

    /* O enunciado pede que o indice seja gravado no final da execucao. */
    if (salvarIndice(IDX_ACESSOS, &indiceAcessos) && salvarIndice(IDX_PRODUTOS, &indiceProdutos))
        puts("Indices gravados em disco.");

    free(indiceAcessos.itens);
    free(indiceProdutos.itens);
    return 0;
}
