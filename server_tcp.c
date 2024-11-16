#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#define TCP_PORT            5000        
#define MAX_BACKLOG         10          
#define BUFFER_SIZE         1024 
#define SOCKET_ERROR        -1
#define BIND_ERROR          -2
#define LISTEN_ERROR        -3
#define IP_SERVER_ERROR     -4
#define ACCEPT_ERROR        -5
#define MAX_KEY_LEN         256    
#define MAX_VALUE_LEN       768  
#define MAX_COMMAND_LEN     16  

typedef enum {
    CMD_SET,
    CMD_GET,
    CMD_DEL,
    CMD_UNKNOWN
} command_type_t;

void handle_client(int client);
int set_tcp_server_socket();
int accept_client_socket(int server_socket);
void parse_message(int client_socket, const char *command, const char *key, const char *value);
command_type_t get_command_type(const char *command);

/**
 * @brief Configures and returns the TCP server socket.
 * 
 * @return int Server socket, or an error code in case of failure.
 */
int set_tcp_server_socket(){    
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0){
        perror("Error creating socket");
        return SOCKET_ERROR;
    }    
    struct sockaddr_in serveraddr = {0};
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(TCP_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &(serveraddr.sin_addr)) <= 0) {
        fprintf(stderr, "ERROR: Invalid server IP address\n");
        return IP_SERVER_ERROR;
    }
    if (bind(server_socket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("Error in bind");
        return BIND_ERROR;
    }
    if (listen(server_socket, MAX_BACKLOG) == -1) {
        perror("Error in listen");
        close(server_socket);
        return LISTEN_ERROR;
    }
    printf("Server is listening on port %d...\n", TCP_PORT);
    return server_socket;
}

/**
 * @brief Accepts an incoming connection from a client.
 * 
 * @param server_socket Server socket.
 * @return int Client socket, or an error code in case of failure.
 */
int accept_client_socket(int server_socket){
    struct sockaddr_in clientaddr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    printf("Server: waiting for a connection...\n");
    int client_socket = accept(server_socket, (struct sockaddr *)&clientaddr, &addr_len);
    if (client_socket == -1) {
        perror("Error in accept");
        return ACCEPT_ERROR;
    }
    char ip_client[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(clientaddr.sin_addr), ip_client, sizeof(ip_client));
    printf("Server: connection established with %s:%d\n", ip_client, ntohs(clientaddr.sin_port));
    return client_socket;
}

/**
 * @brief Determines the type of command based on its name.
 * 
 * @param command The command as a string.
 * @return command_type_t The type of the command.
 */
command_type_t get_command_type(const char *command) {
    if (strcmp(command, "SET") == 0) {
        return CMD_SET;
    } else if (strcmp(command, "GET") == 0) {
        return CMD_GET;
    } else if (strcmp(command, "DEL") == 0) {
        return CMD_DEL;
    } else {
        return CMD_UNKNOWN;
    }
}

/**
 * @brief Parses and executes the command received from the client.
 * 
 * @param client_socket Client's socket.
 * @param command Command to execute.
 * @param key Command key.
 * @param value Command value (if applicable).
 */
void parse_message(int client_socket, const char *command, const char *key, const char *value) {
    switch (get_command_type(command)) {
        case CMD_SET: {
            int fd = open(key, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("Error creating file");
                write(client_socket, "ERROR\n", 6);
            } else {
                if (write(fd, value, strlen(value)) < 0) {
                    perror("Error writing to file");
                    write(client_socket, "ERROR\n", 6);
                } else {
                    write(client_socket, "OK\n", 3);
                }
                close(fd);
            }
            break;
        }
        case CMD_GET: {
            int fd = open(key, O_RDONLY);
            if (fd < 0) {
                write(client_socket, "NOTFOUND\n", 9);
            } else {
                char file_content[MAX_VALUE_LEN] = {0};
                ssize_t bytes_read = read(fd, file_content, sizeof(file_content) - 1);
                if (bytes_read < 0) {
                    perror("Error reading from file");
                    write(client_socket, "ERROR\n", 6);
                } else {
                    file_content[bytes_read] = '\0'; 
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "OK\n%s\n", file_content);
                    write(client_socket, response, strlen(response));
                }
                close(fd);
            }
            break;
        }
        case CMD_DEL: {
            if (remove(key) == 0) {
                write(client_socket, "OK\n", 3);
            } else {
                write(client_socket, "OK\n", 3); 
            }
            break;
        }
        case CMD_UNKNOWN:
        default:
            write(client_socket, "ERROR\n", 6);
            break;
    }
}

/**
 * @brief Handles communication with a connected client.
 * 
 * @param client_socket Socket of the connected client.
 */
void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE] = {0};
    int bytes_received = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_received <= 0) {
        perror("Error in read");
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    buffer[bytes_received] = '\0';
    printf("Received command: %s\n", buffer);
    char command[MAX_COMMAND_LEN] = {0};
    char key[MAX_KEY_LEN] = {0};
    char value[MAX_VALUE_LEN] = {0};
    int parsed_args = sscanf(buffer, "%15s %255s %767[^\n]", command, key, value);
    if (parsed_args >= 2) {
        parse_message(client_socket, command, key, (parsed_args == 3) ? value : "");
    } else {
        write(client_socket, "ERROR: Incorrect number of arguments\n", 36);
    }
    close(client_socket); 
}

/**
 * @brief Main function for the TCP server.
 * 
 * @return int Exit code.
 */
int main(void) {
    int server_socket = set_tcp_server_socket(TCP_PORT);
    if (server_socket < 0 ) return EXIT_FAILURE;
    while (1){
        int client_socket = accept_client_socket(server_socket);
        if (client_socket < 0){
            fprintf(stderr, "Error accepting connection.\n");
        } else {
        handle_client(client_socket);
        }
    }
    return EXIT_SUCCESS;
}
