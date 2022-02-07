/*
client.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>

enum msg_t { CONNECT, // server->client: Informar usuario conectado
	DISCONNECT, // server->client: informar usuario desconectado
	LIM_CONECTION, // server->client: Maximo número de clientes alcanzado
	ACCEPT,  // server->client: Acepta la conexion y luego el nombre de usuario enviado por el cliente
	USER_EXISTS,  // server->client: EL nombre de usuario ya existe
	ALL, // cliente<->server: Mensaje a todos los host
	TO // cliente<->server: Mensaje privado
};

struct msg { // estructura del mensaje
	enum msg_t tipo;
	char from[21], to[21];
	char msg[257];
};
const int MSG_SZ = sizeof(struct msg);

int id_sem, sock_fd;

struct sembuf escritura[] = {{0, -1, 0}};
struct sembuf parar_escritura[] = {{0, +1, 0}};

int pos = 0, cnt = 0;
char log_buff[15][500];
char user[21];
char buff[1000];

void print_log() {
	int start = 0;
	if (cnt == 15)
		start = pos;

	for (int i = 0; i < 15; i++) {
		printf("\033[%d;1H\033[K", i+11);
		if (i < cnt)
			printf("%s", log_buff[(start+i)%15]);
		printf("");
	}
	printf("\033[27;1HYOU: ");
	printf("\033[K");
	fflush(stdout);
}

void *recibe_msg(void *arg) {
	struct msg Msg;
	char LOG[500];
	while (true) {
		if (recv(sock_fd, (char *)&Msg, MSG_SZ, 0) <= 0) {
			semop(id_sem, escritura, 1);
			printf("\033[27;1\033[KSe desconectó el server\n");
			semop(id_sem, parar_escritura, 1);
			exit(-1);	
		}

		switch(Msg.tipo) {
			case CONNECT: sprintf(LOG, "Server: %s se ha conectado", Msg.from);
						  break;
			case DISCONNECT: sprintf(LOG, "Server: %s se ha desconectado", Msg.from);
							 break;
			case ALL: sprintf(LOG, "%s: %s", Msg.from, Msg.msg);
					  break;
			default: sprintf(LOG, "%s (Mensaje Privado): %s", Msg.from, Msg.msg);
					 break;
		}

		semop(id_sem, escritura, 1);
		strcpy(log_buff[pos], LOG);
		pos = (pos+1)%15;
		cnt += (cnt < 15);
		print_log(log_buff, pos, cnt);
		semop(id_sem, parar_escritura, 1);
	}
}

void *envia_msg(void *arg) {
	struct msg Msg;
	strcpy(Msg.from, user);
	while (true) {
		fgets(buff, sizeof(buff), stdin);
		int l = strlen(buff);
		buff[l-1] = '\0';

		if (buff[0] == '@') { // Mensaje Privado
			Msg.tipo = TO;
			int pp = 1;
			while (buff[pp] != ' ') pp ++;
			strncpy(Msg.to, buff+1, pp-1);
			Msg.to[pp-1] = '\0';
			strcpy(Msg.msg, buff+pp+1);

			semop(id_sem, escritura, 1);
			sprintf(log_buff[pos], "TU (Mensaje para %s): %s", Msg.to, Msg.msg);
			pos = (pos+1)%15;
			cnt += (cnt < 15);
			print_log();
			semop(id_sem, parar_escritura, 1);
		} else { // Multimensaje
			Msg.tipo = ALL;
			strcpy(Msg.msg, buff);

			semop(id_sem, escritura, 1);
			sprintf(log_buff[pos], "TU : %s",	 Msg.msg);
			pos = (pos+1)%15;
			cnt += (cnt < 15);
			print_log();
			semop(id_sem, parar_escritura, 1);
		}
		send(sock_fd, (char *)&Msg, MSG_SZ, 0);
	}
}


int main(int argc, char **argv) {
	system("clear");
	printf("Introduzca la dirección del servidor: ");
	char addr[100]; scanf("%s", addr);
	printf("Introduzca el puerto que desean emplear: ");
	int port; scanf("%d", &port);

	sock_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (sock_fd == -1) {
		printf("Error al crear el socket: %s\n", strerror(errno));
		exit(-1);
	}
	
	// Creando semaforo para escribir en la consola
	id_sem = semget(IPC_PRIVATE, 1, 0666 | IPC_CREAT);
 	if (id_sem < 0) {
 		printf("Error al crear semáforos: %s\n", strerror(errno));
 		exit(-1);
 	}
 	semctl(id_sem, 0, SETVAL, 1);

	// Creando estructura para la conexion
	struct sockaddr_in server;
	struct hostent* hostinfo;
	
	server.sin_family = AF_INET;
	hostinfo = gethostbyname(addr);
	if (hostinfo == NULL) {
		printf("Error, no existe el host\n");
		exit(-1);
	}

   	server.sin_addr = *((struct in_addr *)hostinfo->h_addr);
	server.sin_port = htons(port);

	if (connect (sock_fd, (struct sockaddr *)&server, sizeof(server)) == -1) {
		printf("Error al conectar: %s\n", strerror(errno));
		exit(-1);
	}

	struct msg Msg;
	if (recv(sock_fd, (char *)&Msg, MSG_SZ, 0) <= 0) {
		printf("Se desconectó el server\n");
		exit(-1);
	}
	if (Msg.tipo == LIM_CONECTION) {
		printf("El servidor está lleno, intente de nuevo más tarde\n");
		exit(-1);
	}
	assert(Msg.tipo == ACCEPT);
	getchar();
	while (true) {
		printf("Inserte un nombre de usuario (de hasta 20 caracteres ascii): ");
		fgets(buff, sizeof(buff), stdin);
		int l = strlen(buff);
		
		if (l-1 > 20) { 
			printf("El nombre insertado excede el limite de caracteres establecidos\n");
			continue;
		}
		
		buff[l-1] = '\0';
		strcpy(Msg.from, buff);

		send(sock_fd, (char *)&Msg, MSG_SZ, 0);
		
		if (recv(sock_fd, (char *)&Msg, MSG_SZ, 0) <= 0) {
			printf("Se desconectó el server\n");
			exit(-1);	
		}
		if (Msg.tipo == ACCEPT)
			break;
		printf("Ya existe un usuario con el mismo username.\n");
	}

	strcpy(user, buff);
	system("clear");	
	printf("#################################################################################\n");
	printf("Bienvenido %s\n", user);
	printf("Puedes enviar mensajes de hasta 256 caracteres\n");
	printf("Se muestran los ultimos 15 mensajes\n");
	printf("Para enviar un mensaje privado: @<user> <msj>, donde <user> es el nombre\n\
del destinatario del mensaje y <msj> el mensaje deseado\n");
	printf("Para salir presiona Cntrl + C\n");
	printf("Nota: Se debe ajustar la consola para entre todo el area ocupada por el programa\n");
	printf("#################################################################################\n");

	// Mueve cursor hasta la pos de escritura del usuario
	printf("\033[27;1HYOU: ");

	pthread_t id[2];
	pthread_create(&id[0], NULL, &recibe_msg, NULL);
	pthread_create(&id[1], NULL, &envia_msg, NULL);

	pthread_join(id[0], NULL);
	pthread_join(id[1], NULL);

	return 0;
}
