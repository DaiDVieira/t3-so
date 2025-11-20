#ifndef PROCESSO_H
#define PROCESSO_H

#include "so.h"
#include "tabpag.h"

#define INI_MEM_PROC 100
#define MAX_PROCESSOS 4 //número máximo de processos

typedef enum { bloqueado, pronto, morto } estado_proc;

struct processo_t{
    int id;
    int PC;
    int A;
    int X;
    int regErro;
    err_t erro;
    int memIni;
    int memTam;
    int t_cpu;
    int n_exec;
    estado_proc estado;
    float prio;
    int id_terminal;
    int espera_terminal;
    int quantum;
    tabpag_t *tab_pag;
};
typedef struct processo_t processo_t;

struct lista_processos {
    int id;
    float prio;
    estado_proc estado;
    struct lista_processos* prox;
};
typedef struct lista_processos Lista_processos;

#define TIPOS_ESTADOS 3

//processo_t inicializa_init(processo_t processo);
//processo_t *inicializa_processos(processo_t processos[MAX_PROCESSOS]);
processo_t* inicializa_processo(processo_t* processo, int id, int PC, int tam);
int entrada_livre_tabela_proc(processo_t processos[MAX_PROCESSOS]);
int encontra_indice_processo(processo_t processos[MAX_PROCESSOS], int id);
void altera_estado_proc_tabela(processo_t processos[MAX_PROCESSOS], int id, estado_proc estado);
char *estado_nome(estado_proc est);

void lst_libera(Lista_processos* l);
void lst_imprime (Lista_processos* l);
Lista_processos* lst_altera_estado(Lista_processos* l, int id, estado_proc estado);
Lista_processos* lst_insere_ordenado (Lista_processos* l, int id, float prio);
Lista_processos* lst_adicionar_final(Lista_processos* l, int id, float prio);
Lista_processos* lst_retira (Lista_processos* l, int id);
Lista_processos* lst_busca(Lista_processos* l, int id);
void lst_atualiza_prioridades(Lista_processos *l);

#endif