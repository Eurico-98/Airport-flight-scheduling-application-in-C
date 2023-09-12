#include "header.h"

int main() 
{ 
    	int fd_pipe,Comandos_iniciais=0,FD_BUFF=0;
    	char comando[TAMANHO],fim[10];
    	FILE * comandos;//para abrir o ficheiro

   	if((mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0600)<0) && (errno!= EEXIST)){
    		perror("mkfifo(): Couldn't create named pipe");
    		exit(0);
    	}
    	
	if ((fd_pipe = open(PIPE_NAME, O_RDWR)) < 0) {
		perror("open(): Can't open pipe for reading");
		exit(0);
	}

	while(1){
		printf("\nCarrega comandos\n");
		if(Comandos_iniciais == 0){//envia todos os comandos no ficheiro pela primeira veez
			comandos = fopen("COMANDOS.txt","r");
			while(fgets(comando,TAMANHO,comandos)!=NULL && FD_BUFF<=PIPE_BUF){//PIPE_BUF ve, de limits.h: max de bytes do fd
				printf("\nA enviar: %s\n",comando);
				write(fd_pipe,comando,sizeof(comando));
				FD_BUFF += sizeof(comando);//para ter a certeza que nao se ultrapassa a capacidade do file descriptor
				if(FD_BUFF == PIPE_BUF) fflush(comandos);//se o pipe ficar cheio limpa-o para poder continuar a enviar
			}
			fclose(comandos);
			Comandos_iniciais = 1;//variavel que serve para confirmar que os comandos iniciais foram enviados uma vez
		}
		else{//caso os comandos inicias ja tenham sido enviados a primeira vez envia apenas os novos comandos
			printf("\nInserir novo(s) comando(s) a enviar ao gestor:\n(inserir 'enviar' para enviar)\n");
			comandos = fopen("COMANDOS.txt","w");//escreve os novos comandos no ficheiro e depois envia
			while(1){
				fgets(comando,TAMANHO,stdin);//recebe um comando
				if(strcmp(comando,"enviar\n")==0)break;
				fprintf(comandos,"%s",comando);//escreve o novo comando
			}
			fclose(comandos);
			//de seguida abre novamente o ficheiro mas em modo leitura para enviar os novos comandos
			comandos = fopen("COMANDOS.txt","r");
			while(fgets(comando,TAMANHO,comandos)!=NULL && FD_BUFF<=PIPE_BUF){
				printf("\nA enviar: %s\n",comando);
				write(fd_pipe,comando,sizeof(comando));
				FD_BUFF += sizeof(comando);//para ter a certeza que nao se ultrapassa a capacidade do file descriptor
				if(FD_BUFF == PIPE_BUF) fflush(comandos);//se o pipe ficar cheio limpa-o para poder continuar a enviar
			}
			fclose(comandos);
		}
		printf("\nInserir close para fechar o pipe\n'Enter' para ler novos comandos do ficheiro\n");
		if(strcmp(fgets(fim,10,stdin),"close\n") == 0) break;
	}
	close(fd_pipe);      
}   
