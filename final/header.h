#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>		
#include <sys/wait.h>		
#include <sys/ipc.h>		
#include <sys/shm.h>		
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <mqueue.h>
#include <sys/msg.h>		
#include <signal.h>
#include <limits.h>
#include <time.h>

#define TAMANHO 100		//tamanho do array de leitura dos comandos do pipe
#define PIPE_NAME "pipe_voos"	//nome do pipe
#define TORRE 2			//para mensagens da torre
#define VOO 1			//para mensagens de voos
#define EMERGENCIA 3		//para mensagens de emergencia
#define LEN 20			//para guarda nomes em arrays

//para guardar o pid do gestor e da torre por causa da terminaçao
pid_t gestor_pid, torre_pid, tempo_pid, gere_voo_pid, gere_emergencias_pid;
//variaveis para fazer parsing de comandos
int contador,len;
char values[15], *separator;
//socket do pipe
int fd_pipe;
//vaiaveis para a configuracao, 
int time_units, take_off_time, take_off_break_time, landing_time, landing_break_time, min_hold, max_hold, total_departures, total_arrivals;
//contador de total de voos que enviam pedidos, total de voo que sao criados pelo gestor, total de voos que fazem hold
int total_voos=0,total_voos_G=0;
//variaveis para criar shm (para os voos e a estatisticas separadamente) e msq
int shmid, shmid_stats, id_msq; 
//variaveis para gerir os slots da shm
int *slots_livres,gera_slot=0;
//variaveis para as estatisticas
int conta_voos_HOLD=0,total_hold=0,conta_D=0,conta_A=0,conta_tempo_D=0,conta_tempo_A=0,conta_RD=0,conta_RJ=0;

//shared memory struct for statistical information
typedef struct {
	pthread_mutex_t mutex_stats;
	pthread_mutexattr_t mattr_stats;			
	int total_flights;
	int total_ARRIVALS;
	int total_DEPARTURES;
	int total_redirected_flights;
	int total_rejected_flights;
	double average_time_ARRIVALS;
	double average_time_DEPARTURES;
	double average_num_HOLD;
	double average_num_HOLD_urg;
}STATS;

//estruturas para as informações dos comandos que vem do pipe, que servirão para criar os voos, estrutura partilhada pelo gestor e torre
typedef struct voos{
	char tipo[LEN];//para imprimir o ttipo de voo
	char nome[LEN];//nome do voo
	int init;//tempo de criação do voo
	int take_off;//tempo de descolagem			
	int ETA;//valor do ttempo de chagada à pista	
	int combustivel;//valor do cumbustivel do voo de chegada
	struct voos * prox_voo;//ponteiro para o próximo nó da lista ligada de voos
}VOOS;

//estrutura da lista ligada da torre
typedef struct torre{
	int slot;//guarda o slot de cada voo
	char nome[LEN];//para guardar o nome do voo
	int take_off;//para passar a informação de take_off para a torre
	int ETA;//informação do voo de aterragem
	int combustivel;//informação de voo de aterragem
	int emergencia;//para que o escalonamento saiba quando ha um voo de emergencia
	struct torre * next;//ponteiro para o proximo no
}TORRE_struct;

//estrutura da memoria partilhada
typedef struct shm{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	pthread_mutex_t mutex_notifica;
	pthread_cond_t cond_notifica;
	int tempo;//variavel tempo
	char manobra[LEN];//resposta da torre, corresponde à manobra qua o voo tem de fazer
	char pista[LEN];//pista a que a torre dá acesso
}SHARED_MEM;

//estrutura das filas
typedef struct arrivals{
	char nome[LEN];//para depois de notificar o voo se eliminar o no da lista da torre
	int shm_pos_A;//posiçao do voo na shm
	struct torre *eta;//ponteiro para a estrutura do no do voo para aceder ao valor do ETA para manter a fila ordenada pelos ETA
	struct arrivals *next;//ponteiro para o proximo no da fila de arrivals
}ARRIVAL_queue; 

typedef struct departures{
	char nome[LEN];//para depois de notificar o voo se eliminar o no da lista da torre
	int shm_pos_D;//posiçao do voo na shm
	int take_off;//recebe o valor do take_off que esta nos nós dos departures da lista da torre
	struct departures *next;//ponteiro para o proximo no da fila de arrivals
}DEPARTURES_queue;	

//msq para os requests dos voos
typedef struct {
	long msgtype;//tipo da mensagem
	int slot;//slot de memoria partilhada atribuido ao voo que envia pedido
	char nome[LEN];//nome do voo que envia a mensagem
	int take_off;//para passar a informação de take_off para a torre
	int ETA;//informação do voo de aterragem
	int combustivel;//informação de voo de aterragem
}MSQ;
