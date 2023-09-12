//Eurico José 2016225648
//Nuno Duarte 2018295734
#include "header.h"

SHARED_MEM *ptr_shm;//ponteiro da estrutura da shm para apontar para posiçao, e outro para o tempo
STATS *ptr_stats;//ponteiro para a estrutura das estatisticas
VOOS *ptr_voos;//ponteiro para a estrutura de voos
TORRE_struct *ptr_torre;//ponteiro para a estutura da lista ligada da torre
MSQ mensagem;//variavel para referenciar a estrutura da MSQ
DEPARTURES_queue *header_D;//cabeçalho da fila de departures
ARRIVAL_queue *header_A;//cabeçalho da fila de arrivals

sem_t * mutex_log;//semaforo POSIX named para controlar as escritas no log
sem_t * mutex_array_slots;//semaforo POSIX named controla escritas da torre e thread_update_filas no array de slots livres
sem_t * mutex_torre;//mutex POSIX named para controlar alteraçoes na lista da torre

pthread_t thread_tempo;//thread que gere o tempo
pthread_t *thread_voo;//gere_voos
pthread_t thread_emergencias;//thread criada pela torre que gere os pedidos de aterragem de emergencia
pthread_t thread_update_filas;//thread criada pela torre que atualiza os dados da lista da torre, poe os voos nas filas e notific-os

void escreve_log(char * mensagem_log){
	
	sem_wait(mutex_log);//fecha o semáforo para escrita no log
	//recursos para por a hora no log e no ecra
	time_t tempo;
	char hora[TAMANHO];
	struct tm* tm_info;

	FILE * log = fopen("log.txt","a");
	if (log == NULL) {
		perror("fopen(): Failed to open file");
		exit(0);
	}
	//mensagem a imprimir no log e ecra
	time(&tempo);
	tm_info = localtime(&tempo);
	strftime(hora,TAMANHO,"%H:%M:%S",tm_info);
	printf("%s %s",hora,mensagem_log);
	fprintf(log,"%s %s",hora,mensagem_log);
	fclose(log);
	sem_post(mutex_log);//abre o semáforo para que outro processo possa escrever
}

void read_config(){
	int aux1,aux2;
	FILE * configs = fopen("config.txt","r");
	if (configs == NULL) {
		escreve_log("\nfopen(): Failed to open file\n");
		exit(0);
	}
	contador = len = 0;	
	while(fgets(values,15,configs)!=NULL){
		if(contador == 0)
			time_units = atoi(values);
		else if(contador == 4)
			total_departures = atoi(values); 
		else if(contador == 5)
			total_arrivals = atoi(values);
		else{
			separator = strtok(values,", ");
			len += strlen(separator);
			aux1 = atoi(separator);
			separator = strtok(values, ", ");
			separator += len+1;
			aux2 = atoi(separator);
			if(contador == 1){
				take_off_time = aux1;
				take_off_break_time = aux2;
			
			}
			else if(contador == 2){
				landing_time = aux1;
				landing_break_time = aux2;
			}
			else if(contador == 3){
				min_hold = aux1;
				max_hold = aux2;
			}
		}
		contador ++;
		len = 0;
	}
	fclose(configs);
}

void inicializa(){
	
	read_config();//guarda configuraçoes
	
	//aloca memoria para o vetor de threads que corresponde aos voos
	thread_voo = (pthread_t*) malloc (sizeof(pthread_t)*(total_arrivals+total_departures));
	
	//aloca memoria para o vetor de slots livres
	slots_livres = (int*) malloc (sizeof(int)*(total_arrivals+total_departures));
	for(int i=0;i< total_departures+total_arrivals;i++) slots_livres[i] = -1;//inicializa os valores a -1
	
	//criar e mapear a memoria partilhada alocando toda a memoria necessaria para o numero meximo de voos 0666 tudo le e escreve
	if((shmid = shmget(IPC_PRIVATE, (sizeof(SHARED_MEM)*(total_departures+total_arrivals)), IPC_CREAT|0666)) == -1) { 
    		escreve_log("\nErro na criação da memória partilhada\n");
    		exit(-1);
   	}
    	if((ptr_shm = (SHARED_MEM *) shmat(shmid, NULL, 0)) == (SHARED_MEM *)-1) {
    		escreve_log("\nErro ao mapear da memória partilhada\n");
    		exit(-1);
   	}
   	
   	//aloca espaço para as estatiticas
	if((shmid_stats = shmget(IPC_PRIVATE, (sizeof(STATS)*(total_departures+total_arrivals)), IPC_CREAT|0666)) == -1) { 
    		escreve_log("\nErro na criação da memória partilhada\n");
    		exit(-1);
   	}
    	if((ptr_stats = (STATS *) shmat(shmid, NULL, 0)) == (STATS *)-1) {
    		escreve_log("\nErro ao mapear da memória partilhada\n");
    		exit(-1);
   	}
   	
   	//inicializa o mutex, a variavel de condicao e os atributos
	for(int i=0; i<total_arrivals+total_departures; i++){
		pthread_mutexattr_setpshared(&ptr_shm[i].mattr, PTHREAD_PROCESS_SHARED);
		pthread_condattr_setpshared(&ptr_shm[i].cattr, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&ptr_shm[i].mutex_notifica, &ptr_shm[i].mattr);
		pthread_cond_init(&ptr_shm[i].cond_notifica, &ptr_shm[i].cattr);
	}
	
	//cria cabeçalhos da lista de voos do gestor, da torre e das filas de espera
	ptr_voos = (VOOS*) malloc (sizeof(VOOS));//alloca espaço para o cabeçalho
	if(ptr_voos!=NULL) ptr_voos->prox_voo = NULL;
	ptr_torre = (TORRE_struct*) malloc (sizeof(TORRE_struct));
	if(ptr_torre!=NULL) ptr_torre->next = NULL;
	header_D = (DEPARTURES_queue*) malloc (sizeof(DEPARTURES_queue));
	if(header_D!=NULL) header_D->next = NULL;
	header_A = (ARRIVAL_queue*) malloc (sizeof(ARRIVAL_queue));
	if(header_A!=NULL) header_A->next = NULL;

	//cria a messagem queue
	if((id_msq = msgget(IPC_PRIVATE, IPC_CREAT|0777)) < 0){//0777 todos podem ler escrever e executar
      		escreve_log("Erro na criação da message queue");
      		exit(0);
   	}

	//inicializa semaforo mutex para escrever no log
	sem_unlink("MUTEX_LOG");
	mutex_log = sem_open("MUTEX_LOG",O_CREAT|O_EXCL,0700,1);
	
	//inicializa semaforo mutex para escritas no array de slots livres
	sem_unlink("MUTEX_SLOTS_LIVRES");
	mutex_array_slots = sem_open("MUTEX_SLOTS_LIVRES",O_CREAT|O_EXCL,0700,1);
	
	//inicializa semaforo mutex para inserir um nó na lista da torre
	sem_unlink("MUTEX_TORRE");
	mutex_torre = sem_open("MUTEX_TORRE",O_CREAT|O_EXCL,0700,1);

}

void terminate(){

	//gestor e torre terminam os recurso criados
	//fecha o pipe para nao aceitar mais comandos
	unlink(PIPE_NAME);//garante que este pipe nao fica no sitema	
	close(fd_pipe);
	
	//liberta os cabeçalhos
	free(ptr_voos);
	free(ptr_torre);
	free(header_D);
	free(header_A);

	//termina a msq
	msgctl(id_msq, IPC_RMID, NULL);

	//termina o semaforo para o ficheiro log
	sem_close(mutex_log);
	sem_unlink("MUTEX_LOG");
	
	//termina o semaforo mutex para escritas no array de slots livres
	sem_close(mutex_array_slots);
	sem_unlink("MUTEX_SLOTS_LIVRES");
	
	//termina o semaforo mutex para inserir nos na lista da torre
	sem_close(mutex_torre);
	sem_unlink("MUTEX_TORRE");
	
	//destroy mutex e variavels de condiçao da shm
	for(int i=0; i<total_arrivals+total_departures; i++){
		pthread_cond_destroy(&ptr_shm[i].cond_notifica);//destroy a variavel
		pthread_mutex_destroy(&ptr_shm[i].mutex_notifica);//destroy o mutex
	}
	
	//termina e desmapeia a memoria partilhada
	shmdt(&ptr_shm);
	shmctl(shmid, IPC_RMID, NULL);
	
	//termina e desmapeia a memoria partilhada das estatisticas
	shmdt(&ptr_stats);
	shmctl(shmid_stats, IPC_RMID, NULL);
}

void statistics(){
	
	char str[TAMANHO],aux[TAMANHO];
	ptr_stats->total_rejected_flights = conta_RJ;
	ptr_stats->total_redirected_flights = conta_RD;
	ptr_stats->total_DEPARTURES = conta_D;
	ptr_stats->total_ARRIVALS = conta_A;
	ptr_stats->total_flights = conta_D + conta_A;
	ptr_stats->average_time_ARRIVALS = ((double) conta_tempo_A/conta_A);
	ptr_stats->average_time_DEPARTURES = ((double) conta_tempo_D/conta_D);
	ptr_stats->average_time_ARRIVALS = ((double) conta_tempo_D/conta_D);
	if(conta_voos_HOLD == 0)
		ptr_stats->average_num_HOLD = 0;
	else
		ptr_stats->average_num_HOLD = ((double) total_hold/conta_voos_HOLD);
	ptr_stats->average_num_HOLD_urg = 0.0;
	strcpy(str,"---------------------------ESTATISTICAS----------------------------");
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Número total de voos criados: ");
	sprintf(aux, "%d", ptr_stats->total_flights);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Número total de voos que aterraram: ");
	sprintf(aux, "%d", ptr_stats-> total_ARRIVALS);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Número total de voos que descolaram: ");
	sprintf(aux, "%d", ptr_stats->total_DEPARTURES);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Número de voos redirecionados para outro aeroporto: ");
	sprintf(aux, "%d", ptr_stats->total_redirected_flights);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Número de voos rejeitados pela Torre de controlo: ");
	sprintf(aux, "%d", ptr_stats->total_rejected_flights);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Tempo médio de espera para aterrar: ");
	sprintf(aux, "%.3f", ptr_stats->average_time_ARRIVALS);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Tempo médio de espera para descolar: ");
	sprintf(aux, "%.3f", ptr_stats->average_time_DEPARTURES);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Número médio de  manobras de holding por voo de aterragem: ");
	sprintf(aux, "%.3f", ptr_stats->average_num_HOLD);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"Número médio de manobras de holding por voo em estado de urgência: ");
	sprintf(aux, "%.1f", ptr_stats->average_num_HOLD_urg);
	strcat(str,aux);
	strcat(str,"\n");
	escreve_log(str);
	strcpy(str,"----------------------------------------------------------------\n");
	escreve_log(str);
	escreve_log(": Fim de simulação\n");
}

void cleanup(int signum){

	if(gestor_pid == getpid()){//se tiver sido o gestor a receber o ^C
		for(int i=0; i<total_voos_G; i++){//espera que os voos terminem a sua execuçao
			pthread_join(thread_voo[i], NULL);
			wait(NULL);
		}
		kill(tempo_pid, SIGINT);//envia sinal para matar a thread do tempo para nao criar mais voos
		//kill(torre_pid, SIGUSR1);//envia sinal à torre
		wait(NULL);//espera por processos filho ou threads
	}
	else if(torre_pid == getpid()){//se tiver sido a torre a receber o SIGUSR1
		statistics();
		kill(gere_emergencias_pid, SIGKILL);//envia sinal para matar a thread gere_emergencias
		kill(gere_voo_pid, SIGKILL);//envia sinal para matar a thread gere_voos
		wait(NULL);
	}
	terminate();//termina os recursos todos
	exit(0);
}

//funcao para validar os comandos que vem do pipe
int valida_comando(char aux[]){
	
	int init = 0,eta = 0,departure = 0;//variaveis necessarias para verificar se os valores fazem sentido
	separator = strtok(aux," ");
	contador = 0;
	len = strlen(separator);
	while(contador <= 7){
		//se a primeira palavra esta mal escria
		if(strcmp(separator,"DEPARTURE")==0) departure=1;
		if(contador == 3) init=atoi(separator);//guarda o init para comparar com o takeoff
		if(contador == 5 && departure == 0) eta=atoi(separator)+landing_time;//guarda o ETA para comparar com o combustivel
		if(contador == 0 && strcmp(separator,"ARRIVAL") != 0 && strcmp(separator,"DEPARTURE") != 0){
			contador = -1;
			break;
		}
		if(strcmp(separator,"init:") != 0 && contador == 2){//se o init esta mal escrito
			contador = -1;
			break;
		}
		if(contador == 3 && atoi(separator) < ptr_shm[0].tempo){//se o init é menor que tempo
			contador = -1;
			break;
		}
		if(strcmp(separator,"eta:") != 0 && strcmp(separator,"takeoff:") != 0 && contador == 4){//se eta ou takeoff mal escrito
			contador = -1;
			break;
		}
 		if(contador == 5 && atoi(separator)<=init && departure == 1){//se takeoff <= init
			printf("\nTempo de take_off <= init\n");
			contador = -1;
			break;
		}
		if(strcmp(separator,"fuel:") != 0 && contador == 6 && departure == 0){//se o fuel esta mal escrito
			contador = -1;
			break;
		}
		if(contador == 7 && atoi(separator) <= eta+3){//se o combustivel < eta+landing_time+3 => aceita no minimo emergencias
			contador = -1;
			break;
		}
		separator = strtok(NULL, "\n ");
		if(contador == 7 && departure == 0) break;
		if(contador == 5 && departure == 1) break; 
		len = strlen(separator);
		contador ++;
	}
	return contador;
}

//funcao que remove um voo (nó) da lista do gestor, ou da lista da torre, ou das filas de espera
void remove_voo(char lista[],char nome[]){

	if(strcmp(lista,"gestor")==0){
		VOOS *aux, *ant;
		ant = ptr_voos;
		aux = ptr_voos->prox_voo;
		while(aux){//encontra o no com o ID_voo
			if (strcmp(aux->nome,nome)==0){
				ant->prox_voo = aux->prox_voo;
				free(aux);//liberta o nó
				break;
			}
			ant = ant->prox_voo;
			aux = aux->prox_voo;
		}
	}
	else if(strcmp(lista,"torre")==0){
		TORRE_struct *aux, *ant;
		ant = ptr_torre;
		aux = ptr_torre->next;
		while(aux){//encontra o no com o ID_voo
			if (strcmp(aux->nome,nome)==0){
				ant->next = aux->next;
				free(aux);//liberta o nó
				break;
			}
			ant = ant->next;
			aux = aux->next;
		}
	}
	else if(strcmp(lista,"fila_D")==0){
		DEPARTURES_queue *aux, *ant;
		ant = header_D;
		aux = header_D->next;
		while(aux){//encontra o no com o ID_voo
			if (strcmp(aux->nome,nome)==0){
				ant->next = aux->next;
				free(aux);//liberta o nó
				break;
			}
			ant = ant->next;
			aux = aux->next;
		}
	}
	else{
		ARRIVAL_queue *aux, *ant;
		ant = header_A;
		aux = header_A->next;
		while(aux){//encontra o no com o ID_voo
			if (strcmp(aux->nome,nome)==0){
				ant->next = aux->next;
				free(aux);//liberta o nó
				break;
			}
			ant = ant->next;
			aux = aux->next;
		}
	}
}

VOOS * ordena_voos(int max_init,VOOS *ant){
	VOOS * actual;
	ant = ptr_voos;
   	actual = ptr_voos->prox_voo;
   	while(actual){//percorre lista
		if(actual->init < max_init)//ordena pelos inits
        		ant = actual;
		actual = actual->prox_voo;
    	}
    	if ( actual!=NULL && actual->init != max_init) actual = NULL;//se nao houver elemento, é o último nó
	return ant;
}

void read_pipe(){

	VOOS *no,*ant;//definir ponteiros auxiliares para a estrutura da lista ligada
	int  Init,max_init=0,valida,ARRIVAL = 1;
	char comando[TAMANHO];//variaveis para fazer parsing dos comandos
	//cria o pipe
	unlink(PIPE_NAME);//para ter a certeza que nao ha lixo no pipe sempre que o gestor se liga novamente
	if((mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0600)<0) && (errno!= EEXIST)){
    		escreve_log("\nmkfifo(): Couldn't create named pipe\n");
    		exit(0);
    	}
	//abre o pipe para leitura
	if ((fd_pipe = open(PIPE_NAME, O_RDWR)) < 0) {
		escreve_log("\nopen(): Can't open pipe for reading\n");
		exit(0);
	}
	//le a informacao que esta no pipe e guardando nas respectivas estruturas de memoria
	while(1){
		read(fd_pipe, comando, sizeof(comando));
		//valida: recebe o valor de retorno da funçao que valida, ARRIVAL: para distinguir comandos,fd_pipe: para abrir o pipe
		char aux[strlen(comando)];
		strcpy(aux, comando);
		valida = valida_comando(aux);
		contador = len = 0;//para contar as palavaras de cada comando e as posiçoes nos vetores das estruturas
		if(valida == -1){
			bzero(aux,strlen(aux));//limpa o buffer
			strcat(aux,": Comando inválido - ");//copia info de log
			strcat(aux,comando);//copia o comando inválido
			escreve_log(aux);//executa a função log
		}
		else{
			bzero(aux,strlen(aux));//limpa o buffer
			strcat(aux,": Novo comando - ");//copia info de log
			strcat(aux,comando);//copia o comando inválido
			escreve_log(aux);//executa a função log
			no = (VOOS*) malloc (sizeof(VOOS));//aloca espaço para o novo no
			//começa a partiçao da frase
			separator = strtok(comando," ");
			len = strlen(separator);
			contador ++;
			//inicia a gravação de dados
			if(no!=NULL){
				strcpy(no->tipo,separator);
				while(contador < 8){
					separator = strtok(NULL, "\n ");
					if(contador == 1)
						strcpy(no->nome,separator);//guarda o nome no nó
					if(contador == 3){
						Init = atoi(separator);//copia o alor do init
						no->init = atoi(separator);//guarda o init
					}
					if(contador == 4 && strcmp(separator,"takeoff:") == 0)//se for uma DEPARTURE
						ARRIVAL = -1;
					if(contador == 5 && ARRIVAL == -1){//se for departure poe o próximo valor no take_off e sai
						no->take_off = atoi(separator);//guarda o take_off
						no->ETA = 0;
						no->combustivel = 0;
						break;
					}
					if(contador == 5 && ARRIVAL == 1){//se for ARRIVAL poe o próximo valor no ETA e continua
						no->ETA = atoi(separator);//guarda o ETA
						no->take_off = 0;
					}
					if(contador == 7)
						no->combustivel = atoi(separator);//guarda o combustivel
					separator += len;
					len = strlen(separator);
					contador ++;
				}
				if(max_init < Init) max_init = Init;//se o novo init for maior que o max_init, atualiza max_init
				ant = ordena_voos(max_init, ant);//função que ordena os voos da lista
				no->prox_voo = ant->prox_voo;        			
				ant->prox_voo = no;							
			}
			ARRIVAL = 1;//reset da variavel que teste se é uma arrival ou departure
		}
	}
}

//funcao que gere as açoes dos voos
void * voo_thread(void * aux){

	VOOS * ptr_no = ((struct voos *) aux);//cast de aux para ponteiro para a lista de voos, e attribui o nó apontado ao ptr_no
	char log[TAMANHO],str[TAMANHO],nome[LEN];//para escrever no log e guardar o nome do voo
	int take_off,ETA,combustivel,shm_slot,time;//para guardar a informação do voo e o seu slot na memoria partilhada, contar o hold
	strcpy(str,": ");
	strcat(str,ptr_no->nome);
	strcat(str," ");
	strcat(str,ptr_no->tipo);
	strcat(str," criado\n");
	escreve_log(str); 
	//guarda a info do voo para poder retirar o no da lista do getor
	take_off = ptr_no->take_off;
	ETA = ptr_no->ETA;
	combustivel = ptr_no->combustivel;
	strcpy(nome,ptr_no->nome);
	remove_voo("gestor",ptr_no->nome);//elimina o nó do voo da lista para um voo seja criado a partir do mesmo nó
	if(ptr_no->take_off != 0){//se for um voo de partida
		strcpy(mensagem.nome,nome);//envia o nome do voo que esta a fazer o pedido
		mensagem.take_off = take_off;//enviar o tempo de take_off
		mensagem.ETA = mensagem.combustivel = 0;//zera os outros campos porque 
		mensagem.msgtype = TORRE;//para enviar a mensagem à torre
		msgsnd(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), 0);//envia a mensagem a torre
		msgrcv(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), VOO, 0);//recebe resposta da torre
		shm_slot = mensagem.slot;//guarda o slot fornecido pela torre
		if(shm_slot == -1){//o voo foi rejeitado pela torre e tem de terminar
			strcpy(str,": ");
			strcat(str,nome);
			strcat(str," pedido negado\n");
			escreve_log(str);
		}
		else{
			pthread_mutex_lock(&ptr_shm[shm_slot].mutex_notifica);
			while(strcmp(ptr_shm[shm_slot].manobra,"TAKEOFF")!=0)//espera pela notificaçao da torre
				pthread_cond_wait(&ptr_shm[shm_slot].cond_notifica, &ptr_shm[shm_slot].mutex_notifica);
			pthread_mutex_unlock(&ptr_shm[shm_slot].mutex_notifica);//desbolqueia mutex
			strcpy(str,": ");
			strcat(str,nome);//escreve o nome do voo que descolou
			strcat(str," DESCOLAGEM ");//tipo de actividade
			strcat(str,ptr_shm[shm_slot].pista);//pista utilizada
			strcpy(log,str);
			strcat(log," iniciada\n");
			escreve_log(log);
			usleep(time_units*take_off_time*1000);//tempo de descolagem	
			strcpy(log,str);
			strcat(log," concluida\n");
			escreve_log(log);
			pthread_mutex_lock(&ptr_shm[shm_slot].mutex_notifica);
			strcpy(ptr_shm[shm_slot].manobra,"END");//para que a torre saiba que o voo terminou
			pthread_cond_signal(&ptr_shm[shm_slot].cond_notifica);//notifica a torre em mutua exclusao
			pthread_mutex_unlock(&ptr_shm[shm_slot].mutex_notifica);//desbolqueia mutex
		}
	}
	else if(ptr_no->take_off == 0){//se for uma arrival
		strcpy(mensagem.nome,nome);//envia o nome do voo que esta a fazer o pedido
		mensagem.take_off = 0;//zera o take_off
		mensagem.ETA = ETA;//envia o eta
		mensagem.combustivel = combustivel;//envia o combustivel
		if(ETA+landing_time+4 == combustivel){//em caso de emergencia envia pedido de emergencia
			//escreve logo no log
			strcpy(log,": ");
			strcat(log,nome);
			strcat(log," Aterragem de emergência pedida\n");//mensagem
			escreve_log(log);
			mensagem.msgtype = EMERGENCIA;//para enviar a mensagem à thread da torre que gere as mesnagens de emergencia
			msgsnd(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), 0);
			msgrcv(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), VOO, 0);
			shm_slot = mensagem.slot;//guarda o slot fornecido pela torre
			pthread_mutex_lock(&ptr_shm[shm_slot].mutex_notifica);
			while(strcmp(ptr_shm[shm_slot].manobra,"LAND")!=0)//espera notificaçao da torre
				pthread_cond_wait(&ptr_shm[shm_slot].cond_notifica, &ptr_shm[shm_slot].mutex_notifica);
			pthread_mutex_unlock(&ptr_shm[shm_slot].mutex_notifica);//desbolqueia mutex
			strcpy(str,": ");
			strcat(str,nome);//escreve o nome do voo que descolou
			strcat(str," ATERRAGEM DE EMERGENCIA ");//tipo de actividade
			strcat(str,ptr_shm[shm_slot].pista);//pista utilizada
			strcpy(log,str);
			strcat(log," iniciada\n");
			escreve_log(log);
			usleep(time_units*landing_time*1000);//tempo de aterragem
			strcpy(log,str);
			strcat(log," concluida\n");
			escreve_log(log);
			pthread_mutex_lock(&ptr_shm[shm_slot].mutex_notifica);
			strcpy(ptr_shm[shm_slot].manobra,"END");//para que a torre saiba que o voo terminou
			pthread_cond_signal(&ptr_shm[shm_slot].cond_notifica);//notifica a torre em mutua exclusao
			pthread_mutex_unlock(&ptr_shm[shm_slot].mutex_notifica);//desbolqueia mutex
		}
		else{//se nao for situaçao de emergencia
			mensagem.msgtype = TORRE;//destino da mensagem a torre e nao a thread que gere as emergencias
			msgsnd(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), 0);//envia a mensagem a torre
			msgrcv(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), VOO, 0);//recebe resposta
			shm_slot = mensagem.slot;//guarda o slot fornecido pela torre
			if(shm_slot == -1){//o voo foi rejeitado pela torre e tem de terminar
				strcpy(str,": ");
				strcat(str,nome);
				strcat(str," pedido negado\n");
				escreve_log(str);
			}
			else{
				struct timespec ts = {0};//para definir o time out
				ts.tv_sec = combustivel;//tempo em segundos ate ao timeout que é dado pelo valor do combustivel
				pthread_mutex_lock(&ptr_shm[shm_slot].mutex_notifica);
				while(strcmp(ptr_shm[shm_slot].manobra,"LAND")!=0){//espera notificaçao da torre
				time=pthread_cond_timedwait(&ptr_shm[shm_slot].cond_notifica,&ptr_shm[shm_slot].mutex_notifica,&ts);
				}
				pthread_mutex_unlock(&ptr_shm[shm_slot].mutex_notifica);//desbolqueia mutex
				if(time != ETIMEDOUT){//se a thread saiu da condiçao por ter sido notificada pela torre
					strcpy(str,": ");
					strcat(str,nome);//escreve o nome do voo que descolou
					strcat(str," ATERRAGEM ");//tipo de actividade
					strcat(str,ptr_shm[shm_slot].pista);//pista utilizada
					strcpy(log,str);
					strcat(log," iniciada\n");
					escreve_log(log);
					usleep(time_units*landing_time*1000);//tempo de aterragem
					strcpy(log,str);
					strcat(log," concluida\n");
					escreve_log(log);
					pthread_mutex_lock(&ptr_shm[shm_slot].mutex_notifica);//bloqueia o mutex para notificar a torre
					strcpy(ptr_shm[shm_slot].manobra,"END");//para que a torre saiba que o voo terminou
					pthread_cond_signal(&ptr_shm[shm_slot].cond_notifica);//notifica a torre em mutua exclusao
					pthread_mutex_unlock(&ptr_shm[shm_slot].mutex_notifica);//desbolqueia mutex
				}
				else escreve_log(": voo %s redirecionado para outro aeroporto: combustivel => 0\n");
			}
		}
	}
	total_voos_G--;//decrementa contador de voo criados pelo gestor
	pthread_exit(NULL);
	return NULL;
}

ARRIVAL_queue *ordena_A(int prioridade, ARRIVAL_queue *header){
	ARRIVAL_queue *ant,*actual;
	ant = header;
	actual = header->next;
	while(actual!=NULL && actual->eta->ETA < prioridade){//ordena a lista pelos ETA
	       	ant = actual;
		actual = actual->next;
	}
	if(actual!=NULL && actual->eta->ETA != prioridade) actual = NULL;//se nao houver elemento, é o último nó
	return ant;
}

void arrival_fila(int slot, struct torre *ptr_no_torre, ARRIVAL_queue *header){
	ARRIVAL_queue *no, *ant;
	ant = header;
	no = (ARRIVAL_queue*) malloc (sizeof(ARRIVAL_queue));
	strcpy(no->nome,ptr_no_torre->nome);//guarda nome na fila, para poder eliminar o no da torre a partir da lista da fila
	no->shm_pos_A = slot;//guarda o vaor do slot atribudo pela torre no nó da fila de espera
	no->eta = ptr_no_torre;//ponteiro da fila aponta para o nó do voo na torre, para aceder às informaçoes a partir da fila
	ant = ordena_A(no->eta->ETA, header);//ordena a fila pelo ETA do no da torre
	no->next = ant->next;//insere o no
	ant->next = no;
}

DEPARTURES_queue *ordena_D(int prioridade, DEPARTURES_queue *ant, DEPARTURES_queue *header){
	DEPARTURES_queue *actual;
	actual = header->next;
	while(actual!=NULL && actual->take_off < prioridade){//ordena a lista pelos take_off
	       	ant = actual;
		actual = actual->next;
	}
	if(actual!=NULL && actual->take_off != prioridade) actual = NULL;//se nao houver elemento, é o último nó
	return ant;
}

void departure_fila(int slot, struct torre *ptr_no_torre, DEPARTURES_queue *header){
	DEPARTURES_queue *no, *ant;
	ant = header;
	no = (DEPARTURES_queue*) malloc (sizeof(DEPARTURES_queue));
	strcpy(no->nome,ptr_no_torre->nome);//guarda nome na fila, para poder eliminar o no da torre a partir da lista da fila
	no->shm_pos_D = slot;//guarda o vaor do slot atribudo pela torre no nó da fila de espera
	no->take_off = ptr_no_torre->take_off;//guarda o valor do take_off
	ant = ordena_D(no->take_off, ant, header);//ordena a fila
	no->next = ant->next;//insere o no
	ant->next = no;
}

int escalonamento(int conta_take_off){

	DEPARTURES_queue *fila_D;//ponteiro auxiliar para percorre a fila de departures
	ARRIVAL_queue *fila_A;//ponteiro auxiliar para percorre a fila de arrivals
	int pista, prioridade_D=0, prioridade_A=0, conta_no=0;//devolde 28 ou 01 para decidir quem acede à pista
	//NOTA: prioridade maxima = 3
	fila_D = header_D->next;
	while(fila_D){//percorre fila de DEPARTURES
		conta_no++;
		if(fila_D->take_off == ptr_shm[0].tempo){//caso uma mergencia surga e o voo nao possa descolar, descola a seguir
			prioridade_D = 3;//sempre que um departure tiver no tempo de descolar, tem prioridade maxima
			break;
		}
		else  break;//como estao ordenados pelo take_off se o primeiro voo nao tiver take_off = tempo, nenhum tem
		fila_D = fila_D->next;
	}
	conta_no = 0;//reset no contador de
	fila_A = header_A->next;
	while(fila_A){//percorre fila de ARRIVALS
		conta_no++;//conta os nos ja passados
		if(fila_A->eta->emergencia == 1){//caso haja pelo menos um voo em emergencia 
			prioridade_A = 3;//a prioridade de arrival é maxima
			break;//sai para nao alterar a prioridade maxima
		}
		else{//se nao for emergencia a prioridade de arrival é minima, mas tem sempre acesso à pista se nao houver take_offs
			prioridade_A = 1;
			break;
		}
		fila_A = fila_A->next;
	}
	
	if(prioridade_A > prioridade_D)//se a prioridade de arriavls for maior que a departures
		pista = 1;//cede as pistas a arrivals
	if(prioridade_A < prioridade_D)//se a prioridade de arriavls for menor que a departures
		pista = 28;//cede as pistas a departures
	if(prioridade_A == prioridade_D)//se as prioridades forem iguais
		pista = 1;//cede as pistas a arrivals, porquem o arrival está em estado de emergencia
	return pista;
}

void * gere_voos(void * ptr){//thread que atualiza dados e notifica os voos
	
	gere_voo_pid = getpid();
	TORRE_struct *aux;//para percorrer a lista da torre
	DEPARTURES_queue *fila_D;//ponteiro auxiliar para percorre a fila de departures
	ARRIVAL_queue *fila_A;//ponteiro auxiliar para percorre a fila de arrivals
	int tempo_anterior=0,tempo_actual,i=0,pista,conta_take_off=0,conta_arrival=0,conta_no,rand_hold;
	srand(min_hold);//cria uma seed para o rand com o hold minimo
	
	while(1){
		tempo_actual = ptr_shm[0].tempo;//marca o tempo atual
		if(tempo_actual > tempo_anterior){//se passou uma unidade de tempo atualiza a lista
			aux = ptr_torre->next;//inicializa aux para o cabeçalho da lista da torre
			while(aux){//percorre a lista da torre
				if(aux->take_off == 0){//se for um ARRIVAL
					aux->combustivel -= 1;//decrementa o combustivel
					if(aux->combustivel == 0){
						conta_RD++;
						//se o voo ficar sem combustivel dá timeout com o pthread_cond_timedwait
						//aqui so tem de guardar o seu slot no array de slots livres e remover os nós do voo
						ordena_A(aux->ETA, header_A);//reordena a fila de espera
						sem_wait(mutex_array_slots);//bloqueia o acesso ao array de slots livres
						slots_livres[i] = aux->slot;//guarda o slot que ficou livre para se reutilizar
						sem_post(mutex_array_slots);//desbloqueia o acesso
						remove_voo("torre",aux->nome);//remove o no da lista da torre
						if(i == sizeof(slots_livres)) i=0;//reset do array de slots livres
						i++;
					}				
					else if(aux->combustivel <= aux->ETA+landing_time+4){//caso voo em HOLD fique em emergencia
						aux->emergencia = 1;//para o escalonamento saber
						aux->ETA = 0;//poe ETA a zero para ficar no topo da fila
						ordena_A(aux->ETA, header_A);//reordena a fila de espera
					}
				}
				aux = aux->next;//passa para o proximo no
			}
		}//terminou a atualizaçao da lista da torre
		tempo_anterior = tempo_actual;
		pista = 0;
		pista = escalonamento(conta_take_off);//recebe os contadores de descolagens para fazer o escalonamento
		if(pista == 28){//se o tipo de pista devolvida pelo escalonamento é 28, cede a aterragens
			conta_take_off++;
			fila_D = header_D->next;//reset do ponteiro que percorre a fila de espera de departures
			while(fila_D){//notifica o primeiro voo da fila
				strcpy(ptr_shm[fila_D->shm_pos_D].manobra,"TAKEOFF");//acede a posiçao e escrve a manobra
				if(conta_take_off%2 == 0) strcpy(ptr_shm[fila_D->shm_pos_D].pista,"28R");//diz qual a pista
				else strcpy(ptr_shm[fila_D->shm_pos_D].pista,"28L");
				pthread_mutex_lock(&ptr_shm[fila_D->shm_pos_D].mutex_notifica);
				pthread_cond_signal(&ptr_shm[fila_D->shm_pos_D].cond_notifica);//notifica em mutua exclusao
				pthread_mutex_unlock(&ptr_shm[fila_D->shm_pos_D].mutex_notifica);
				pthread_mutex_lock(&ptr_shm[fila_D->shm_pos_D].mutex_notifica);
				while(strcmp(ptr_shm[fila_D->shm_pos_D].manobra,"END")!=0){
					pthread_cond_wait(&ptr_shm[fila_D->shm_pos_D].cond_notifica, &ptr_shm[fila_D->shm_pos_D].mutex_notifica);
					}
				pthread_mutex_unlock(&ptr_shm[fila_D->shm_pos_D].mutex_notifica);				
				usleep(time_units*take_off_break_time*1000);//intervalo entre descolagens
				conta_tempo_D += take_off_time + take_off_break_time;	
				sem_wait(mutex_array_slots);//bloqueia o acesso ao array de slots livres
				slots_livres[i] = fila_D->shm_pos_D;//guarda o slot que ficou livre
				sem_post(mutex_array_slots);//desbloqueia o acesso
				total_voos--;//decrementa contador de voos totais no sistemas
				remove_voo("torre",fila_D->nome);//remove o no da lista da torre
				remove_voo("fila_D",fila_D->nome);//remove o no da fila de departures
				if(i == sizeof(slots_livres)) i=-1;//reset do array de slots livres quando o i chega à ultima posiçao
				i++;
				fila_D = NULL;
			}
		}//terminou a notificaçao de departures
		else if(pista == 1){//se o valor da pista for 1 cede para aterrar
			conta_arrival++;
			conta_no = 0;//contador de nos para saber quando ha mais que 5 voos na fila e po-los em HOLD
			fila_A = header_A->next;//reset do ponteiro que percorre a fila de espera de arrivals
			while(fila_A){//percorre a fila de arrivals
				conta_no++;
				if(conta_no == 1){//notifica o primeiro voo da fila
					strcpy(ptr_shm[fila_A->shm_pos_A].manobra,"LAND");
					if(conta_arrival%2 == 0) strcpy(ptr_shm[fila_A->shm_pos_A].pista,"01R");//diz qual a pista
					else strcpy(ptr_shm[fila_A->shm_pos_A].pista,"01L");			
					pthread_mutex_lock(&ptr_shm[fila_A->shm_pos_A].mutex_notifica);
					pthread_cond_signal(&ptr_shm[fila_A->shm_pos_A].cond_notifica);//notifica em mutua exclusao
					pthread_mutex_unlock(&ptr_shm[fila_A->shm_pos_A].mutex_notifica);
					pthread_mutex_lock(&ptr_shm[fila_A->shm_pos_A].mutex_notifica);
					while(strcmp(ptr_shm[fila_A->shm_pos_A].manobra,"END")!=0)
						pthread_cond_wait(&ptr_shm[fila_A->shm_pos_A].cond_notifica, &ptr_shm[fila_A->shm_pos_A].mutex_notifica);
					pthread_mutex_unlock(&ptr_shm[fila_A->shm_pos_A].mutex_notifica);
					usleep(time_units*landing_break_time*1000);//intervalo entre aterragens
					conta_tempo_A += landing_time + landing_break_time;
					sem_wait(mutex_array_slots);//bloqueia o acesso ao array de slots livres
					slots_livres[i] = fila_A->shm_pos_A;//guarda o slot que ficou livre
					sem_post(mutex_array_slots);//desbloqueia o acesso
					total_voos--;//decrementa contador de voos totais no sistema
					remove_voo("torre",fila_A->nome);//remove o no da lista da torre
					remove_voo("fila_A",fila_A->nome);//remove o no da fila de arrivals
					if(i == sizeof(slots_livres)) i=-1;//reset do array de slots livres
					i++;
				}//fim da condiçao de notificaçao dos 2 primeiros voos da fila
				if(conta_no > 5){//se houver mais que 5 voos na fila faz hold nos voos para lá da 5 posiçao
					conta_voos_HOLD++;
					rand_hold = rand()%max_hold;//faz hold incrementando o ETA min_hold < x < max_hold
					fila_A->eta->ETA  += rand_hold;
					ordena_A(fila_A->eta->ETA, header_A);//se houver voos em HOLD reordena a fila pelo ETA
					total_hold += rand_hold;
				}
				fila_A = fila_A->next;
			}
		}//terminou a notificaçao de arrivals
	}
	return NULL;
}

void * gere_emergencias(void *id){//thread qe recebe mensagens de emergencia e insere o voo na lista da torre na fila de espera
	
	gere_emergencias_pid = getpid();
	TORRE_struct *no,*ant;
	ant = ptr_torre;//ant serve para inserir o no na lista
	while(1){
		if(total_voos == total_departures+total_arrivals){//se o total de voos tiver sido atingido
			escreve_log(": Limite de voos atingido\n");
			while(1){
				msgrcv(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), EMERGENCIA, 0);
				conta_RJ++;
				mensagem.msgtype = VOO;//envia para o voo
				mensagem.slot = -1;//para o voo saber que foi rejeitado
				msgsnd(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), 0);//envia a mensagem ao voo
			 	if(total_voos < total_departures+total_arrivals) break;//se houver terminaçao de voos continua
			 }
		}
		else if(msgrcv(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), EMERGENCIA, 0)){//recebe emergencia
			conta_A++;
			total_voos++;//incrementa contador de voos
			no = (TORRE_struct*) malloc (sizeof(TORRE_struct));//alloca espaçp para um novo nó na lista da torre
			sem_wait(mutex_array_slots);//bloqueia o acesso ao array de slots livres
			for(int i=0;i< total_departures+total_arrivals;i++){//verifica se ha slots reutilizaveis
				if(slots_livres[i]!=-1){
					no->slot = slots_livres[i];//reutiliza slots de voos que ja terminaram
					slots_livres[i] = -1;//limpa o valor do buffer
					break;
				}
				else if(i == total_departures+total_arrivals-1){
					no->slot = gera_slot;//atribibui novo valor de slot
					gera_slot++;//incrementa contador do slot
				}
			}
			sem_post(mutex_array_slots);//desbloqueia o acesso
			strcpy(no->nome,mensagem.nome);//guarda o nome do voo;
			mensagem.slot = no->slot;//escreve o slot atribuido na msq para enviar
			no->take_off = 0;//zera o take_off porque é um arrival
			no->ETA = 0;//poe o ETA zero para que o escalonamento lhe de acesso à pista primeiro que os outros
			no->emergencia = 1;//para o escalonamento saber
			no->combustivel = mensagem.combustivel;//guarda o combustivel
			sem_wait(mutex_torre);//controla a introduçao de um novo nó, porque a torre tambem introduz nós
			no->next = ant->next;
			ant->next = no;
			sem_post(mutex_torre);//desbloqueia semaforo
			arrival_fila(no->slot, (void *) no, header_A);//insere na fila passa a referencia do no da torre
			mensagem.msgtype = VOO;//envia para o voo
			msgsnd(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), 0);//envia a mensagem ao voo
		}
	}
	return NULL;
}

void control_tower(){

	torre_pid = getpid();//guarda o PID da torre
	signal(SIGINT, cleanup);//redireciona o ^C para a funçao cleanup
	TORRE_struct *no,*ant;//para a criaçao de nos da lista da torre e ordenaçao pelas prioridades
	char torre[50],str[10];
	//escreve no log
	sprintf(str, "%d", torre_pid);
	strcat(torre,": Ativação da torre de controlo PID: ");
	strcat(torre,str);
	strcat(torre,"\n");
	escreve_log(torre);
		
	ant = ptr_torre;//ponteiro ant serve para inserir o novo no na lista da torre
	
	//thread que gere as mensagens de urgencia recebe o cabeçalho de lista
	pthread_create(&thread_emergencias, NULL, gere_emergencias, NULL);
	
	//atualiza os dados, poes voos nas filas de espera e notifica-os recebe o cabeçalho da lista
	pthread_create(&thread_update_filas, NULL, gere_voos, NULL);
	
	//gere pedidos, guarda na lista os dados, poe voos nas respectivas filas de espera
	while(1){
		if(total_voos == total_departures+total_arrivals){//se o total de voos tiver sido atingido
			escreve_log(": Limite de voos atingido\n");
			while(1){
				msgrcv(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), TORRE, 0);
				conta_RJ++;//incrementa contador de voos rejeitados
				mensagem.msgtype = VOO;//envia para o voo
				mensagem.slot = -1;//para o voo saber que foi rejeitado
				msgsnd(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), 0);//envia a mensagem ao voo
			 	if(total_voos < total_departures+total_arrivals) break;//se houver terminaçao de voos continua
			 }
		}
		else{//caso o limite de voos ainda nao tenha sido atingido
			msgrcv(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), TORRE, 0);//recebe pedidos dos voos
			total_voos++;//icrementa contador de voos
			//acrescenta um novo no na lista da torre
			no = (TORRE_struct*) malloc (sizeof(TORRE_struct));
			sem_wait(mutex_array_slots);//bloqueia o acesso ao array de slots livres
			for(int i=0;i< total_departures+total_arrivals;i++){//verifica se ha slots reutilizaveis
				if(slots_livres[i]!=-1){
					no->slot = slots_livres[i];//reutiliza slots de voos que ja terminaram
					slots_livres[i] = -1;//limpa o valor do buffer
					break;
				}
				else if(i == total_departures+total_arrivals-1){
					no->slot = gera_slot;//atribibui novo valor de slot
					gera_slot++;//incrementa contador do slot
				}
			}
			sem_post(mutex_array_slots);//desbloqueia o acesso ao array de slots livres
			mensagem.slot = no->slot;//escreve o slot atribuido na msq para enviar
			strcpy(no->nome,mensagem.nome);//guarda o nome do voo
			//para os ARRIVALS
			if(mensagem.take_off == 0){
				conta_A++;
				no->take_off = 0;//zera o take_off porque é um arrival
				no->ETA = mensagem.ETA;//guarda o ETA
				no->combustivel = mensagem.combustivel;//guarda o combustivel
				arrival_fila(no->slot, no, header_A);//insere na fila de ARRIVALS passa o nó da lista da torre
			}
			//para as DEPARTURES
			else if(mensagem.take_off != 0){
				conta_D++;
				no->take_off = mensagem.take_off;//guarda o take_off
				no->ETA = no->combustivel = 0;//zera ETA e combustivel porque é um departure
				departure_fila(no->slot, no, header_D);//insere na fila de DEPARTURES passa o nó da lista da torre
			}
			sem_wait(mutex_torre);//controla introduçao de um novo nó, porque a thraed que gere emergencias tambem introduz
			no->next = ant->next;
			ant->next = no;
			sem_post(mutex_torre);
			mensagem.msgtype = VOO;//envia para o voo
			msgsnd(id_msq, &mensagem, sizeof(MSQ)-sizeof(mensagem.msgtype), 0);//envia a mensagem ao voo
		 }
	}
}

void * gere_tempo(void * id){

	tempo_pid = getpid();//para receber sinal para que morra
	VOOS * aux;//ponteiro para a percorrer a lista do gestor
	int i=0;
	//ptr_shm_tempo = ptr_shm;//inicializa o ponteiro do tempo para a primeira instancia da estrutura de memoria partilhada
	ptr_shm[0].tempo = -1;//inicializar o tempo a zero
	//ciclo que conta o tempo
	while(1){
		ptr_shm[0].tempo++;//incrementa o tempo
		//percorre a lista de voos para ver se é tempo de criar um voo
		aux = ptr_voos->prox_voo;
		while(aux){
			if(ptr_shm[0].tempo == aux->init){//se o tempo for igual a um init qualquer cria uma thread
				pthread_create(&thread_voo[i], NULL, voo_thread, (void *)aux);//(&aux) ponteiro para o no da lista
				i++;//incrementa o identificador
				total_voos_G++;//incrementa contador de voos
			}
			aux = aux->prox_voo;
		}
		printf("---------------------------------------------------------------Time: %d\n",ptr_shm[0].tempo);
		usleep(time_units*10000);
	}	
  	return NULL;
}

int main(){

	gestor_pid = getpid();
	FILE * log = fopen("log.txt","w");//limpa o log cada vez que o programa e corrido
	fclose(log);
	
	signal(SIGINT, cleanup);//redireciona o ^C para a funçao cleanup
	
	//inicializa os semaforos, memoria partilhada e MSQ
	inicializa();
	
	char str[LEN*3],converte[LEN];
	sprintf(converte, "%d", gestor_pid);
	strcpy(str,": Inicio do gestor de simulação  PID: ");
	strcat(str,converte);
	strcat(str,"\n");
	escreve_log(str);
	
	//conta tempo, corre durante todo o programa
	pthread_create(&thread_tempo, NULL, gere_tempo, NULL);

	if(fork() == 0){
		control_tower();//cria processo filho torre
		exit(0);
	}

	//como o main ja nao tem de chamar nem criar mais nada pode ficar permanentemente a ler o pipe, assim gastam-se menos recursos
	read_pipe();	

	return 0;
}
