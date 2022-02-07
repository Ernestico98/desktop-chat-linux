/*
server.c
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
#include <pthread.h>
#include <assert.h>

const ushort N = 45; // maxima cantidad de clientes
const ushort MAX_REMOTO = 15;
const ushort MAX_LOCAL = 30;

enum tipo_t {NONE, LOCAL, REMOTE};
struct shm_zone {
	int cnt_local, cnt_remoto; // cant de clientes local y remoto
	int socket_pool[45]; // sockets para los clientes
	enum tipo_t tipo[45]; // toma valores de tipo_t
	char username[45][21]; // nombre de usuario del cliente, hasta 20 caracteres
	int sem_send; // semaforo para sincronizar la escritura por los socket de los clientes
	pthread_t ids[45];
}m;

enum msg_t { CONNECT, // server->client: Informar usuario conectado
	DISCONNECT, // server->client: informar usuario desconectado
	LIM_CONECTION, // server->client: Maximo número de clientes alcanzado
	ACCEPT,  // server->client: Acepta la conexion y luego el nombre de usuario enviado por el cliente
	USER_EXISTS,  // server->client: EL nombre de usuario ya existe
	ALL, // cliente<->server: Mensaje a todos los users
	TO // cliente<->server: Mensaje privado
};

struct msg { // estructura del mensaje
	enum msg_t tipo;
	char from[21], to[21];
	char msg[257];
};
const int MSG_SZ = sizeof(struct msg);

int id_sem;
// Lectura a la zona compartida
struct sembuf lectura[] = {{0, 0, 0}, {1, -1, 0}};
struct sembuf parar_lectura[] = {{1, +1, 0}};

// Escritura a la zona compartida
struct sembuf escritura[] = {{0, +1, 0}, {1, -(N+1), 0}};
struct sembuf parar_escritura[] = {{0, -1, 0}, {1, +(N+1), 0}};

struct param {
	int sock_fd;
	char ipstr[INET6_ADDRSTRLEN];
};

void *thread(void *arg) {
	// Escritura en los socket (Cambiar primer valor de la tupla por la posición del socket en el grupo)
	// Estan locales a la funcion pq cambian en dependencia del destinatario
	struct sembuf enviar[] = {{0, -1, 0}};
	struct sembuf parar_enviar[] = {{0, +1, 0}};			

	struct param info = *((struct param *)arg);

	enum tipo_t connection = REMOTE;
	if (!strcmp(info.ipstr, "127.0.0.1"))
		connection = LOCAL;

	semop(id_sem, lectura, 2);
	int cnt = (connection == REMOTE)? m.cnt_remoto : m.cnt_local;
	semop(id_sem, parar_lectura, 2);

	pthread_t id = pthread_self();
	struct msg Msg; 
	// Chequear si ya se alcanzo la maxima cantidad de clientes
	if ((connection == REMOTE && cnt == MAX_REMOTO) || (connection == LOCAL && cnt == MAX_LOCAL)) {
		Msg.tipo = LIM_CONECTION;
		send(info.sock_fd, (char *)&Msg, MSG_SZ, 0);
		printf("Thread %lld, cliente en %s: Limite de clientes alcanzado para el tipo de conexion solicitada\n", (long long)id, info.ipstr);
		return NULL;
	}

	// Acepta la conexion y espera por nombre de usuario
	printf("Thread %lld, cliente en %s: Aceptada la conexión\n", (long long)id, info.ipstr);
	Msg.tipo = ACCEPT;
	send(info.sock_fd, (char *)&Msg, MSG_SZ, 0);

	// Chequea si el nombre de usuario ya existe
	bool exists = true;
	while (exists) {
		exists = false;
		printf("Thread %lld, cliente en %s: Solicitando nombre de usuario a cliente\n", (long long)id, info.ipstr);
		if (recv(info.sock_fd, (char *)&Msg, MSG_SZ, 0) <= 0) {
			printf("Thread %lld, cliente en %s: Falló la conexión con el cliente. Cerrando sesión\n", (long long)id, info.ipstr);
			close(info.sock_fd);
			return NULL;
		}
		
		semop(id_sem, lectura, 2);
		for (int i = 0; i < N; i++) {
			if (m.tipo[i] != NONE && !strcmp(m.username[i], Msg.from)) {
				exists = true;
				break;
			}
		}
		semop(id_sem, parar_lectura, 2);	

		if (exists == true) {
			Msg.tipo = USER_EXISTS;
			send(info.sock_fd, (char *)&Msg, MSG_SZ, 0);
			printf("Thread %lld, cliente en %s: Ya existe un usuario con el nombre %s\n", (long long)id, info.ipstr, Msg.from);
		}
	}

 	// Aceptar nombre de usuario
 	char user[21];
 	strcpy(user, Msg.from);
	Msg.tipo = ACCEPT;
	send(info.sock_fd, (char *)&Msg, MSG_SZ, 0);
	printf("Thread %lld, cliente en %s: Nombre de usuario %s aceptado\n", (long long)id, info.ipstr, Msg.from);

	// Agregar info de usuario a la memoria compartida
	int pos;
	semop(id_sem, escritura, 2);
	for (int i = 0; i < N; i++) 
		if (m.tipo[i] == NONE) {
			pos = i;
			m.tipo[i] = connection;
			strcpy(m.username[i], user);
			if (connection == LOCAL)
				m.cnt_local ++;
			else
				m.cnt_remoto ++;
			m.socket_pool[i] = info.sock_fd;
			m.ids[i] = id;
			semctl(m.sem_send, i, SETVAL, 1);
			break;
		}
	semop(id_sem, parar_escritura, 2);

	// Informar a todos que el usuario se ha conectado
	printf("Thread %lld, cliente en %s: Informando a todos que %s se ha conectado\n", (long long)id, info.ipstr, user);
	Msg.tipo = CONNECT;
	strcpy(Msg.from, user);			
	semop(id_sem, lectura, 2);
	for (int i = 0; i < N; i++)
		if (m.tipo[i] != NONE && pos != i) {
			enviar[0].sem_num = i;
			parar_enviar[0].sem_num = i;
			semop(m.sem_send, enviar, 1);
			send(m.socket_pool[i], (char *)&Msg, MSG_SZ, 0);
			semop(m.sem_send, parar_enviar, 1);
		}
	semop(id_sem, parar_lectura, 2);

	while(true) { // Escucha el socket para recibir mensajes del cliente
		int n = recv(info.sock_fd, (char *)&Msg, MSG_SZ, 0);
		if (n <= 0) { // el cliente se desconecto
			printf("Thread %lld, cliente en %s: El usuario %s se ha desconectado\n", (long long)id, info.ipstr, user);
			// Actualiza info de los clientes
			semop(id_sem, escritura, 2);
			close(info.sock_fd);
			m.tipo[pos] = NONE;
			if (connection == LOCAL)
				m.cnt_local --;
			else
				m.cnt_remoto --;
			semop(id_sem, parar_escritura, 2);

			// Informa que el cliente se desconecto
			printf("Thread %lld, cliente en %s: Informando a todos que %s se ha desconectado\n", (long long)id, info.ipstr, user);
			Msg.tipo = DISCONNECT;
			strcpy(Msg.from, user);
			semop(id_sem, lectura, 2);
			for (int i = 0; i < N; i++)
				if (m.tipo[i] != NONE && pos != i) {
					enviar[0].sem_num = i;
					parar_enviar[0].sem_num = i;
					semop(m.sem_send, enviar, 1);
					send(m.socket_pool[i], (char *)&Msg, MSG_SZ, 0);
					semop(m.sem_send, parar_enviar, 1);
				}
			semop(id_sem, parar_lectura, 2);	
			return NULL;
		}
	
		if (Msg.tipo == ALL) {
			printf("Thread %lld, cliente en %s: Multimensaje recibido de %s. Despachando...\n", (long long)id, info.ipstr, user);
			semop(id_sem, lectura, 2);
			for (int i = 0; i < N; i++)
				if (m.tipo[i] != NONE && pos != i) {
					enviar[0].sem_num = i;
					parar_enviar[0].sem_num = i;
					semop(m.sem_send, enviar, 1);
					send(m.socket_pool[i], (char *)&Msg, MSG_SZ, 0);
					semop(m.sem_send, parar_enviar, 1);
				}
			semop(id_sem, parar_lectura, 2);	
		} else if (Msg.tipo == TO) {
			printf("Thread %lld, cliente en %s: Mensaje privado recibido de %s para %s. Despachando...\n", (long long)id, info.ipstr, user, Msg.to);
			semop(id_sem, lectura, 2);
			for (int i = 0; i < N; i++) {
				if (m.tipo[i] != NONE && pos != i && !strcmp(Msg.to, m.username[i])) {
					enviar[0].sem_num = i;
					parar_enviar[0].sem_num = i;
					semop(m.sem_send, enviar, 1);
					send(m.socket_pool[i], (char *)&Msg, MSG_SZ, 0);
					semop(m.sem_send, parar_enviar, 1);
					break;
				}
			}
			semop(id_sem, parar_lectura, 2);	
		} 
	}
	return NULL;
}

int main(int argc, char **argv) {
	system("clear");
	printf("Introduzca el puerto que desea utilizar: ");
	int port; scanf("%d", &port);

	// Inicializando memoria compartida entre los hilos
 	m.cnt_local = m.cnt_remoto = 0;
 	for (int i = 0; i < N; i++)
 		m.tipo[i] = NONE;

 	// Creando e inicializando semaforos para control de acceso a la memoria compartida
 	id_sem = semget(IPC_PRIVATE, 2, 0666 | IPC_CREAT);
 	if (id_sem < 0) {
 		printf("Error al crear semáforos: %s\n", strerror(errno));
 		exit(-1);
 	}
 	ushort sem_init[] = {0, N+1};
 	if (semctl(id_sem, 2, SETALL, sem_init) < 0) {
 		printf("Error al inicializar semáforos: %s\n", strerror(errno));
 		exit(-1);
 	}
 	
	// Creando semaforos para control de escritura en los socket de los clientes
	m.sem_send = semget(IPC_PRIVATE, N, 0666 | IPC_CREAT);
	if (m.sem_send < 0) {
		printf("Error al crear semáforos: %s\n", strerror(errno));
 		exit(-1);
	} 
	
 	// Creando socket para la conexión con los clientes
	int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		printf("Error al crear el socket: %s\n", strerror(errno));
		exit(-1);
	}

	struct sockaddr_in server;
	server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    if (bind(sock_fd, (struct sockaddr*)&server, sizeof(server)) < 0) {
    	printf("Error al enlazar el socket: %s\n", strerror(errno));
    	exit(-1);
    }

    listen(sock_fd, N); // 30 usuarios conectados local + 15 en otros hosts

    printf("Servidor creado con éxito en el puerto %d\n", port);

    struct sockaddr_in address;
    int addrlen = sizeof(address);
    struct param info;
    pthread_t id;
    while (1) { // escucha conexiones
    	info.sock_fd = accept(sock_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
	    inet_ntop(AF_INET, &address.sin_addr, info.ipstr, sizeof info.ipstr);
    	printf("Petición de conexión recibida desde %s\n", info.ipstr);

		pthread_create(&id, NULL, &thread, (void *)&info);
    }

    return 0;
}
