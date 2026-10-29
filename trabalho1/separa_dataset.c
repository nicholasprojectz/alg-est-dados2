#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ARQUIVO_PRODUTOS "produtos.dat"
#define ARQUIVO_ACESSOS  "acessos.dat"

#define TAM_CODIGO_CATEGORIA 64
#define TAM_MARCA            32
#define TAM_DATA_HORA        19
#define TAM_TIPO_EVENTO      16
#define TAM_SESSAO           36

#define TAM_REGISTRO_PRODUTO (sizeof(uint32_t) + sizeof(uint64_t) + TAM_CODIGO_CATEGORIA \
                              + TAM_MARCA + sizeof(int32_t) + 1)
#define TAM_REGISTRO_ACESSO  (sizeof(uint32_t) + TAM_DATA_HORA + TAM_TIPO_EVENTO \
                              + sizeof(uint32_t) + sizeof(uint32_t) + TAM_SESSAO + 1)

#define TAM_MAX_LINHA      1024
#define QTD_COLUNAS_CSV    9
#define INTERVALO_PROGRESSO 1000000UL
#define POSICAO_VAZIA      SIZE_MAX

enum ColunaCsv {
    COL_EVENT_TIME,
    COL_EVENT_TYPE,
    COL_PRODUCT_ID,
    COL_CATEGORY_ID,
    COL_CATEGORY_CODE,
    COL_BRAND,
    COL_PRICE,
    COL_USER_ID,
    COL_USER_SESSION
};

typedef struct {
    uint32_t id_produto;
    uint64_t id_categoria;
    char     codigo_categoria[TAM_CODIGO_CATEGORIA];
    char     marca[TAM_MARCA];
    int32_t  preco_centavos;
} Produto;

typedef struct {
    uint32_t id_acesso;
    char     data_hora[TAM_DATA_HORA];
    char     tipo_evento[TAM_TIPO_EVENTO];
    uint32_t id_produto;
    uint32_t id_usuario;
    char     sessao_usuario[TAM_SESSAO];
} Acesso;

/* Produtos distintos ficam em um vetor; a tabela hash guarda a posição de cada id nesse vetor. */
typedef struct {
    Produto *produtos;
    size_t   quantidade;
    size_t   capacidade;
    size_t  *tabela;
    size_t   capacidade_tabela;
} CatalogoProdutos;

typedef struct {
    unsigned long linhas_lidas;
    unsigned long linhas_ignoradas;
    unsigned long acessos_gravados;
} Estatisticas;

static void encerrar_com_erro(const char *mensagem)
{
    fprintf(stderr, "Erro: %s\n", mensagem);
    exit(EXIT_FAILURE);
}

static void *alocar(size_t tamanho)
{
    void *memoria = malloc(tamanho);
    if (memoria == NULL)
        encerrar_com_erro("memoria insuficiente");
    return memoria;
}

static FILE *abrir_arquivo(const char *caminho, const char *modo)
{
    FILE *arquivo = fopen(caminho, modo);
    if (arquivo == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s'\n", caminho);
        exit(EXIT_FAILURE);
    }
    return arquivo;
}

/* ---------- Leitura do CSV ---------- */

static void remover_quebra_de_linha(char *linha)
{
    linha[strcspn(linha, "\r\n")] = '\0';
}

/* Diferente de strtok, preserva colunas vazias (category_code e brand vêm vazios com frequência). */
static int separar_colunas(char *linha, char *colunas[], int max_colunas)
{
    int quantidade = 0;
    char *inicio = linha;

    while (quantidade < max_colunas) {
        char *virgula = strchr(inicio, ',');
        colunas[quantidade++] = inicio;
        if (virgula == NULL)
            break;
        *virgula = '\0';
        inicio = virgula + 1;
    }
    return quantidade;
}

static int converter_inteiro(const char *texto, uint64_t *valor)
{
    char *fim;
    if (*texto == '\0')
        return 0;
    *valor = strtoull(texto, &fim, 10);
    return *fim == '\0';
}

static int converter_preco_em_centavos(const char *texto, int32_t *centavos)
{
    char *fim;
    double preco = strtod(texto, &fim);
    if (*texto == '\0' || *fim != '\0' || preco < 0)
        return 0;
    *centavos = (int32_t)(preco * 100.0 + 0.5);
    return 1;
}

static void copiar_texto_fixo(char *destino, size_t tamanho, const char *origem)
{
    size_t comprimento = strlen(origem);
    if (comprimento > tamanho)
        comprimento = tamanho;
    memset(destino, ' ', tamanho);
    memcpy(destino, origem, comprimento);
}

static int texto_fixo_vazio(const char *texto, size_t tamanho)
{
    for (size_t i = 0; i < tamanho; i++)
        if (texto[i] != ' ')
            return 0;
    return 1;
}

static int montar_registros(char *colunas[], uint32_t id_acesso, Produto *produto, Acesso *acesso)
{
    uint64_t id_produto, id_categoria, id_usuario;

    if (!converter_inteiro(colunas[COL_PRODUCT_ID], &id_produto) || id_produto > UINT32_MAX)
        return 0;
    if (!converter_inteiro(colunas[COL_USER_ID], &id_usuario) || id_usuario > UINT32_MAX)
        return 0;
    if (!converter_inteiro(colunas[COL_CATEGORY_ID], &id_categoria))
        return 0;
    if (!converter_preco_em_centavos(colunas[COL_PRICE], &produto->preco_centavos))
        return 0;

    produto->id_produto = (uint32_t)id_produto;
    produto->id_categoria = id_categoria;
    copiar_texto_fixo(produto->codigo_categoria, TAM_CODIGO_CATEGORIA, colunas[COL_CATEGORY_CODE]);
    copiar_texto_fixo(produto->marca, TAM_MARCA, colunas[COL_BRAND]);

    acesso->id_acesso = id_acesso;
    copiar_texto_fixo(acesso->data_hora, TAM_DATA_HORA, colunas[COL_EVENT_TIME]);
    copiar_texto_fixo(acesso->tipo_evento, TAM_TIPO_EVENTO, colunas[COL_EVENT_TYPE]);
    acesso->id_produto = (uint32_t)id_produto;
    acesso->id_usuario = (uint32_t)id_usuario;
    copiar_texto_fixo(acesso->sessao_usuario, TAM_SESSAO, colunas[COL_USER_SESSION]);
    return 1;
}

/* ---------- Gravação binária ---------- */

static void anexar_campo(unsigned char *registro, size_t *posicao, const void *campo, size_t tamanho)
{
    memcpy(registro + *posicao, campo, tamanho);
    *posicao += tamanho;
}

/* Grava campo a campo (e não a struct inteira) para o registro não carregar bytes de padding do compilador. */
static void gravar_produto(FILE *arquivo, const Produto *produto)
{
    unsigned char registro[TAM_REGISTRO_PRODUTO];
    size_t posicao = 0;

    anexar_campo(registro, &posicao, &produto->id_produto, sizeof produto->id_produto);
    anexar_campo(registro, &posicao, &produto->id_categoria, sizeof produto->id_categoria);
    anexar_campo(registro, &posicao, produto->codigo_categoria, TAM_CODIGO_CATEGORIA);
    anexar_campo(registro, &posicao, produto->marca, TAM_MARCA);
    anexar_campo(registro, &posicao, &produto->preco_centavos, sizeof produto->preco_centavos);
    registro[posicao] = '\n';

    if (fwrite(registro, TAM_REGISTRO_PRODUTO, 1, arquivo) != 1)
        encerrar_com_erro("falha ao gravar " ARQUIVO_PRODUTOS);
}

static void gravar_acesso(FILE *arquivo, const Acesso *acesso)
{
    unsigned char registro[TAM_REGISTRO_ACESSO];
    size_t posicao = 0;

    anexar_campo(registro, &posicao, &acesso->id_acesso, sizeof acesso->id_acesso);
    anexar_campo(registro, &posicao, acesso->data_hora, TAM_DATA_HORA);
    anexar_campo(registro, &posicao, acesso->tipo_evento, TAM_TIPO_EVENTO);
    anexar_campo(registro, &posicao, &acesso->id_produto, sizeof acesso->id_produto);
    anexar_campo(registro, &posicao, &acesso->id_usuario, sizeof acesso->id_usuario);
    anexar_campo(registro, &posicao, acesso->sessao_usuario, TAM_SESSAO);
    registro[posicao] = '\n';

    if (fwrite(registro, TAM_REGISTRO_ACESSO, 1, arquivo) != 1)
        encerrar_com_erro("falha ao gravar " ARQUIVO_ACESSOS);
}

/* ---------- Catálogo de produtos distintos ---------- */

static size_t espalhar_id(uint32_t id)
{
    id ^= id >> 16;
    id *= 0x45d9f3bU;
    id ^= id >> 16;
    id *= 0x45d9f3bU;
    id ^= id >> 16;
    return id;
}

static void catalogo_iniciar(CatalogoProdutos *catalogo)
{
    catalogo->quantidade = 0;
    catalogo->capacidade = 1024;
    catalogo->produtos = alocar(catalogo->capacidade * sizeof(Produto));

    catalogo->capacidade_tabela = 2048;
    catalogo->tabela = alocar(catalogo->capacidade_tabela * sizeof(size_t));
    for (size_t i = 0; i < catalogo->capacidade_tabela; i++)
        catalogo->tabela[i] = POSICAO_VAZIA;
}

static size_t catalogo_localizar_slot(const CatalogoProdutos *catalogo, uint32_t id_produto)
{
    size_t mascara = catalogo->capacidade_tabela - 1;
    size_t slot = espalhar_id(id_produto) & mascara;

    while (catalogo->tabela[slot] != POSICAO_VAZIA
           && catalogo->produtos[catalogo->tabela[slot]].id_produto != id_produto)
        slot = (slot + 1) & mascara;
    return slot;
}

static void catalogo_dobrar_tabela(CatalogoProdutos *catalogo)
{
    free(catalogo->tabela);
    catalogo->capacidade_tabela *= 2;
    catalogo->tabela = alocar(catalogo->capacidade_tabela * sizeof(size_t));
    for (size_t i = 0; i < catalogo->capacidade_tabela; i++)
        catalogo->tabela[i] = POSICAO_VAZIA;

    for (size_t i = 0; i < catalogo->quantidade; i++) {
        size_t slot = catalogo_localizar_slot(catalogo, catalogo->produtos[i].id_produto);
        catalogo->tabela[slot] = i;
    }
}

static void catalogo_adicionar(CatalogoProdutos *catalogo, size_t slot, const Produto *produto)
{
    if (catalogo->quantidade == catalogo->capacidade) {
        catalogo->capacidade *= 2;
        catalogo->produtos = realloc(catalogo->produtos, catalogo->capacidade * sizeof(Produto));
        if (catalogo->produtos == NULL)
            encerrar_com_erro("memoria insuficiente");
    }
    catalogo->produtos[catalogo->quantidade] = *produto;
    catalogo->tabela[slot] = catalogo->quantidade;
    catalogo->quantidade++;

    if (catalogo->quantidade * 2 > catalogo->capacidade_tabela)
        catalogo_dobrar_tabela(catalogo);
}

/* O CSV é cronológico: o preço mais recente prevalece, e marca/categoria só são preenchidas se ainda estiverem vazias. */
static void catalogo_atualizar(Produto *existente, const Produto *novo)
{
    existente->preco_centavos = novo->preco_centavos;
    if (texto_fixo_vazio(existente->codigo_categoria, TAM_CODIGO_CATEGORIA))
        memcpy(existente->codigo_categoria, novo->codigo_categoria, TAM_CODIGO_CATEGORIA);
    if (texto_fixo_vazio(existente->marca, TAM_MARCA))
        memcpy(existente->marca, novo->marca, TAM_MARCA);
}

static void catalogo_registrar(CatalogoProdutos *catalogo, const Produto *produto)
{
    size_t slot = catalogo_localizar_slot(catalogo, produto->id_produto);

    if (catalogo->tabela[slot] == POSICAO_VAZIA)
        catalogo_adicionar(catalogo, slot, produto);
    else
        catalogo_atualizar(&catalogo->produtos[catalogo->tabela[slot]], produto);
}

static int comparar_produtos_por_id(const void *a, const void *b)
{
    uint32_t id_a = ((const Produto *)a)->id_produto;
    uint32_t id_b = ((const Produto *)b)->id_produto;
    return (id_a > id_b) - (id_a < id_b);
}

static size_t catalogo_gravar_ordenado(CatalogoProdutos *catalogo, const char *caminho)
{
    FILE *arquivo = abrir_arquivo(caminho, "wb");

    free(catalogo->tabela);
    catalogo->tabela = NULL;
    qsort(catalogo->produtos, catalogo->quantidade, sizeof(Produto), comparar_produtos_por_id);

    for (size_t i = 0; i < catalogo->quantidade; i++)
        gravar_produto(arquivo, &catalogo->produtos[i]);

    fclose(arquivo);
    return catalogo->quantidade;
}

static void catalogo_liberar(CatalogoProdutos *catalogo)
{
    free(catalogo->produtos);
    free(catalogo->tabela);
}

/* ---------- Processamento ---------- */

static int ler_linha(FILE *arquivo, char *linha)
{
    if (fgets(linha, TAM_MAX_LINHA, arquivo) == NULL)
        return 0;

    if (strchr(linha, '\n') == NULL && !feof(arquivo)) {
        int caractere;
        while ((caractere = fgetc(arquivo)) != '\n' && caractere != EOF)
            ;
        linha[0] = '\0';
    }
    return 1;
}

static Estatisticas processar_csv(FILE *csv, FILE *arquivo_acessos,
                                  CatalogoProdutos *catalogo, unsigned long limite_linhas)
{
    Estatisticas estatisticas = {0, 0, 0};
    char linha[TAM_MAX_LINHA];
    char *colunas[QTD_COLUNAS_CSV];
    Produto produto;
    Acesso acesso;

    if (!ler_linha(csv, linha))
        encerrar_com_erro("arquivo CSV vazio");

    while ((limite_linhas == 0 || estatisticas.linhas_lidas < limite_linhas) && ler_linha(csv, linha)) {
        estatisticas.linhas_lidas++;
        remover_quebra_de_linha(linha);

        uint32_t proximo_id_acesso = (uint32_t)(estatisticas.acessos_gravados + 1);
        if (separar_colunas(linha, colunas, QTD_COLUNAS_CSV) != QTD_COLUNAS_CSV
            || !montar_registros(colunas, proximo_id_acesso, &produto, &acesso)) {
            estatisticas.linhas_ignoradas++;
            continue;
        }

        gravar_acesso(arquivo_acessos, &acesso);
        estatisticas.acessos_gravados++;
        catalogo_registrar(catalogo, &produto);

        if (estatisticas.linhas_lidas % INTERVALO_PROGRESSO == 0)
            fprintf(stderr, "\r%lu linhas processadas...", estatisticas.linhas_lidas);
    }
    fprintf(stderr, "\r");
    return estatisticas;
}

static unsigned long ler_limite(int argc, char *argv[])
{
    char *fim;
    unsigned long limite;

    if (argc < 3)
        return 0;
    limite = strtoul(argv[2], &fim, 10);
    if (*argv[2] == '\0' || *fim != '\0')
        encerrar_com_erro("limite de linhas invalido");
    return limite;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Uso: %s <arquivo.csv> [limite_de_linhas]\n", argv[0]);
        return EXIT_FAILURE;
    }

    unsigned long limite_linhas = ler_limite(argc, argv);
    FILE *csv = abrir_arquivo(argv[1], "r");
    FILE *arquivo_acessos = abrir_arquivo(ARQUIVO_ACESSOS, "wb");
    CatalogoProdutos catalogo;

    catalogo_iniciar(&catalogo);
    Estatisticas estatisticas = processar_csv(csv, arquivo_acessos, &catalogo, limite_linhas);
    fclose(csv);
    fclose(arquivo_acessos);

    size_t produtos_gravados = catalogo_gravar_ordenado(&catalogo, ARQUIVO_PRODUTOS);
    catalogo_liberar(&catalogo);

    printf("Linhas lidas do CSV:  %lu\n", estatisticas.linhas_lidas);
    printf("Linhas ignoradas:     %lu\n", estatisticas.linhas_ignoradas);
    printf("%-21s %lu registros de %zu bytes\n", ARQUIVO_ACESSOS ":",
           estatisticas.acessos_gravados, (size_t)TAM_REGISTRO_ACESSO);
    printf("%-21s %zu registros de %zu bytes\n", ARQUIVO_PRODUTOS ":",
           produtos_gravados, (size_t)TAM_REGISTRO_PRODUTO);
    return EXIT_SUCCESS;
}
