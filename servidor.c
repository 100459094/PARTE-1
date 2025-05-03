#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

// Definiciones según el PDF
#define MAX_FILENAME 256
#define MAX_DESCRIPTION 256
#define MAX_USERNAME 256
#define BUFFER_SIZE 1024
#define BACKLOG 10

// Códigos de respuesta según el PDF
#define RC_OK 0
#define RC_USER_EXISTS 1
#define RC_ERROR 2
#define RC_USER_NOT_CONNECTED 2
#define RC_ALREADY_CONNECTED 2
#define RC_CONTENT_EXISTS 3
#define RC_REMOTE_USER_NOT_EXISTS 3
#define RC_OTHER_ERROR 4

typedef struct {
    char filename[MAX_FILENAME];
    char description[MAX_DESCRIPTION];
} file_info;

typedef struct {
    char username[MAX_USERNAME];
    char ip[16];
    int port;
    int connected;
    file_info *files;
    int num_files;
    pthread_mutex_t lock;
} user_info;

// Variables globales
user_info *users = NULL;
int num_users = 0;
pthread_mutex_t users_lock = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t server_running = 1;

// Prototipos de funciones
void *handle_client(void *socket_desc);
int find_user(const char *username);
void add_user(const char *username);
void remove_user(const char *username);
void add_file(int user_index, const char *filename, const char *description);
void remove_file(int user_index, const char *filename);
char* read_string(int socket);
int send_string(int socket, const char *str);
void handle_signal(int sig);

void handle_signal(int sig) {
    printf("\ns> Servidor terminando...\n");
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

    // Configurar manejador de señal mejorado
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Crear socket del servidor
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creando socket");
        exit(1);
    }

    // Permitir reutilización del puerto
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Error en setsockopt");
        close(server_socket);
        exit(1);
    }

    // Configurar dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        close(server_socket);
        exit(1);
    }

    // Listen
    if (listen(server_socket, BACKLOG) < 0) {
        perror("Error en listen");
        close(server_socket);
        exit(1);
    }

    // Obtener IP local
    struct sockaddr_in local_addr;
    socklen_t len = sizeof(local_addr);
    getsockname(server_socket, (struct sockaddr*)&local_addr, &len);
    inet_ntop(AF_INET, &(local_addr.sin_addr), local_ip, sizeof(local_ip));

    printf("s> init server %s:%d\n", local_ip, port);
    printf("s>\n");

    // Bucle principal del servidor
    while (server_running) {
        client_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (!server_running) {
            break;
        }

        if (client_socket < 0) {
            if (server_running) {
                perror("Error en accept");
            }
            continue;
        }

        int *new_sock = malloc(sizeof(int));
        *new_sock = client_socket;

        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Error creando hilo");
            close(client_socket);
            free(new_sock);
            continue;
        }
        pthread_detach(thread_id);
    }

    // Limpieza final
    close(server_socket);
    
    // Liberar memoria y recursos
    for (int i = 0; i < num_users; i++) {
        pthread_mutex_destroy(&users[i].lock);
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
            char response = RC_USER_EXISTS;
            send(sock, &response, 1, 0);
        } else {
            add_user(username);
            char response = RC_OK;
            send(sock, &response, 1, 0);
        }
        pthread_mutex_unlock(&users_lock);
    }
    else if (strcmp(operation, "UNREGISTER") == 0) {
        pthread_mutex_lock(&users_lock);
        int user_index = find_user(username);
        if (user_index < 0) {
            char response = RC_ERROR;
            send(sock, &response, 1, 0);
        } else {
            remove_user(username);
            char response = RC_OK;
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
            char response = RC_ERROR;
            send(sock, &response, 1, 0);
        }
        else if (users[user_index].connected) {
            char response = RC_ALREADY_CONNECTED;
            send(sock, &response, 1, 0);
        }
        else {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            getpeername(sock, (struct sockaddr*)&addr, &len);
            strcpy(users[user_index].ip, inet_ntoa(addr.sin_addr));
            users[user_index].port = atoi(port_str);
            users[user_index].connected = 1;
            char response = RC_OK;
            send(sock, &response, 1, 0);
        }
        pthread_mutex_unlock(&users_lock);
        free(port_str);
    }
    else if (strcmp(operation, "DISCONNECT") == 0) {
        pthread_mutex_lock(&users_lock);
        int user_index = find_user(username);
        
        if (user_index < 0) {
            char response = RC_ERROR;
            send(sock, &response, 1, 0);
        }
        else if (!users[user_index].connected) {
            char response = RC_USER_NOT_CONNECTED;
            send(sock, &response, 1, 0);
        }
        else {
            users[user_index].connected = 0;
            char response = RC_OK;
            send(sock, &response, 1, 0);
        }
        pthread_mutex_unlock(&users_lock);
    }
    else if (strcmp(operation, "PUBLISH") == 0) {
        char *filename = read_string(sock);
        char *description = read_string(sock);
        
        if (!filename || !description) {
            free(filename);
            free(description);
            close(sock);
            return NULL;
        }

        pthread_mutex_lock(&users_lock);
        int user_index = find_user(username);
        
        if (user_index < 0) {
            char response = RC_ERROR;
            send(sock, &response, 1, 0);
        }
        else if (!users[user_index].connected) {
            char response = RC_USER_NOT_CONNECTED;
            send(sock, &response, 1, 0);
        }
        else {
            pthread_mutex_lock(&users[user_index].lock);
            int file_exists = 0;
            for (int i = 0; i < users[user_index].num_files; i++) {
                if (strcmp(users[user_index].files[i].filename, filename) == 0) {
                    file_exists = 1;
                    break;
                }
            }
            
            if (file_exists) {
                char response = RC_CONTENT_EXISTS;
                send(sock, &response, 1, 0);
            } else {
                add_file(user_index, filename, description);
                char response = RC_OK;
                send(sock, &response, 1, 0);
            }
            pthread_mutex_unlock(&users[user_index].lock);
        }
        pthread_mutex_unlock(&users_lock);
        
        free(filename);
        free(description);
    }
    else if (strcmp(operation, "DELETE") == 0) {
        char *filename = read_string(sock);
        if (!filename) {
            close(sock);
            return NULL;
        }

        pthread_mutex_lock(&users_lock);
        int user_index = find_user(username);
        
        if (user_index < 0) {
            char response = RC_ERROR;
            send(sock, &response, 1, 0);
        }
        else if (!users[user_index].connected) {
            char response = RC_USER_NOT_CONNECTED;
            send(sock, &response, 1, 0);
        }
        else {
            pthread_mutex_lock(&users[user_index].lock);
            int file_found = 0;
            
            for (int i = 0; i < users[user_index].num_files; i++) {
                if (strcmp(users[user_index].files[i].filename, filename) == 0) {
                    file_found = 1;
                    remove_file(user_index, filename);
                    break;
                }
            }
            
            if (file_found) {
                char response = RC_OK;
                send(sock, &response, 1, 0);
            } else {
                char response = RC_ERROR;
                send(sock, &response, 1, 0);
            }
            
            pthread_mutex_unlock(&users[user_index].lock);
        }
        pthread_mutex_unlock(&users_lock);
        free(filename);
    }
    else if (strcmp(operation, "LIST USERS") == 0) {
        pthread_mutex_lock(&users_lock);
        int user_index = find_user(username);
        
        if (user_index < 0) {
            char response = RC_ERROR;
            send(sock, &response, 1, 0);
        }
        else if (!users[user_index].connected) {
            char response = RC_USER_NOT_CONNECTED;
            send(sock, &response, 1, 0);
        }
        else {
            char response = RC_OK;
            send(sock, &response, 1, 0);
            
            int connected_users = 0;
            for (int i = 0; i < num_users; i++) {
                if (users[i].connected) connected_users++;
            }
            
            char num_str[16];
            sprintf(num_str, "%d", connected_users);
            send_string(sock, num_str);
            
            for (int i = 0; i < num_users; i++) {
                if (users[i].connected) {
                    send_string(sock, users[i].username);
                    send_string(sock, users[i].ip);
                    sprintf(num_str, "%d", users[i].port);
                    send_string(sock, num_str);
                }
            }
        }
        pthread_mutex_unlock(&users_lock);
    }
    else if (strcmp(operation, "LIST CONTENT") == 0) {
        char *target_username = read_string(sock);
        if (!target_username) {
            close(sock);
            return NULL;
        }

        pthread_mutex_lock(&users_lock);
        int user_index = find_user(username);
        int target_index = find_user(target_username);
        
        if (user_index < 0) {
            char response = RC_ERROR;
            send(sock, &response, 1, 0);
        }
        else if (!users[user_index].connected) {
            char response = RC_USER_NOT_CONNECTED;
            send(sock, &response, 1, 0);
        }
        else if (target_index < 0) {
            char response = RC_REMOTE_USER_NOT_EXISTS;
            send(sock, &response, 1, 0);
        }
        else {
            char response = RC_OK;
            send(sock, &response, 1, 0);
            
            pthread_mutex_lock(&users[target_index].lock);
            
            char num_str[16];
            sprintf(num_str, "%d", users[target_index].num_files);
            send_string(sock, num_str);
            
            for (int i = 0; i < users[target_index].num_files; i++) {
                send_string(sock, users[target_index].files[i].filename);
            }
            
            pthread_mutex_unlock(&users[target_index].lock);
        }
        pthread_mutex_unlock(&users_lock);
        free(target_username);
    }

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

int send_string(int socket, const char *str) {
    return send(socket, str, strlen(str) + 1, 0);
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
        free(&users[index].files[i]);
    }
    free(users[index].files);

    for (int i = index; i < num_users - 1; i++) {
        users[i] = users[i + 1];
    }
    num_users--;
    users = realloc(users, num_users * sizeof(user_info));
}

void add_file(int user_index, const char *filename, const char *description) {
    user_info *user = &users[user_index];
    user->files = realloc(user->files, (user->num_files + 1) * sizeof(file_info));
    strcpy(user->files[user->num_files].filename, filename);
    strcpy(user->files[user->num_files].description, description);
    user->num_files++;
}

void remove_file(int user_index, const char *filename) {
    user_info *user = &users[user_index];
    for (int i = 0; i < user->num_files; i++) {
        if (strcmp(user->files[i].filename, filename) == 0) {
            for (int j = i; j < user->num_files - 1; j++) {
                user->files[j] = user->files[j + 1];
            }
            user->num_files--;
            user->files = realloc(user->files, user->num_files * sizeof(file_info));
            break;
        }
    }
}
