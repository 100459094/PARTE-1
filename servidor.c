#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define MAX_FILENAME 256
#define MAX_DESCRIPTION 256
#define BUFFER_SIZE 1024

// Estructura para almacenar información de usuarios
typedef struct {
    char username[256];
    char ip[16];
    int port;
    int connected;
    char **files;
    int num_files;
    pthread_mutex_t lock;
} user_info;

// Variables globales
user_info *users = NULL;
int num_users = 0;
pthread_mutex_t users_lock = PTHREAD_MUTEX_INITIALIZER;
int server_running = 1;

// Funciones auxiliares
void *handle_client(void *socket_desc);
int find_user(const char *username);
void add_user(const char *username);
void remove_user(const char *username);
void add_file(int user_index, const char *filename);
void remove_file(int user_index, const char *filename);
char* read_string(int socket);

// Manejador de señal para terminar el servidor
void handle_signal(int sig) {
    server_running = 0;
}

int main(int argc, char *argv[]) {
    int server_socket, client_socket, port;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    pthread_t thread_id;
    char local_ip[16];

    // Verificar argumentos
    if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "Uso: %s -p <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[2]);

    // Configurar manejador de señal
    signal(SIGINT, handle_signal);

    // Crear socket del servidor
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creando socket");
        exit(1);
    }

    // Configurar dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Obtener IP local
    struct sockaddr_in local_addr;
    socklen_t len = sizeof(local_addr);
    getsockname(server_socket, (struct sockaddr *)&local_addr, &len);
    inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, sizeof(local_ip));

    // Bind
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        exit(1);
    }

    // Listen
    if (listen(server_socket, 10) < 0) {
        perror("Error en listen");
        exit(1);
    }

    printf("s> init server %s:%d\n", local_ip, port);
    printf("s>\n");

    // Bucle principal del servidor
    while (server_running) {
        client_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            if (server_running) {
                perror("Error en accept");
            }
            continue;
        }

        // Crear estructura para pasar al hilo
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_socket;

        // Crear nuevo hilo para manejar la conexión
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Error creando hilo");
            close(client_socket);
            free(new_sock);
            continue;
        }
        pthread_detach(thread_id);
    }

    // Liberar recursos
    close(server_socket);
    for (int i = 0; i < num_users; i++) {
        pthread_mutex_destroy(&users[i].lock);
        for (int j = 0; j < users[i].num_files; j++) {
            free(users[i].files[j]);
        }
        free(users[i].files);
    }
    free(users);
    pthread_mutex_destroy(&users_lock);

    return 0;
}

void *handle_client(void *socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);
    char *operation = read_string(sock);
    
    if (!operation) {
        close(sock);
        return NULL;
    }

    char *username = read_string(sock);
    if (!username) {
        free(operation);
        close(sock);
        return NULL;
    }

    printf("s> OPERATION %s FROM %s\n", operation, username);

    if (strcmp(operation, "REGISTER") == 0) {
        pthread_mutex_lock(&users_lock);
        if (find_user(username) >= 0) {
            // Usuario ya existe
            char response = 1;
            send(sock, &response, 1, 0);
        } else {
            // Registrar nuevo usuario
            add_user(username);
            char response = 0;
            send(sock, &response, 1, 0);
        }
        pthread_mutex_unlock(&users_lock);
    }
    else if (strcmp(operation, "UNREGISTER") == 0) {
        pthread_mutex_lock(&users_lock);
        int user_index = find_user(username);
        if (user_index < 0) {
            // Usuario no existe
            char response = 1;
            send(sock, &response, 1, 0);
        } else {
            // Eliminar usuario
            remove_user(username);
            char response = 0;
            send(sock, &response, 1, 0);
        }
        pthread_mutex_unlock(&users_lock);
    }
    else if (strcmp(operation, "CONNECT") == 0) {
        char *port_str = read_string(sock);
        if (!port_str) {
            free(operation);
            free(username);
            close(sock);
            return NULL;
        }

        pthread_mutex_lock(&users_lock);
        int user_index = find_user(username);
        
        if (user_index < 0) {
            // Usuario no existe
            char response = 1;
            send(sock, &response, 1, 0);
        }
        else if (users[user_index].connected) {
            // Usuario ya conectado
            char response = 2;
            send(sock, &response, 1, 0);
        }
        else {
            // Conectar usuario
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            getpeername(sock, (struct sockaddr*)&addr, &len);
            strcpy(users[user_index].ip, inet_ntoa(addr.sin_addr));
            users[user_index].port = atoi(port_str);
            users[user_index].connected = 1;
            char response = 0;
            send(sock, &response, 1, 0);
        }
        pthread_mutex_unlock(&users_lock);
        free(port_str);
    }
    // Implementar resto de operaciones...

    free(operation);
    free(username);
    close(sock);
    return NULL;
}

// Funciones auxiliares
char* read_string(int socket) {
    char *buffer = malloc(BUFFER_SIZE);
    int i = 0;
    
    while (i < BUFFER_SIZE - 1) {
        if (recv(socket, &buffer[i], 1, 0) <= 0) {
            free(buffer);
            return NULL;
        }
        if (buffer[i] == '\0') break;
        i++;
    }
    buffer[i] = '\0';
    return buffer;
}

int find_user(const char *username) {
    for (int i = 0; i < num_users; i++) {
        if (strcmp(users[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

void add_user(const char *username) {
    users = realloc(users, (num_users + 1) * sizeof(user_info));
    strcpy(users[num_users].username, username);
    users[num_users].connected = 0;
    users[num_users].files = NULL;
    users[num_users].num_files = 0;
    pthread_mutex_init(&users[num_users].lock, NULL);
    num_users++;
}

void remove_user(const char *username) {
    int index = find_user(username);
    if (index < 0) return;

    pthread_mutex_destroy(&users[index].lock);
    for (int i = 0; i < users[index].num_files; i++) {
        free(users[index].files[i]);
    }
    free(users[index].files);

    for (int i = index; i < num_users - 1; i++) {
        users[i] = users[i + 1];
    }
    num_users--;
    users = realloc(users, num_users * sizeof(user_info));
}

void add_file(int user_index, const char *filename) {
    user_info *user = &users[user_index];
    user->files = realloc(user->files, (user->num_files + 1) * sizeof(char*));
    user->files[user->num_files] = strdup(filename);
    user->num_files++;
}

void remove_file(int user_index, const char *filename) {
    user_info *user = &users[user_index];
    for (int i = 0; i < user->num_files; i++) {
        if (strcmp(user->files[i], filename) == 0) {
            free(user->files[i]);
            for (int j = i; j < user->num_files - 1; j++) {
                user->files[j] = user->files[j + 1];
            }
            user->num_files--;
            user->files = realloc(user->files, user->num_files * sizeof(char*));
            break;
        }
    }
}