// so.c
// sistema operacional
// simulador de computador
// so25b

// ---------------------------------------------------------------------
// INCLUDES {{{1
// ---------------------------------------------------------------------

#include "so.h"
#include "cpu.h"
#include "dispositivos.h"
#include "err.h"
#include "irq.h"
#include "memoria.h"
#include "programa.h"
#include "tabpag.h"
#include "processo.h"

#include <stdlib.h>
#include <stdbool.h>


// ---------------------------------------------------------------------
// CONSTANTES E TIPOS {{{1
// ---------------------------------------------------------------------

// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas
#define TERMINAIS 4

// Não tem processos nem memória virtual, mas é preciso usar a paginação,
//   pelo menos para implementar relocação, já que os programas estão sendo
//   todos montados para serem executados no endereço 0 e o endereço 0
//   físico é usado pelo hardware nas interrupções.
// Os programas estão sendo carregados no início de um quadro, e usam quantos
//   quadros forem necessárias. Para isso a variável quadro_livre contém
//   o número do primeiro quadro da memória principal que ainda não foi usado.
//   Na carga do processo, a tabela de páginas (deveria ter uma por processo,
//   mas não tem processo) é alterada para que o endereço virtual 0 resulte
//   no quadro onde o programa foi carregado. Com isso, o programa carregado
//   é acessível, mas o acesso ao anterior é perdido.

// t3: a interface de algumas funções que manipulam memória teve que ser alterada,
//   para incluir o processo ao qual elas se referem. Para isso, é necessário um
//   tipo de dados para identificar um processo. Neste código, não tem processos
//   implementados, e não tem um tipo para isso. Foi usado o tipo int.
//   É necessário também um valor para representar um processo inexistente.
//   Foi usado o valor -1. Altere para o seu tipo, ou substitua os usos de
//   processo_t e NENHUM_PROCESSO para o seu tipo.
//   ALGUM_PROCESSO serve para representar um processo que não é NENHUM. Só tem
//   algum sentido enquanto não tem implementação de processos.

//typedef int processo_t;
//#define NENHUM_PROCESSO -1
//#define ALGUM_PROCESSO 0

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  mmu_t *mmu;
  es_t *es;
  console_t *console;
  bool erro_interno;

  int regA, regX, regPC, regERRO, regComplemento; // cópia do estado da CPU
  // t2: tabela de processos, processo corrente, pendências, etc
   processo_t processos[MAX_PROCESSOS];
  processo_t *processo_corrente;
  Lista_processos* ini_fila_proc;
  Lista_processos* ini_fila_proc_prontos;
  int cont_processos; 
  bool dispositivos_livres[TERMINAIS]; 
  escalonador_atual escalonador;

  // primeiro quadro da memória que está livre (quadros anteriores estão ocupados)
  // t3: com memória virtual, o controle de memória livre e ocupada deve ser mais
  //     completo que isso
  int quadro_livre;
  // uma tabela de páginas para poder usar a MMU
  // t3: com processos, não tem esta tabela global, tem que ter uma para
  //     cada processo
  tabpag_t *tabpag_global;
};


// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// no t3, foi adicionado o 'processo' aos argumentos dessas funções 
// carrega o programa contido no arquivo para memória virtual de um processo
// retorna o endereço virtual inicial de execução
static int so_carrega_programa(so_t *self, processo_t *processo, char *nome_do_executavel);
// copia para str da memória do processo, até copiar um 0 (retorna true) ou tam bytes
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam], int end_virt, processo_t *processo);


// ---------------------------------------------------------------------
// CRIAÇÃO {{{1
// ---------------------------------------------------------------------

so_t *so_cria_valores_processo(so_t *self){
  self->cont_processos = 0;
  self->ini_fila_proc = NULL;
  self->processo_corrente = NULL;
  self->ini_fila_proc_prontos = NULL;
  for(int i = 0; i < MAX_PROCESSOS; i++){
    self->processos[i].estado = morto;
  }

  for(int i = 0; i < TERMINAIS; i++){
    self->dispositivos_livres[i] = true;
  }
  self->escalonador = simples;
  return self;
}

so_t *so_cria(cpu_t *cpu, mem_t *mem, mmu_t *mmu, es_t *es, console_t *console)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->mmu = mmu;
  self->es = es;
  self->console = console;
  self->erro_interno = false;

  self = so_cria_valores_processo(self);

  // quando a CPU executar uma instrução CHAMAC, deve chamar a função
  //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

  // inicializa a tabela de páginas global, e entrega ela para a MMU
  // t3: com processos, essa tabela não existiria, teria uma por processo, que
  //     deve ser colocada na MMU quando o processo é despachado para execução
  self->tabpag_global = tabpag_cria();
  mmu_define_tabpag(self->mmu, self->tabpag_global);

  return self;
}

void so_destroi(so_t *self)
{
  cpu_define_chamaC(self->cpu, NULL, NULL);
  free(self);
}


// ---------------------------------------------------------------------
// TRATAMENTO DE INTERRUPÇÃO {{{1
// ---------------------------------------------------------------------

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);

// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da interrupção
//   (e executar o código de usuário) ou executar PARA e ficar suspensa até receber
//   outra interrupção
static int so_trata_interrupcao(void *argC, int reg_A)
{
  so_t *self = argC;
  irq_t irq = reg_A;
  // esse print polui bastante, recomendo tirar quando estiver com mais confiança
  console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
  // salva o estado da cpu no descritor do processo que foi interrompido
  so_salva_estado_da_cpu(self);
  // faz o atendimento da interrupção
  so_trata_irq(self, irq);
  // faz o processamento independente da interrupção
  so_trata_pendencias(self);
  // escolhe o próximo processo a executar
  so_escalona(self);
  // recupera o estado do processo escolhido
  return so_despacha(self);
}

static void so_salva_estado_da_cpu(so_t *self)
{
  // t2: salva os registradores que compõem o estado da cpu no descritor do
  //   processo corrente. os valores dos registradores foram colocados pela
  //   CPU na memória, nos endereços CPU_END_PC etc. O registrador X foi salvo
  //   pelo tratador de interrupção (ver trata_irq.asm) no endereço 59
  // se não houver processo corrente, não faz nada
  if(self->processo_corrente != NULL && self->processo_corrente->estado != morto){
    if (mem_le(self->mem, CPU_END_A, &self->processo_corrente->A) != ERR_OK
        || mem_le(self->mem, CPU_END_PC, &self->processo_corrente->PC) != ERR_OK
        || mem_le(self->mem, CPU_END_erro, &self->processo_corrente->regErro) != ERR_OK
        || mem_le(self->mem, 59, &self->processo_corrente->X) != ERR_OK) {
      console_printf("SO: erro na leitura dos registradores");
      self->erro_interno = true;
    }
  }
}

static void so_trata_pendencias(so_t *self)
{
  // t2: realiza ações que não são diretamente ligadas com a interrupção que
  //   está sendo atendida:
  // - E/S pendente
  // - desbloqueio de processos
  // - contabilidades
  // - etc
}

//funcao auxiliar temporaria para escalonamento
processo_t* so_proximo_pronto(so_t* self);
Lista_processos* so_coloca_fila_pronto(so_t* self, processo_t* processo);

static void so_escalona(so_t *self)
{
  // escolhe o próximo processo a executar, que passa a ser o processo
  //   corrente; pode continuar sendo o mesmo de antes ou não
  // t2: na primeira versão, escolhe um processo pronto caso o processo
  //   corrente não possa continuar executando, senão deixa o mesmo processo.
  //   depois, implementa um escalonador melhor
  if(self->processo_corrente != NULL && self->escalonador == prioridade){
    if(self->ini_fila_proc_prontos == NULL || self->processo_corrente->prio > self->ini_fila_proc_prontos->prio){
      self->ini_fila_proc_prontos = lst_retira(self->ini_fila_proc_prontos, self->processo_corrente->id);
      self->ini_fila_proc_prontos = so_coloca_fila_pronto(self, self->processo_corrente);
    }
  }
  if(self->processo_corrente != NULL)
    console_printf("(escalona proc_corr estado: %d)", self->processo_corrente->estado);
  else
    console_printf("(escalona proc nulo)");
  if(self->processo_corrente ==  NULL || self->processo_corrente->estado != pronto){
    processo_t* prox_processo = so_proximo_pronto(self);
    if(prox_processo != NULL)
      console_printf("(prox_proc_id %d)", prox_processo->id);
    else
      console_printf("(prox_proc eh nulo)");
    if(prox_processo != NULL && prox_processo->espera_terminal != 0){
      self->dispositivos_livres[prox_processo->id_terminal/4] = false;
      prox_processo->espera_terminal = 0;
    }
    
    self->processo_corrente = prox_processo; //pode ser NULL
    if(self->processo_corrente != NULL)
      console_printf("id_proc_corr escalonado %d", self->processo_corrente->id);
  }
}

static int so_despacha(so_t *self)
{
  // t2: se houver processo corrente, coloca o estado desse processo onde ele
  //   será recuperado pela CPU (em CPU_END_PC etc e 59) e retorna 0,
  //   senão retorna 1
  // o valor retornado será o valor de retorno de CHAMAC, e será colocado no 
  //   registrador A para o tratador de interrupção (ver trata_irq.asm).

  if(self->processo_corrente != NULL){
    if(mem_escreve(self->mem, CPU_END_A, self->processo_corrente->A) != ERR_OK
      || mem_escreve(self->mem, CPU_END_PC, self->processo_corrente->PC) != ERR_OK
      || mem_escreve(self->mem, CPU_END_erro, self->processo_corrente->regErro) != ERR_OK
      || mem_escreve(self->mem, 59, self->processo_corrente->X) != ERR_OK) {
      console_printf("SO: erro na escrita dos registradores do processo %d.", self->processo_corrente->id);
      self->erro_interno = true;
      return 1;
    }
    mmu_define_tabpag(self->mmu, self->processo_corrente->tab_pag);
    return 0;
  }
  else{
    return 1;
  }
}


// ---------------------------------------------------------------------
// TRATAMENTO DE UMA IRQ {{{1
// ---------------------------------------------------------------------

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq(so_t *self, int irq)
{
  // verifica o tipo de interrupção que está acontecendo, e atende de acordo
  switch (irq) {
    case IRQ_RESET:
      so_trata_reset(self);
      break;
    case IRQ_SISTEMA:
      so_trata_irq_chamada_sistema(self);
      break;
    case IRQ_ERR_CPU:
      so_trata_irq_err_cpu(self);
      break;
    case IRQ_RELOGIO:
      so_trata_irq_relogio(self);
      break;
    default:
      so_trata_irq_desconhecida(self, irq);
  }
}

processo_t* so_cria_entrada_processo(so_t* self, int PC, int tam);

// chamada uma única vez, quando a CPU inicializa
static void so_trata_reset(so_t *self)
{
  // coloca o tratador de interrupção na memória
  // quando a CPU aceita uma interrupção, passa para modo supervisor,
  //   salva seu estado à partir do endereço CPU_END_PC, e desvia para o
  //   endereço CPU_END_TRATADOR
  // colocamos no endereço CPU_END_TRATADOR o programa de tratamento
  //   de interrupção (escrito em asm). esse programa deve conter a
  //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
  //   foi definido na inicialização do SO)
  int ender = so_carrega_programa(self, NULL, "trata_int.maq");
  if (ender != CPU_END_TRATADOR) {
    console_printf("SO: problema na carga do programa de tratamento de interrupção");
    self->erro_interno = true;
  }

  // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
  if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK) {
    console_printf("SO: problema na programação do timer");
    self->erro_interno = true;
  }

  // define o primeiro quadro livre de memória como o seguinte àquele que
  //   contém o endereço final da memória protegida (que não podem ser usadas
  //   por programas de usuário)
  // t3: o controle de memória livre deve ser mais aprimorado que isso  
  self->quadro_livre = CPU_END_FIM_PROT / TAM_PAGINA + 1;

  // t2: deveria criar um processo para o init, e inicializar o estado do
  //   processador para esse processo com os registradores zerados, exceto
  //   o PC e o modo.
  // como não tem suporte a processos, está carregando os valores dos
  //   registradores diretamente no estado da CPU mantido pelo SO; daí vai
  //   copiar para o início da memória pelo despachante, de onde a CPU vai
  //   carregar para os seus registradores quando executar a instrução RETI
  //   em bios.asm (que é onde está a instrução CHAMAC que causou a execução
  //   deste código

  // coloca o programa init na memória
  processo_t *init =  so_cria_entrada_processo(self, 0, 0); // deveria inicializar um processo...
  ender = so_carrega_programa(self, init, "init.maq");
  if (ender == -1) {
    console_printf("SO: problema na carga do programa inicial");
    self->erro_interno = true;
    return;
  }
  // altera o PC para o endereço de carga
  init->PC = ender;
  init->erro = ERR_OK;
  init->regErro = 0; 

  //atualiza processo corrente e coloca init na fila de processos
  if (init != NULL) {
      self->processo_corrente = init;
      init->estado = pronto;
      self->ini_fila_proc_prontos = so_coloca_fila_pronto(self, init);
      self->ini_fila_proc = lst_insere_ordenado(self->ini_fila_proc, init->id, init->prio);
  }

  console_printf("(init id_terminal %d)", self->processo_corrente->id_terminal);  
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{
  // Ocorreu um erro interno na CPU
  // O erro está codificado em CPU_END_erro
  // Em geral, causa a morte do processo que causou o erro
  // Ainda não temos processos, causa a parada da CPU
  // t2: com suporte a processos, deveria pegar o valor do registrador erro
  //   no descritor do processo corrente, e reagir de acordo com esse erro
  //   (em geral, matando o processo)
  mem_le(self->mem, CPU_END_erro, &self->processo_corrente->regErro);
  err_t err = self->processo_corrente->regErro;
  
  console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
  self->erro_interno = true;
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self)
{
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0); // desliga o sinalizador de interrupção
  e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
  if (e1 != ERR_OK || e2 != ERR_OK) {
    console_printf("SO: problema da reinicialização do timer");
    self->erro_interno = true;
  }
  // t2: deveria tratar a interrupção
  //   por exemplo, decrementa o quantum do processo corrente, quando se tem
  //   um escalonador com quantum
  if(self->escalonador != simples && self->processo_corrente != NULL)
    self->processo_corrente->quantum--; 
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  self->erro_interno = true;
}


// ---------------------------------------------------------------------
// CHAMADAS DE SISTEMA {{{1
// ---------------------------------------------------------------------

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static void so_trata_irq_chamada_sistema(so_t *self)
{
  // a identificação da chamada está no registrador A
  // t2: com processos, o reg A deve estar no descritor do processo corrente
  int id_chamada = self->processo_corrente->A;
  console_printf("SO: chamada de sistema %d", id_chamada);
  switch (id_chamada) {
    case SO_LE:
      so_chamada_le(self);
      break;
    case SO_ESCR:
      so_chamada_escr(self);
      break;
    case SO_CRIA_PROC:
      so_chamada_cria_proc(self);
      break;
    case SO_MATA_PROC:
      so_chamada_mata_proc(self);
      break;
    case SO_ESPERA_PROC:
      so_chamada_espera_proc(self);
      break;
    default:
      console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);
      // t2: deveria matar o processo
      so_chamada_mata_proc(self);
  }
}

static void so_muda_estado_processo(so_t* self, int id_proc, estado_proc est);

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
static void so_chamada_le(so_t *self)
{
  // implementação com espera ocupada
  //   t2: deveria realizar a leitura somente se a entrada estiver disponível,
  //     senão, deveria bloquear o processo.
  //   no caso de bloqueio do processo, a leitura (e desbloqueio) deverá
  //     ser feita mais tarde, em tratamentos pendentes em outra interrupção,
  //     ou diretamente em uma interrupção específica do dispositivo, se for
  //     o caso
  // implementação lendo direto do terminal A
  //   t2: deveria usar dispositivo de entrada corrente do processo
  int estado;
  if ((es_le(self->es, self->processo_corrente->id_terminal + TERM_TECLADO_OK, &estado)) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado do teclado");
    return;
  }
  if (estado == 0){
    self->processo_corrente->espera_terminal = 1;
    so_muda_estado_processo(self, self->processo_corrente->id, bloqueado);
    return;
  }

  int dado;
  if ((es_le(self->es, self->processo_corrente->id_terminal + TERM_TECLADO, &dado)) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    return;
  }
  // escreve no reg A do processador
  // (na verdade, na posição onde o processador vai pegar o A quando retornar da int)
  // t2: se houvesse processo, deveria escrever no reg A do processo
  // t2: o acesso só deve ser feito nesse momento se for possível; se não, o processo
  //   é bloqueado, e o acesso só deve ser feito mais tarde (e o processo desbloqueado)
  self->processo_corrente->A = dado;
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{
  // implementação com espera ocupada
  //   t2: deveria bloquear o processo se dispositivo ocupado
  // implementação escrevendo direto do terminal A
  //   t2: deveria usar o dispositivo de saída corrente do processo
  int estado;
  if ((es_le(self->es, self->processo_corrente->id_terminal + TERM_TELA_OK, &estado)) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado da tela");
    return;
  }
  if (estado == 0){
    console_printf("espera terminal = 2 estado = %d", estado);
    self->processo_corrente->espera_terminal = 2;
    so_muda_estado_processo(self, self->processo_corrente->id, bloqueado);
    return;
  } 
  // está lendo o valor de X e escrevendo o de A direto onde o processador colocou/vai pegar
  // t2: deveria usar os registradores do processo que está realizando a E/S
  // t2: caso o processo tenha sido bloqueado, esse acesso deve ser realizado em outra execução
  //   do SO, quando ele verificar que esse acesso já pode ser feito.
  int dado = self->processo_corrente->X;
  if ((es_escreve(self->es,  self->processo_corrente->id_terminal + TERM_TELA, dado)) != ERR_OK) {
    console_printf("SO: problema no acesso à tela");
    return;
  }
  self->processo_corrente->A = 0;
}

static void altera_registrador_A(so_t *self, processo_t *processo){
  self->processo_corrente->A = processo->A;
  int ind = encontra_indice_processo(self->processos, processo->id);
  if(ind != -1) 
    self->processos->A = processo->A;
}

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self)
{
  // ainda sem suporte a processos, carrega programa e passa a executar ele
  // quem chamou o sistema não vai mais ser executado, coitado!
  // t2: deveria criar um novo processo
  // t3: identifica direito esses processos
  processo_t *processo_criador = self->processo_corrente;
  processo_t *processo_criado = so_cria_entrada_processo(self, 0, 0); /*inicializa com valores "incorretos" para PC e tam*/

  // em X está o endereço onde está o nome do arquivo
  int ender_proc;
  // t2: deveria ler o X do descritor do processo criador
  ender_proc = self->processo_corrente->X;
  char nome[100];
  if (so_copia_str_do_processo(self, 100, nome, ender_proc, processo_criador)) {
    int ender_carga = so_carrega_programa(self, processo_criado, nome);
    if (ender_carga != -1) {
      // t2: deveria escrever no PC do descritor do processo criado
      processo_criado->PC = ender_carga;
      processo_criado->erro = ERR_OK;
      processo_criado->regErro = 0;
      //} 
      self->ini_fila_proc_prontos = so_coloca_fila_pronto(self, processo_criado);
      console_printf("(id_proc: %d, ini_proc %d)", processo_criado->id, self->ini_fila_proc_prontos->id);
      self->ini_fila_proc = lst_insere_ordenado(self->ini_fila_proc, processo_criado->id, processo_criado->prio);
    } else{
      processo_criado->erro = ERR_OP_INV;
      processo_criado->regErro = 1;
    }
  }
  // deveria escrever -1 (se erro) ou o PID do processo criado (se OK) no reg A
  //   do processo que pediu a criação 
  if(processo_criado->regErro == 1){
    processo_criador->A = -1;
  } else{
    processo_criador->A = processo_criado->id;
  }
  altera_registrador_A(self, processo_criador);
}

static void so_libera_espera_proc(so_t *self, int id_proc_morrendo);

// implementação da chamada se sistema SO_MATA_PROC
// mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t *self)
{
  // t2: deveria matar um processo
  // ainda sem suporte a processos, retorna erro -1
  int id_proc_a_matar = self->processo_corrente->id;
  console_printf("(id proc_a_matar %d)", id_proc_a_matar);

  int indice = encontra_indice_processo(self->processos, id_proc_a_matar);
  /*nao encontrou processo com esse id*/
  if(indice == -1){
    console_printf("SO: processo de id %d nao encontrado para SO_MATA_PROC", id_proc_a_matar);
    self->regA = -1;
    self->processo_corrente->A = -1;
  }

  so_muda_estado_processo(self, id_proc_a_matar, morto);

  if(self->processo_corrente != NULL){
    self->dispositivos_livres[self->processo_corrente->id_terminal/4] = true;    //libera
    self->processo_corrente = NULL;
  }

  so_libera_espera_proc(self, id_proc_a_matar);

  self->regA = 0; //tudo ok
}

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
  // t2: deveria bloquear o processo se for o caso (e desbloquear na morte do esperado)
  // ainda sem suporte a processos, retorna erro -1
  console_printf("SO: SO_ESPERA_PROC não implementada");
  self->regA = -1;
}


// ---------------------------------------------------------------------
// CARGA DE PROGRAMA {{{1
// ---------------------------------------------------------------------

// funções auxiliares
static int so_carrega_programa_na_memoria_fisica(so_t *self, programa_t *programa);
static int so_carrega_programa_na_memoria_virtual(so_t *self, programa_t *programa, processo_t *processo);

// carrega o programa na memória
// se processo for NENHUM_PROCESSO, carrega o programa na memória física
//   senão, carrega na memória virtual do processo
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, processo_t *processo, char *nome_do_executavel)
{
  console_printf("SO: carga de '%s'", nome_do_executavel);

  programa_t *programa = prog_cria(nome_do_executavel);
  if (programa == NULL) {
    console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_carga;
  if (processo == NULL) {
    end_carga = so_carrega_programa_na_memoria_fisica(self, programa);
  } else {
    end_carga = so_carrega_programa_na_memoria_virtual(self, programa, processo);
  }
  /*coloca os valores corretos do processo*/
  processo->PC = prog_end_carga(programa);
  processo->memTam = prog_tamanho(programa);

  if(end_carga == -1){
    console_printf("Erro ao carregar programa, endereco -1");
  }

  prog_destroi(programa);
  return end_carga;
}

static int so_carrega_programa_na_memoria_fisica(so_t *self, programa_t *programa)
{
  int end_ini = prog_end_carga(programa);
  int end_fim = end_ini + prog_tamanho(programa);

  for (int end = end_ini; end < end_fim; end++) {
    if (mem_escreve(self->mem, end, prog_dado(programa, end)) != ERR_OK) {
      console_printf("Erro na carga da memória, endereco %d\n", end);
      return -1;
    }
  }

  console_printf("SO: carga na memória física %d-%d", end_ini, end_fim);
  return end_ini;
}

static int so_carrega_programa_na_memoria_virtual(so_t *self, programa_t *programa, processo_t *processo)
{
  // t3: isto tá furado...
  // está simplesmente lendo para o próximo quadro que nunca foi ocupado,
  //   nem testa se tem memória disponível
  // com memória virtual, a forma mais simples de implementar a carga de um
  //   programa é carregá-lo para a memória secundária, e mapear todas as páginas
  //   da tabela de páginas do processo como inválidas. Assim, as páginas serão
  //   colocadas na memória principal por demanda. Para simplificar ainda mais, a
  //   memória secundária pode ser alocada da forma como a principal está sendo
  //   alocada aqui (sem reuso)
  int end_virt_ini = prog_end_carga(programa);
  // o código abaixo só funciona se o programa iniciar no início de uma página
  if ((end_virt_ini % TAM_PAGINA) != 0) return -1;
  int end_virt_fim = end_virt_ini + prog_tamanho(programa) - 1;
  int pagina_ini = end_virt_ini / TAM_PAGINA;
  int pagina_fim = end_virt_fim / TAM_PAGINA;
  int n_paginas = pagina_fim - pagina_ini + 1;
  int quadro_ini = self->quadro_livre;
  int quadro_fim = quadro_ini + n_paginas - 1;
  // mapeia as páginas nos quadros
  for (int i = 0; i < n_paginas; i++) {
    tabpag_define_quadro(self->tabpag_global, pagina_ini + i, quadro_ini + i);
  }
  self->quadro_livre = quadro_fim + 1;

  // carrega o programa na memória principal
  int end_fis_ini = quadro_ini * TAM_PAGINA;
  int end_fis = end_fis_ini;
  for (int end_virt = end_virt_ini; end_virt <= end_virt_fim; end_virt++) {
    if (mem_escreve(self->mem, end_fis, prog_dado(programa, end_virt)) != ERR_OK) {
      console_printf("Erro na carga da memória, end virt %d fís %d\n", end_virt,
                     end_fis);
      return -1;
    }
    end_fis++;
  }
  console_printf("SO: carga na memória virtual V%d-%d F%d-%d npag=%d",
                 end_virt_ini, end_virt_fim, end_fis_ini, end_fis - 1, n_paginas);
  return end_virt_ini;
}


// ---------------------------------------------------------------------
// ACESSO À MEMÓRIA DOS PROCESSOS {{{1
// ---------------------------------------------------------------------

// copia uma string da memória do processo para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// O endereço é um endereço virtual de um processo.
// t3: Com memória virtual, cada valor do espaço de endereçamento do processo
//   pode estar em memória principal ou secundária (e tem que achar onde)
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam],
                                     int end_virt, processo_t *processo)
{
  if (processo == NULL) return false;
  for (int indice_str = 0; indice_str < tam; indice_str++) {
    int caractere;
    // não tem memória virtual implementada, posso usar a mmu para traduzir
    //   os endereços e acessar a memória, porque todo o conteúdo do processo
    //   está na memória principal, e só temos uma tabela de páginas
    if (mmu_le(self->mmu, end_virt + indice_str, &caractere, usuario) != ERR_OK) {
      return false;
    }
    if (caractere < 0 || caractere > 255) {
      return false;
    }
    str[indice_str] = caractere;
    if (caractere == 0) {
      return true;
    }
  }
  // estourou o tamanho de str
  return false;
}

// vim: foldmethod=marker


float so_calcula_prioridade(processo_t* processo){
  int t_exec = QUANTUM_INICIAL - processo->quantum;
  float prioridade = (processo->prio + t_exec/QUANTUM_INICIAL) / 2;
  return prioridade;
}

Lista_processos* so_coloca_fila_pronto(so_t* self, processo_t* processo){
  if(lst_busca(self->ini_fila_proc_prontos, processo->id) == NULL){   /*adiciona na lista de prontos se ja nao estiver na lista*/
    float prio = 0.5; //simples e round-robin
    console_printf("(escalonador atual = %d)", self->escalonador);
    switch (self->escalonador){
    case simples:
      self->ini_fila_proc_prontos = lst_adicionar_final(self->ini_fila_proc_prontos, processo->id, processo->prio);
      break;
    case round_robin:
      self->ini_fila_proc_prontos = lst_adicionar_final(self->ini_fila_proc_prontos, processo->id, processo->prio);
      break;
    case prioridade:
      prio = so_calcula_prioridade(processo);
      self->ini_fila_proc_prontos = lst_insere_ordenado(self->ini_fila_proc_prontos, processo->id, prio);
    default:
      console_printf("SO: escalonador nao encontrado");
      break;
    }  
  }
  return self->ini_fila_proc_prontos;
}

int so_busca_entrada_tabela(so_t* self){
  for(int i = 0; i < MAX_PROCESSOS; i++){
    if(self->processos[i].estado == morto){
      return i;
    }
  }
  return -1; //tabela cheia
}

processo_t* so_cria_entrada_processo(so_t* self, int PC, int tam) {
    int i = so_busca_entrada_tabela(self);
    if (i == -1) {
        console_printf("SO: tabela de processos cheia");
        return NULL;
    }
    int id = self->cont_processos++;
    inicializa_processo(&self->processos[i], id, PC, tam);
    self->processos[i].estado = pronto;
    if(self->processos[i].id == 0)
      console_printf("id eh 0");
    else
      console_printf("id diferente de zero");
    return &self->processos[i];
}

processo_t* so_proximo_pendente(so_t* self, int quant_bloq){
  Lista_processos* l = self->ini_fila_proc;
  int proc_bloq = 0; 
  while(l != NULL){
    if(l->estado == bloqueado){
      proc_bloq++;
      console_printf("quant %d proc_bloq %d", quant_bloq, proc_bloq);
      if(quant_bloq < proc_bloq){
        int indice = encontra_indice_processo(self->processos, l->id);
        return &self->processos[indice];
      }
    }
    l = l->prox;
  }
  console_printf("nao ha proc pendente");
  return NULL; //nao ha processos pendentes/bloqueados
}

processo_t* so_proximo_pronto(so_t* self){
  Lista_processos* l = self->ini_fila_proc_prontos;
  if(l != NULL){
    int indice = encontra_indice_processo(self->processos, l->id);
    if(indice != -1)
      return &self->processos[indice];
  }
  return NULL; //nao ha processos prontos
}

static void so_muda_estado_processo(so_t* self, int id_proc, estado_proc est){
  altera_estado_proc_tabela(self->processos, id_proc, est);
  self->ini_fila_proc = lst_altera_estado(self->ini_fila_proc, id_proc, est);

  if(est == pronto){
    self->ini_fila_proc_prontos = so_coloca_fila_pronto(self, self->processo_corrente);
  }
  else{ /*bloqueado ou morto*/
    self->ini_fila_proc_prontos = lst_retira(self->ini_fila_proc_prontos, id_proc); 
    if(est == morto){
      self->ini_fila_proc = lst_retira(self->ini_fila_proc, id_proc);
    }
  }
}

static void so_libera_espera_proc(so_t *self, int id_proc_morrendo){
  for(int i = 0; i < MAX_PROCESSOS; i++){ 
    if(self->processos[i].X == id_proc_morrendo){
      so_muda_estado_processo(self, self->processos[i].id, pronto);
    }
  }
}

