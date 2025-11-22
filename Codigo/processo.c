#include <stdio.h>
#include <stdlib.h>
#include "processo.h"
#include "mmu.h"

processo_t* inicializa_processo(processo_t* processo, int id, int PC, int tam){
    if (PC < 0) {
      // t2: deveria escrever no PC do descritor do processo criado
        return NULL;
    }
    processo->id = id;
    processo->PC = PC;
    processo->regErro = 0;
    processo->erro = ERR_OK;
    processo->A = 0;
    processo->X = 0;
    processo->memIni = PC;
    processo->memTam = tam;
    processo->t_cpu = 0;
    processo->n_exec = 0;
    processo->prio = 0.5;
    processo->id_terminal = (id % 4) * 4;     //0-3, 4-7, 8-11, 12-15
    processo->espera_terminal = 0;     //Sem espera = 0, Le = 1, Escreve = 2
    processo->quantum = QUANTUM_INICIAL;
    processo->tab_pag = tabpag_cria();
    //processo->n_paginas = tam/TAM_PAGINA +1;
    processo->n_falha_paginas = 0;
    return processo;
}

int encontra_indice_processo(processo_t processos[MAX_PROCESSOS], int id){
    for(int i = 0; i < MAX_PROCESSOS; i++){
        if(processos[i].id == id)
            return i;
    }
    return -1;
}

void altera_estado_proc_tabela(processo_t processos[MAX_PROCESSOS], int id, estado_proc estado){
    int indice = encontra_indice_processo(processos, id);
    if(indice != -1){
        processos[indice].estado = estado;
    }
}

static char *nomes_estados[3] = {
  [bloqueado] =   "Bloqueado",
  [pronto] = "Pronto",
  [morto] = "Morto",
};

// retorna o nome da interrupção
char *estado_nome(estado_proc est)
{
  if (est < 0 || est > 3) return "DESCONHECIDA";
  return nomes_estados[est];
}

// ---------------------------------------------------------------------
// LISTA DE PROCESSOS
// ---------------------------------------------------------------------

void lst_libera(Lista_processos* l){
    Lista_processos* p = l;
    while(p != NULL){
        Lista_processos* t = p->prox;
        free(p);
        p = t;
    }
}

void lst_imprime(Lista_processos* l){
    Lista_processos* p;
    for (p = l; p != NULL; p = p->prox)
        console_printf("pid = %d prio = %.2f estado = %d\n", p->id, p->prio, p->estado);
}

int lst_vazia(Lista_processos* l){
    return (l == NULL);
}

Lista_processos* lst_altera_estado(Lista_processos* l, int id, estado_proc estado){
    Lista_processos* p;
    for(p = l; p != NULL; p = p->prox){
        if(p->id == id){
            p->estado = estado;
            return l;
        }
    }
    return l; //id invalido, lista inalterada
}

Lista_processos* lst_insere_ordenado(Lista_processos* l, int id, float prio){
    Lista_processos* novo;
    Lista_processos* ant = NULL; /* ponteiro para elemento anterior */
    Lista_processos* p = l; /* ponteiro para percorrer a lista */
    /* procura posição de inserção */
    while (p != NULL && p->prio <= prio){ 
        ant = p; 
        p = p->prox; 
    }
    /* cria novo elemento */
    novo = (Lista_processos*)malloc(sizeof(Lista_processos));
    novo->id = id;
    novo->prio = prio;
    novo->estado = pronto;
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

Lista_processos* lst_adicionar_final(Lista_processos* l, int id, float prio){
    console_printf("(proc_id adicionar %d)", id);
	Lista_processos* p = (Lista_processos*)malloc(sizeof(Lista_processos));
	if(p == NULL){
		printf("\nFalha ao alocar memoria\n");
		return NULL;
	}
    else{
        Lista_processos* aux = l;
        p->id = id;
        p->prio = prio;
        p->prox = NULL;
        if(!lst_vazia(l)){
            while(aux->prox != NULL) 
                aux = aux->prox;
            aux->prox = p;
            console_printf("aux id: %d prox id: %d", aux->id, aux->prox->id);
        }
        else{
            l = p;
        }
        console_printf("(proc_adicionado_final %d)", p->id);
    }
	return l;
}

Lista_processos* lst_retira(Lista_processos* l, int id){
    Lista_processos* ant = NULL;
    Lista_processos* p = l;
    while (p != NULL){ 
        if (p->id == id || p->estado == morto)
            break;
        ant = p;
        p = p->prox; 
    }
    if (p == NULL)
        return l;
    if (ant == NULL){
        l = p->prox; }
    else { 
        ant->prox = p->prox; 
    }
    free(p);
    return l;
}

Lista_processos* lst_busca(Lista_processos* l, int id){
    Lista_processos* p;
    for (p = l; p != NULL; p = p->prox){
        if(p->id == id)
            return p;
    }
    return NULL;
}