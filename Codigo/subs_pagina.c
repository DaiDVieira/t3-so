#include <stdio.h>
#include <stdlib.h>

#include "tabpag.h"
#include "so.h"
#include "subs_pagina.h"

// ---------------------------------------------------------------------
// FILA DE QUADROS - IMPLEMENTAÇÃO DE SUBSTITUIÇÃO POR FIFO
// ---------------------------------------------------------------------

// FUNÇÕES AUXILIARES DE FILA

FIFO* fila_cria (void){
    FIFO *f = (FIFO*) malloc(sizeof(FIFO));
    f->inicio = NULL;
    f->fim = NULL;
    return f;
}

void fila_libera (FIFO *f){
    Lst_quadros_FIFO *l = f->inicio;
    while (l != NULL){
        Lst_quadros_FIFO *aux = l->prox;
        free(l);
        l = aux;
    }
    free(f);
}

int fila_vazia (FIFO *f){
    return (f->inicio == NULL);
}

void fila_insere (FIFO *f, int id){
    Lst_quadros_FIFO *novo = (Lst_quadros_FIFO*) malloc(sizeof(Lst_quadros_FIFO));
    novo->id_quadro = id;
    novo->prox = NULL;
    if(!fila_vazia(f))
        f->fim->prox = novo;
    else
        f->inicio = novo;
    f->fim = novo;
}

int fila_retira (FIFO *f){
    Lst_quadros_FIFO *l = f->inicio;
    if (fila_vazia(f)) {
        printf("A fila ja esta vazia\n");
        return -1;
    }
    int v = l->id_quadro;
    f->inicio = l->prox;
    if (fl_vazia(f))
        f->fim = NULL;
    free(l);
    return v;
}

// FUNÇÃO DE SUBSTITUIÇÃO POR FIFO

int FIFO_quadro_a_substituir(FIFO *f){
    return fila_retira(f);
}


// ---------------------------------------------------------------------
// LISTA DE QUADROS - IMPLEMENTAÇÃO DE SUBSTITUIÇÃO POR LRU
// ---------------------------------------------------------------------

// FUNÇÕES AUXILIARES DE LISTA

void lst_pag_libera(Lista_quadros* l){
    Lista_quadros* p = l;
    while(p != NULL){
        Lista_quadros* t = p->prox;
        free(p);
        p = t;
    }
}

int lst_pag_vazia(Lista_quadros* l){
    return (l == NULL);
}

Lista_quadros* lst_pag_insere_ordenado(Lista_quadros* l, int id, unsigned int env){
    Lista_quadros* novo;
    Lista_quadros* ant = NULL; /* ponteiro para elemento anterior */
    Lista_quadros* p = l; /* ponteiro para percorrer a lista */
    /* procura posição de inserção */
    while (p != NULL && p->envelhecimento <= env){ 
        ant = p; 
        p = p->prox; 
    }
    /* cria novo elemento */
    novo = (Lista_quadros*)malloc(sizeof(Lista_quadros));
    novo->id_quadro = id;
    novo->envelhecimento = env;
    /* encadeia elemento */
    if (ant == NULL){
        novo->prox = l; 
        l = novo; 
    }
    else {
        novo->prox = ant->prox;
        ant->prox = novo; 
    }
    return l;
}

Lista_quadros* lst_pag_retira_primeira(Lista_quadros* l){
    if (l == NULL)
        return l;

    Lista_quadros* p = l;
    l = p->prox; 

    free(p);
    return l;
}

Lista_quadros* lst_pag_busca(Lista_quadros* l, int id){
    Lista_quadros* p;
    for (p = l; p != NULL; p = p->prox){
        if(p->id_quadro == id)
            return p;
    }
    return NULL;
}

unsigned int soma_bit_mais_significativo(unsigned int valor){
    unsigned int mascara = 1, aux;
    while((aux = mascara << 1) != 0){
        mascara <<= 1;
    }
    return valor | mascara;     //soma valor com mascara = 1 no bit mais significativo
}

int *lista_paginas_alterar(Lista_quadros* l, int tam, int pag_processo[QUANT_PAGINAS], int id_proc){
    int j = 0;
    int *lista = (int *) malloc(tam * sizeof(int));
    for(int i = 0; i < tam; i++) lista[i] = -1;

    for(int i = 0; i < QUANT_PAGINAS; i++){
        if(pag_processo[i] == id_proc){
            lista[j] = i;
            j++;
        }
    }
    return lista;
}

void altera_envelhecimento(Lista_quadros* l, tabpag_t *tab, int *id_paginas){     /*chamada por irq_relogio*/
    Lista_quadros *aux = l;
    while(aux != NULL){
        for(int j = 0; j < tabpag_numero_pagina(tab); j++){
            if(aux->id_quadro == id_paginas[j]){
                aux->envelhecimento >>= 1;        //divide por 2
                int pagina = tabpag_encontra_pagina_pelo_quadro(tab, aux->id_quadro);
                if(tabpag_bit_acesso(tab, pagina)){
                    aux->envelhecimento = soma_bit_mais_significativo(aux->envelhecimento);
                    tabpag_zera_bit_acesso(tab, pagina);
                }
                aux = aux->prox;
            }
        }
    }
}

void atualiza_envelhecimento(Lista_quadros* l, tabpag_t *tab, int pag_processo[QUANT_PAGINAS], int id_proc){
    int tam = tabpag_numero_pagina(tab);
    int *lista_paginas = lista_paginas_alterar(l, tam, pag_processo, id_proc);
    altera_envelhecimento(l, tab, lista_paginas);
}

Lista_quadros* lst_pag_ordena(Lista_quadros* l){
    Lista_quadros* nova = NULL;
    Lista_quadros* p = l;
    while(p != NULL){
        nova = lst_pag_insere_ordenado(nova, p->id_quadro, p->envelhecimento);
        p = p->prox;
    }
    lst_pag_libera(l);   // libera lista antiga
    return nova;         // retorna lista ordenada
}

// FUNÇÃO DE SUBSTITUIÇÃO POR LRU

Lista_quadros* LRU_quadro_a_substituir(Lista_quadros* l, int *quadro){
    if(l == NULL) return -1;

    l = lst_pag_ordena(l);
    *quadro = l->id_quadro;

    l = lst_pag_retira_primeira(l);
    return l;
}

