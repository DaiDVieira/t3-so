#ifndef SUBS_PAGINA_H
#define SUBS_PAGINA_H

struct lst_quadros {
    int id_quadro;
    struct lst_quadros* prox;
};
typedef struct lst_quadros Lst_quadros_FIFO;

struct fila{
    Lst_quadros_FIFO *inicio;
    Lst_quadros_FIFO *fim;   
};
typedef struct fila FIFO;

FIFO* fila_cria (void);
void fila_libera (FIFO* f);
int fila_vazia (FIFO* f);
void fila_insere (FIFO* f, int id);
int fila_busca(FIFO *f, int id);
int fila_retira (FIFO* f);
int FIFO_quadro_a_substituir(FIFO *f);


struct lista_quadros {
    int id_quadro;
    unsigned int envelhecimento;
    struct lista_quadros* prox;
};
typedef struct lista_quadros Lista_quadros;

void lst_pag_libera(Lista_quadros* l);
int lst_pag_vazia(Lista_quadros* l);
Lista_quadros* lst_pag_insere_ordenado(Lista_quadros* l, int id, unsigned int env);
Lista_quadros* lst_pag_retira_primeira(Lista_quadros* l);
Lista_quadros* lst_pag_busca(Lista_quadros* l, int id);

unsigned int soma_bit_mais_significativo(unsigned int valor);
/*int *lista_paginas_alterar(Lista_quadros* l, int tam, int pag_processo[QUANT_PAGINAS], int id_proc);
void altera_envelhecimento(Lista_quadros* l, tabpag_t *tab, int *id_paginas);
void atualiza_envelhecimento(Lista_quadros* l, tabpag_t *tab, int pag_processo[QUANT_PAGINAS], int id_proc);*/
void atualiza_envelhecimento(Lista_quadros* l, tabpag_t *tab, int quadro_processo[QUANT_QUADROS], int id_proc);
Lista_quadros* lst_pag_ordena(Lista_quadros* l);
Lista_quadros* LRU_quadro_a_substituir(Lista_quadros* l, int *quadro);

#endif
