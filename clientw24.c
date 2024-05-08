//
// Created by Nayeem Mehedi on 2024-04-02
//
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <ctype.h>

#define CHUNK_SIZE_TEXT 2048
#define CHUNK_SIZE_FILE 5120

const char *FILE_NAME = "temp.tar.gz";

// check if the str1 has str2 in it
int strContains(const char *str1, const char *str2) {
    return (strstr(str1, str2) != NULL);
}

// list of allowed commands
const char *allowed_commands[] = {"quitc", "dirlist -a", "dirlist -t", "w24fn", "w24fz", "w24ft", "w24fdb", "w24fda"};

// func to validate command
int command_validator(const char *command) {
    // check if the entered command matches one of the allowed commands
    for (int i = 0; i < sizeof(allowed_commands) / sizeof(allowed_commands[0]); i++) {
        if (strContains(command, allowed_commands[i])) {
            return EXIT_SUCCESS;
        }
    }
    return EXIT_FAILURE;
}

int receive_tar_file(int client_socket) {

    long tar_size;
    if (recv(client_socket, &tar_size, sizeof(long), 0) != sizeof(long)) {
        // tar size is not long, it can be an error message
        char error_msg[100];
        if (recv(client_socket, error_msg, sizeof(error_msg), 0) <= 0) {
            perror("error: receiving error message\n");
            return EXIT_FAILURE;
        }

        printf("%s\n", error_msg);
        return EXIT_FAILURE;
    } else {
        // tar size is long, it is the TAR file size
        printf("received TAR file size from server: %ld\n", tar_size);
    }

    // open a new TAR file for writing
    FILE *tar_fp = fopen(FILE_NAME, "wb");
    if (tar_fp == NULL) {
        perror("error: opening TAR file for writing\n");
        return EXIT_FAILURE;
    }

    // receive and write the contents of the TAR file
    char buffer[CHUNK_SIZE_FILE];
    size_t total_received = 0;
    while (total_received < tar_size) {
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                perror("error: server closed connection unexpectedly\n");
                fclose(tar_fp);
                return EXIT_FAILURE;
            } else {
                perror("error: receiving TAR file chunk\n");
                fclose(tar_fp);
                return EXIT_FAILURE;
            }
            break;
        }
        fwrite(buffer, 1, bytes_received, tar_fp);
        total_received += bytes_received;
//        printf("chunk received : %ld\n", bytes_received);
    }

    // close the TAR file
    fclose(tar_fp);
    printf("TAR file received of : %ld\n", total_received);

    return EXIT_SUCCESS;
}

int is_valid_date(char *date_str) {
    struct tm date_tm;
    // Parse date string into struct tm
    memset(&date_tm, 0, sizeof(struct tm));
    if (strptime(date_str, "%Y-%m-%d", &date_tm) == NULL) {
        fprintf(stderr, "error parsing date string: %s\n", date_str);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int sendConnectionType(int socket, const char *type) {
    if (send(socket, type, strlen(type), 0) < 0) {
        perror("error: sending connection type failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int receive_response_text_with_check(int socket, char *checkString) {

    // receive the total size of the response
    int response_len;
    if (recv(socket, &response_len, sizeof(int), 0) < 0) {
        perror("error: receiving response length\n");
        return EXIT_FAILURE;
    }

    // allocate memory to store the response + 1 null-terminating char
    char *response = (char *) malloc(response_len + 1);
    if (response == NULL) {
        perror("error: memory allocation failed\n");
        return EXIT_FAILURE;
    }

    strcpy(response, "");

    // receive response data chunk by chunk
    char buffer[CHUNK_SIZE_FILE];
    int received = 0;

    while (received < response_len) {
        int receiving = (response_len - received) < CHUNK_SIZE_TEXT ? (response_len - received) : CHUNK_SIZE_TEXT;
        int chunk_size = recv(socket, buffer + received, receiving, 0);
        if (chunk_size <= 0) {
            perror("error : reading from server\n");
            break;
        }

        strcat(response, buffer);
        received += chunk_size;
    }
    // add null-terminate char at the end of the response string
    response[received] = '\0';

    // printf("response : %s\n", response);

    if (strncmp(response, checkString, 8) != EXIT_SUCCESS) {
        // print server response
        printf("%s\n", response);
        return EXIT_FAILURE;
    }

    // free allocated memory
    free(response);

    // omit the response string and continue to command sending
    return EXIT_SUCCESS;
}

int receive_response_print(int client_socket) {
    // receive response length from server
    int response_len;
    if (recv(client_socket, &response_len, sizeof(int), 0) != sizeof(int)) {
        perror("error: receiving response length\n");

        return EXIT_FAILURE;
    }

    char *response_text = malloc(response_len * sizeof(char));
    strcpy(response_text, "");

    // receive and print response from server
    char buffer[CHUNK_SIZE_FILE];
    int total_read = 0;
    while (total_read < response_len) {
        int chunk_size = recv(client_socket, buffer + total_read, response_len - total_read, 0);
        if (chunk_size <= 0) {
            if (chunk_size == 0) {
                perror("server closed connection unexpectedly\n");
            } else {
                perror("error: reading from server\n");
            }
            break;
        }
        strcat(response_text, buffer);
        total_read += chunk_size;
    }
    response_text[total_read] = '\0';
    printf("%s\n", response_text);

    return EXIT_SUCCESS;
}

int main() {
    int client_socket;
    struct sockaddr_in server_address;
    char command[1024];

    // TODO ip address taking can be enabled if needed
    char ip[16] = "127.0.0.1";
    int port;

//    printf("enter server IP address: ");
//    scanf("%s", ip);

    printf("enter server port: ");
    scanf("%d", &port);

    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "error: - socket creation failed port : %d\n", port);
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_address.sin_addr) <= 0) {
        fprintf(stderr, "error: invalid address : %s port : %d\n", ip, port);
        exit(EXIT_FAILURE);
    }

    if (connect(client_socket, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        fprintf(stderr, "error: - connection failed port : %d\n", port);
        exit(EXIT_FAILURE);
    }

    if (sendConnectionType(client_socket, "CLIENT") == EXIT_FAILURE) {
        fprintf(stderr, "error: - connection type negotiation failed port : %d\n", port);

        close(client_socket);
        exit(EXIT_FAILURE);
    }

    if (receive_response_text_with_check(client_socket, "CONTINUE") == EXIT_FAILURE) {

        close(client_socket);
        exit(EXIT_FAILURE);
    }

    printf("Connection successful to %s:%d\n", ip, port);

    // consume newline character
    getchar();

    while (1) {
        printf("enter command: ");
        fgets(command, 1024, stdin);

        // remove trailing newline
        command[strcspn(command, "\n")] = 0;
//        printf("sending command: %s\n", command); // Debug print

        // validating the command
        if (command_validator(command) == EXIT_FAILURE) {
            printf("error: Invalid command : %s\n", command);
            continue;
        }

        if (strncmp(command, "quitc", 5) == EXIT_SUCCESS) { // cmd 8
            // send command to server
            if (send(client_socket, command, strlen(command), 0) < 0) {
                perror("error: command sending failed\n");
                continue;
            }

            // receive response length from server
            int response_len;
            if (recv(client_socket, &response_len, sizeof(int), 0) != sizeof(int)) {
                perror("error: receiving response length\n");
                continue;
            }

            // receive and print response from server
            char buffer[CHUNK_SIZE_FILE];
            int total_read = 0;
            while (total_read < response_len) {
                int chunk_size = recv(client_socket, buffer + total_read, response_len - total_read, 0);
                if (chunk_size <= 0) {
                    if (chunk_size == 0) {
                        perror("Server closed connection unexpectedly\n");
                    } else {
                        perror("Error reading from server\n");
                    }
                    break;
                }
                total_read += chunk_size;
            }
            buffer[total_read] = '\0';

            if (strncmp(buffer, "EXIT", 4) == EXIT_SUCCESS) {
                perror("server connection closed\n");
                close(client_socket);
                exit(EXIT_SUCCESS);
            }
        }

        if (strncmp(command, "w24fn ", 6) == EXIT_SUCCESS) { // cmd 3
//            printf("sending command: %s\n", command); // Debug print

            //TODO  add parameter validation
            // Send command to server
            if (send(client_socket, command, strlen(command), 0) < 0) {
                perror("error: command sending failed\n");
                continue;
            }
//            printf("command sent\n"); // Debug print
            if (receive_response_print(client_socket) == EXIT_FAILURE) {

                continue;
            }

            // printf("received response: %s\n", buffer); // Debug print
        } else if (strncmp(command, "w24fz ", 6) == EXIT_SUCCESS) { // cmd 4

            char *sizes = command + 6;
            off_t size1, size2;
            if (sscanf(sizes, "%ld %ld", &size1, &size2) != 2 || size1 < 0 || size2 < 0 || size1 > size2) {
                perror("error : invalid size range\n");
                continue;
            }

            // Send command to server
            if (send(client_socket, command, strlen(command), 0) < 0) {
                perror("error: command sending failed\n");
                continue;
            }

            if (receive_tar_file(client_socket) == EXIT_SUCCESS) {
                printf("TAR received successfully. file : %s\n", FILE_NAME);
            }
        } else if (strncmp(command, "w24ft ", 6) == EXIT_SUCCESS) { // cmd 5
//            printf("sending command: %s\n", command); // Debug print

            char *t_cmd;
            t_cmd = malloc(strlen(command) * sizeof(char));
            strcpy(t_cmd, command);

            int MAX_EXTENSIONS = 3;

            char *extensions_str = t_cmd + 6;

            const char *delimiters = " ";
            char *extensions = strtok(extensions_str, delimiters);
            int num_extensions = 0;

            while (extensions != NULL) {
                num_extensions++;
                extensions = strtok(NULL, delimiters);
            }
            free(t_cmd);

            if (num_extensions < 1 || num_extensions > MAX_EXTENSIONS) {
                perror("error : invalid number of file types as arg\n");
                continue;
            }

            printf("command: %s\n", command);
            // Send command to server
            if (send(client_socket, command, strlen(command), 0) < 0) {
                perror("error: command sending failed\n");
                continue;
            }

            if (receive_tar_file(client_socket) == EXIT_SUCCESS) {
                printf("TAR received successfully. file : %s\n", FILE_NAME);
            }
        } else if (strncmp(command, "w24fdb ", 6) == 0 || strncmp(command, "w24fda ", 6) == 0) { // cmd 6 + 7
//            printf("sending command: %s\n", command); // Debug print

            char *date = command + 6;

            if (is_valid_date(date) == 1) {
                printf("error: Invalid date format\n");
                printf("please enter the date as YYYY-mm-dd\n");
                continue;
            } else {
                // Send command to server
                if (send(client_socket, command, strlen(command), 0) < 0) {
                    perror("error: command sending failed\n");
                    continue;
                }
//                printf("Command sent\n"); // Debug print

                if (receive_tar_file(client_socket) == EXIT_SUCCESS) {
                    printf("TAR received successfully. file : %s\n", FILE_NAME);
                }
            }

        } else {
            // cmd 1 + 2
            // Send command to server
            if (send(client_socket, command, strlen(command), 0) < 0) {
                perror("error: command sending failed\n");
                continue;
            }
//            printf("Command sent\n"); // Debug print
            if (receive_response_print(client_socket) == EXIT_FAILURE) {

                continue;
            }

        }
    }

    printf("Disconnected from server\n");

    close(client_socket);
    return 0;
}
