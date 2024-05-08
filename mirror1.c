//
// Created by Nayeem Mehedi on 2024-04-02.
//
#define _XOPEN_SOURCE 500 // Required for nftw

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <ftw.h>
#include <pwd.h>
#include <time.h>
#include <signal.h>

#define SERVER_NAME "mirror1"

#define IP "127.0.0.1"

#define SERVER_PORT 10001
#define MIRROR_1_PORT 10002
#define MIRROR_2_PORT 10003

#define MAX_RESPONSE_LENGTH_TEXT 1048576
#define CHUNK_SIZE_TEXT 2048

#define CHUNK_SIZE_FILE 5120

#define MAX_CLIENTS 5
// limit for file paths
#define MAX_FILE_PATHS 10
#define MAX_PATH_LENGTH 1024

#define MAX_FILE_TYPES 3

int connection = 0;

// decrement the connection count when a child exits / client closes connection
void handle_child_exit(int signum) {
    if (signum == SIGCHLD) {
        printf("child process exited\n");
        connection--;

        printf("total connected clients: %d\n", connection);
    }
}

// check if the str1 has str2 in it
int strContains(char *str1, char *str2) {
    return (strstr(str1, str2) != NULL);
}

// check if a string ends with another sub string
int endswith(char* str, char* substr) {
    if (!str || !substr)
        return EXIT_FAILURE;

    size_t length_of_string    = strlen(str);
    size_t length_of_substring = strlen(substr);

    if (length_of_substring > length_of_string)
        return EXIT_FAILURE;

    char* endOfTheString = &str[length_of_string - length_of_substring];

    return strcmp(endOfTheString, substr) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

// remove a file by fileName
int remove_file(char *filename) {
    if (remove(filename) == 0) {
        printf("file %s deleted successfully.\n", filename);
        return 0;
    } else {
        perror("error: deleting file\n");
        return 1;
    }
}

char* get_directory() {
    // Get the home directory path
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;

    // TODO change directory
    // Append "/Downloads" to the home directory
    char *downloadDir = malloc(strlen(homedir) + strlen("/Downloads") + 1);
    strcpy(downloadDir, homedir);
    strcat(downloadDir, "/Downloads");

    return downloadDir;
}

/////////////// RESPONSE SENDING START //////////////////////////////////

int send_response(int client_socket, const char *response) {
    printf("preparing response : %s\n", response);
    int response_length = strlen(response);

    // send the total size of the text response to the client
    if (send(client_socket, &response_length, sizeof(int), 0) < 0) {
        perror("error: sending TAR file size\n");
        return EXIT_FAILURE;
    }

    // send response data chunk by chunk
    int sent_total = 0;
    while (sent_total < response_length) {
        int sending = (response_length - sent_total) < CHUNK_SIZE_TEXT ? (response_length - sent_total) : CHUNK_SIZE_TEXT;
        int sent = send(client_socket, response + sent_total, sending, 0);

        if (sent < 0) {
            perror("error: sending response data\n");
            return EXIT_FAILURE;
        }
        sent_total += sent;
    }

    printf("sent response\n");
    return EXIT_SUCCESS;
}

int send_tar_file(int client_socket, char *file_name) {
    // open the TAR file for reading
    FILE *tar_fp = fopen(file_name, "rb");
    if (tar_fp == NULL) {
        send_response(client_socket, "error : failed to open TAR file\n");
        return EXIT_FAILURE;
    }

    // determine the total size of the TAR file
    // ref : https://stackoverflow.com/questions/238603/how-can-i-get-a-files-size-in-c
    fseek(tar_fp, 0, SEEK_END);
    long tar_size = ftell(tar_fp);
    fseek(tar_fp, 0, SEEK_SET);

    // send the total size of the TAR file to the client
    if (send(client_socket, &tar_size, sizeof(long), 0) < 0) {
        perror("error: sending TAR file size\n");
        fclose(tar_fp);
        return EXIT_FAILURE;
    }

    // read and send the contents of the TAR file
    char buffer[CHUNK_SIZE_FILE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), tar_fp)) > 0) {
        if (send(client_socket, buffer, bytes_read, 0) < 0) {
            perror("error: sending TAR file chunk\n");
            break;
        }
//        printf("bytes sent : %ld\n", bytes_read);
    }

    printf("TAR file sent: %ld bytes\n", tar_size);

    // close the TAR file
    fclose(tar_fp);

    // remove the TAR file
    remove_file(file_name);

    return EXIT_SUCCESS;
}

/////////////// RESPONSE SENDING END ////////////////////////////////////

///////////////// cmd 3 START ////////////////////////

int FILE_FOUND_STATUS = 0;
char *fileNameOrExt;
char *textResponse;

int fileDetailsIfFileFound(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        if (strContains(fpath, fileNameOrExt)) {
            // file found, send its information to the client
            char response[2048];

            sprintf(response, "Filename: %s\nPath:%s\nSize: %ld bytes\nDate created: %sPermissions: %o\n",
                    fpath + ftwbuf->base, fpath, sb->st_size, ctime(&sb->st_ctime),
                    sb->st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));

            strcpy(textResponse, response);
            // stop traversal since file is found
            FILE_FOUND_STATUS = 1;
            return 1; // return non-zero to stop traversal
        }
    }

    return 0; // Continue traversal
}

// search for file using nftw and send response
int search_file(const char *filename, int client_socket) {
    fileNameOrExt = malloc(strlen(filename) * sizeof(char));
    strcpy(fileNameOrExt, filename);

    int response_length = MAX_RESPONSE_LENGTH_TEXT;
    textResponse = malloc(response_length * sizeof(char));

    // start the directory tree traversal from the given directory
    if (nftw(get_directory(), fileDetailsIfFileFound, 20, FTW_PHYS) == -1) {
        // Failed to traverse directory tree
        send_response(client_socket, "error: failed to traverse directory tree\n");
        free(fileNameOrExt);
        free(textResponse);
        return -1;
    }

    if (FILE_FOUND_STATUS == 0) {
        // File not found in the directory tree
        send_response(client_socket, "File not found\n");
        free(fileNameOrExt);
        free(textResponse);
    } else {
        send_response(client_socket, textResponse);
        FILE_FOUND_STATUS = 0; // changed for next execution
        free(fileNameOrExt);
        free(textResponse);
    }

    return 0;
}

///////////////// cmd 3 END ///////////////////////////

////////////// COMMON START //////////////////////////

// global variables to store file paths
char file_paths[MAX_FILE_PATHS][MAX_PATH_LENGTH];
int file_count = 0;

char* TAR_FILE_NAME = "temp.tar.gz";

// cmd 4
int size1 = -1;
int size2 = -1;

//cmd 5
char file_types[MAX_FILE_TYPES][MAX_PATH_LENGTH];
int num_file_types = 0;

//cmd 6
time_t before_date_time;

//cmd 7
time_t after_date_time;

void clear_file_paths() {
    for (int i = 0; i < file_count; i++) {
        memset(file_paths[i], '\0', MAX_PATH_LENGTH); // set each element to the null character
    }
    file_count = 0;

    for (int i = 0; i < num_file_types; i++) {
        memset(file_types[i], '\0', MAX_PATH_LENGTH);
    }
    num_file_types = 0;

    size1 = -1;
    size2 = -1;
    before_date_time = 0;
    after_date_time = 0;
}

///////////////// COMMON END //////////////////////////

///////////////// cmd 4 START ////////////////////////

// Define a callback function for nftw
int collectFilePaths(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    // check if it is a file
    if (typeflag == FTW_F) {
        // check if maximum number of file paths reached
        if (file_count < MAX_FILE_PATHS) {
            // check if the file size is between size1 and size2
            if(sb->st_size >= size1 && sb->st_size <= size2) {
                // append the file path to the array
                strcpy(file_paths[file_count], fpath);
                file_count++;
            }
        } else {
            // return non-zero to stop traversal
            return 1;
        }
    }
    // continue traversal
    return 0;
}

int create_file_list(int size11, int size22) {
    size1 = size11;
    size2 = size22;
    // Get the home directory path
    struct passwd *pw = getpwuid(getuid());

    // Start the directory tree traversal from the given directory
    if (nftw(get_directory(), collectFilePaths, 20, FTW_PHYS) == -1) {
        // Failed to traverse directory tree
        printf("error: failed to traverse directory tree\n");
        return EXIT_FAILURE;
    }

    if (file_count > 0) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

///////////////// cmd 4 END ///////////////////////////

///////////////// cmd 5 START /////////////////////////

int collectFilePathsByTypes(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    // check if it is a file
    if (typeflag == FTW_F) {
        // check if maximum number of file paths reached
        if (file_count < MAX_FILE_PATHS) {
            for (int i = 0; i < num_file_types; i++) {
                // check if file extension matches any of the provided extensions
                if (endswith(fpath, file_types[i]) == EXIT_SUCCESS) {
                    // append the file path to the array
                    strcpy(file_paths[file_count], fpath);
                    file_count++;
                    // return non-zero to stop traversal
                    break;
                }
            }
        } else {
            // return non-zero to stop traversal
            return 1;
        }
    }
    // continue traversal
    return 0;
}

int create_file_list_on_file_types(char *file_types_str) {
    const char *delimiters = " ";
    char *fType = strtok(file_types_str, delimiters);

    while (fType != NULL && num_file_types < MAX_FILE_TYPES) {
        strcpy(file_types[num_file_types], fType);
        num_file_types++;
        fType = strtok(NULL, delimiters);
    }

    // Start the directory tree traversal from the given directory
    if (nftw(get_directory(), collectFilePathsByTypes, 20, FTW_PHYS) == -1) {
        // failed to traverse directory tree
        printf("error: failed to traverse directory tree\n");
        return EXIT_FAILURE;
    }

    if (file_count > 0) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

///////////////// cmd 5 END ///////////////////////////

///////////////// cmd 6 & 7 START /////////////////////

int collectFilePathsBeforeDate(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    // check if it is a file
    if (typeflag == FTW_F) {
        // check if maximum number of file paths reached
        if (file_count < MAX_FILE_PATHS) {
            // check if the file time - before_date_time is 0 or less
            if (difftime(sb->st_mtime, before_date_time) <= 0) {
                // Append the file path to the array
                strcpy(file_paths[file_count], fpath);
                file_count++;
            }
        } else {
            // return non-zero to stop traversal
            return 1;
        }
    }
    // continue traversal
    return 0;
}

int collectFilePathsAfterDate(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    // check if it is a file
    if (typeflag == FTW_F) {
        // check if maximum number of file paths reached
        if (file_count < MAX_FILE_PATHS) {
            // check if the file time - after_date_time is 0 or greater
            if (difftime(sb->st_mtime, after_date_time) >= 0) {
                // Append the file path to the array
                strcpy(file_paths[file_count], fpath);
                file_count++;
            }
        } else {
            // return non-zero to stop traversal
            return 1;
        }
    }
    // continue traversal
    return 0;
}

// create file list based on provided date and type of comparison
// type -> 1 : before or equal, 2 : after or equal
int create_file_list_on_date(char *date_str, int type) {
    struct tm date_tm;
    memset(&date_tm, 0, sizeof(struct tm));
    if (strptime(date_str, "%Y-%m-%d", &date_tm) == NULL) {
        fprintf(stderr, "error : parsing date string: %s\n", date_str);
        return EXIT_FAILURE;
    }

    if(type == 1) {
        // convert before_date_tm to time_t
        before_date_time = mktime(&date_tm);
    } else {
        // convert before_date_tm to time_t
        after_date_time = mktime(&date_tm);
    }

    if(type == 1) {
        // start the directory tree traversal from the given directory
        if (nftw(get_directory(), collectFilePathsBeforeDate, 20, FTW_PHYS) == -1) {
            // failed to traverse directory tree
            printf("error: failed to traverse directory tree\n");
            return EXIT_FAILURE;
        }
    } else {
        // start the directory tree traversal from the given directory
        if (nftw(get_directory(), collectFilePathsAfterDate, 20, FTW_PHYS) == -1) {
            // failed to traverse directory tree
            printf("error: failed to traverse directory tree\n");
            return EXIT_FAILURE;
        }
    }

    if (file_count > 0) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

///////////////// cmd 6 & 7 END ////////////////////////

// create a tar file with the provided name
// with the files from -> file_paths
// number of files -> file_count
int create_tar_gz_v2(char *file_name) {
    //TODO remove DEBUG
    if (file_count > 0) {
        printf("file(s) found : %d\n", file_count);

//        for (int i = 0; i < file_count; i++) {
//            printf("%s\n", file_paths[i]);
//        }
    } else {
        printf("No file found\n");
    }

    char *cmd_base = "tar -czf ";

    // build the tar command with file paths
    // base + file_name + space + filepaths + space + ' + '
    int total_size = sizeof(cmd_base) + strlen(file_name) + 1 +file_count * MAX_PATH_LENGTH + file_count * 3;
    char *command = malloc(total_size * sizeof(char));

    strcpy(command, cmd_base);
    // add file name
    strcat(command, file_name);
    strcat(command, " ");

    // snprintf(command, sizeof(command), "tar -czf temp.tar.gz ");
    for (int i = 0; i < file_count; i++) {
        printf("path : %s\n", file_paths[i]);
        if(file_paths[i][0] != '\0') {
            strcat(command, "'");
            strcat(command, file_paths[i]);
            strcat(command, "'");
            strcat(command, " ");
        }
    }

    printf("command %s\n", command);

    // temporarily block SIGCHLD signals around system() call
    sigset_t mask, prev_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    // execute the tar command
    int ret = system(command);

    // restore previous signal mask
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    if (ret != 0) {
        free(command);
        fprintf(stderr, "error: failed to create the tar.gz archive\n");
        return EXIT_FAILURE;
    } else {
        free(command);
        printf("tar file created\n");
        return EXIT_SUCCESS;
    }
}

void crequest(int client_socket) {
    char buffer[1024] = {0};
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_socket, buffer, 1024);

        if (valread == 0 || strncmp(buffer, "quitc", 5) == EXIT_SUCCESS) { // cmd 8
            char* response = "EXIT";
            send_response(client_socket, response);

            close(client_socket);
            // forked child will exit
            exit(EXIT_SUCCESS);
        }

        // process command and send response to client
        if (strncmp(buffer, "w24fn ", 6) == EXIT_SUCCESS) { // cmd 3
            // w24fn TEST2_1_word_2.pdf

            // printf("received cmd : w24fn \n");
            // extract filename from the command
            char *filename = buffer + 6;
            // printf("received cmd filename : %s \n", filename);

            // Search for the file starting from the home directory
            search_file(filename, client_socket);
        } else if (strncmp(buffer, "w24fz ", 6) == EXIT_SUCCESS) { // cmd 4
            // w24fz 1227 69879

            clear_file_paths();

            // printf("received cmd : w24fz \n");
            // Extract size1 and size2 from the command
            char *sizes = buffer + 6;
            // printf("sizes: %s\n", sizes);

            off_t size1, size2;
            if (sscanf(sizes, "%ld %ld", &size1, &size2) != 2 || size1 < 0 || size2 < 0 || size1 > size2) {
                send_response(client_socket, "error : invalid size range\n");
                continue;
            }

            if(create_file_list(size1, size2) == EXIT_FAILURE) {
                send_response(client_socket, "No file found\n");
                continue;
            }

            if (create_tar_gz_v2(TAR_FILE_NAME) == EXIT_FAILURE) {
                send_response(client_socket, "error: failed to create the tar.gz archive.\n");
                continue;
            }

            if (send_tar_file(client_socket, TAR_FILE_NAME) == EXIT_FAILURE) {
                printf("error : tar file send operation failed\n");
                continue;
            }

            printf("tar file send operation successful\n");
        } else if (strncmp(buffer, "w24ft ", 6) == EXIT_SUCCESS) { // cmd 5
            // w24ft c txt pdf
            // w24ft pdf
            // w24ft c txt pdf pptx

            clear_file_paths();

            char *extension_str = buffer + 6;
            // printf("extentions: %s\n", extension_str); // Debug print

            // printf("extension string: %s\n", extension_str);

            if(create_file_list_on_file_types(extension_str) == EXIT_FAILURE) {
                send_response(client_socket, "No file found\n");
                continue;
            }

            if (create_tar_gz_v2(TAR_FILE_NAME) == EXIT_FAILURE) {
                send_response(client_socket, "error: failed to create the tar.gz archive\n");
                continue;
            }

            if (send_tar_file(client_socket, TAR_FILE_NAME) == EXIT_FAILURE) {
                printf("error: tar file send operation failed\n");
                continue;
            }

            printf("tar file send operation successful\n");
        } else if (strncmp(buffer, "w24fdb ", 6) == EXIT_SUCCESS) { // cmd 6
            // w24fdb 2024-03-03

            clear_file_paths();

            // printf("received cmd: w24fdb \n");
            // extract date
            char *date = buffer + 6;
            // printf("date: %s\n", date);

            if (create_file_list_on_date(date, 1) == EXIT_FAILURE) {
                send_response(client_socket, "No file found\n");
                continue;
            }

            if (create_tar_gz_v2(TAR_FILE_NAME) == EXIT_FAILURE) {
                send_response(client_socket, "error: failed to create the tar.gz archive.\n");
                continue;
            }

            if (send_tar_file(client_socket, TAR_FILE_NAME) == EXIT_FAILURE) {
                printf("error: tar file send operation failed\n");
                continue;
            }

            printf("tar file send operation successful\n");
        } else if (strncmp(buffer, "w24fda ", 6) == EXIT_SUCCESS) { // cmd 7
            // w24fda 2024-03-03

            clear_file_paths();

            // printf("received cmd : w24fda \n");
            // extract date
            char *date = buffer + 6;
            // printf("date: %s\n", date);

            if (create_file_list_on_date(date, 2) == EXIT_FAILURE) {
                send_response(client_socket, "No file found\n");
                continue;
            }

            if (create_tar_gz_v2(TAR_FILE_NAME) == EXIT_FAILURE) {
                send_response(client_socket, "error: failed to create the tar.gz archive.\n");
                continue;
            }

            if (send_tar_file(client_socket, TAR_FILE_NAME) == EXIT_FAILURE) {
                printf("error: tar file send operation failed\n");
                continue;
            }

            printf("tar file send operation successful\n");
        } else if (strncmp(buffer, "dirlist -a", 10) == EXIT_SUCCESS) {  // cmd 1
            // List directories alphabetically
            // printf("received cmd: dirlist -a\n");
            FILE *fp;

            // find ~ -maxdepth 1 -mindepth 1 -type d -printf '%f\n' | sort

            // list directories from home directory AND sort them by alphabetical order
            // list directories alphabetically from home directory
            char command[70] = "find ~ -maxdepth 1 -mindepth 1 -type d -printf '%f\n' | sort";

            fp = popen(command, "r");
            if(fp == NULL){
                send_response(client_socket, "error: failed to execute command\n");
                continue;
            }

            textResponse = malloc(MAX_RESPONSE_LENGTH_TEXT * sizeof(char));
            while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                strcat(textResponse, buffer);
            }
            pclose(fp);

            //TODO add error block
            send_response(client_socket, textResponse);
            free(textResponse);
        } else if (strncmp(buffer, "dirlist -t", 10) == EXIT_SUCCESS) { // cmd 2
            // list directories BY creation time
            FILE *fp;

            // list directories from home directory AND sort them by time
            // find ~ -maxdepth 1 -mindepth 1 -type d -printf '%T@ %p\n' | sort -n | awk '{print $2}' | xargs -I{} basename {}
            char command[120] = "find ~ -maxdepth 1 -mindepth 1 -type d -printf '%T@ %p\n' | sort -n | awk '{print $2}' | xargs -I{} basename {}";

            fp = popen(command, "r");
            if(fp == NULL){
                send_response(client_socket, "error: failed to execute command\n");
                continue;
            }

            textResponse = malloc(MAX_RESPONSE_LENGTH_TEXT * sizeof(char));
            while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                strcat(textResponse, buffer);
            }
            pclose(fp);

            //TODO add error block
            send_response(client_socket, textResponse);
            free(textResponse);
        } else {
            // invalid command
            send_response(client_socket, "error: invalid command\n");
        }
    }
}

//////////////////////////// MIRROR GAME START ///////////////////////////////////////

int sendServerReqType(int socket) {
    char* cmd = "SERVER";
    // printf("SENT value : %s\n", cmd);
    if (send(socket, cmd, strlen(cmd), 0) < 0) {
        perror("error: sending connection type failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int sendServerReqCOUNT(int socket) {
    char* cmd = "COUNT";
    // printf("SENT value : %s\n", cmd);
    if (send(socket, cmd, strlen(cmd), 0) < 0) {
        perror("error: sending connection type failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int send_connection_count(int client_socket, int number) {
    // send the total size of the text response to the client
    if (send(client_socket, &number, sizeof(int), 0) < 0) {
        perror("error: sending connection count\n");
        return EXIT_FAILURE;
    }

    printf("sent connection count\n");
    return EXIT_SUCCESS;
}

int receiveInteger(int socket) {
    int received_int;
    if (recv(socket, &received_int, sizeof(received_int), 0) < 0) {
        perror("error: receiving integer failed\n");

        return 0;
    }
    return received_int;
}

int getConnectionCount(char* ip, int server_port){
    int mirror_socket;
    struct sockaddr_in mirror_address;

    if ((mirror_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "error: - socket creation failed port : %d\n", server_port);

        return 0;
    }

    mirror_address.sin_family = AF_INET;
    mirror_address.sin_port = htons(server_port);
    if (inet_pton(AF_INET, ip, &mirror_address.sin_addr) <= 0) {
        fprintf(stderr, "error: - invalid address : %s port : %d\n", ip, server_port);

        return 0;
    }

    if (connect(mirror_socket, (struct sockaddr *) &mirror_address, sizeof(mirror_address)) < 0) {
        fprintf(stderr, "error: - connection failed port : %d\n", server_port);

        return 0;
    }

    // printf("connected to MIRROR\n");

    // send connection type
    if (sendServerReqType(mirror_socket) == EXIT_FAILURE) {
        fprintf(stderr, "error: - connection type negotiation failed port : %d\n", server_port);

        close(mirror_socket);
        return 0;
    }

    // sleep for 500000 microseconds (500 milliseconds)
    usleep(500000);

    // printf("sent CONN TYPE\n");

    // send COUNT request
    if (sendServerReqCOUNT(mirror_socket) == EXIT_FAILURE) {
        fprintf(stderr, "error: - send count request failed port : %d\n", server_port);

        close(mirror_socket);
        return 0;
    }

    // printf("send CMD COUNT\n");

    int con_count = receiveInteger(mirror_socket);

    // printf("received COUNT\n");

    close(mirror_socket);

    // printf("closed mirror CONN\n");

    return con_count;
}

// for handling server requests
// send COUNT
void srequest(int s_socket) {
    char buffer[1024] = {0};

    memset(buffer, 0, sizeof(buffer));
    int valread = read(s_socket, buffer, 1024);

    // printf("received some CMD char length: %d\n", valread);
    // printf("received some CMD : %s\n", buffer);

    if (strncmp(buffer, "COUNT", 5) == EXIT_SUCCESS) { // cmd COUNT

        if(send_connection_count(s_socket, connection) == EXIT_FAILURE){
                perror("error: sending connection count\n");

                close(s_socket);
        }

        close(s_socket);
    }
}

//////////////////////////// MIRROR GAME END ///////////////////////////////////////


int main() {
    // signal handler to handle child process exit
    struct sigaction sa;
    sa.sa_handler = handle_child_exit;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("error : sigaction");

        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server;
    int server_fd, opt = 1;

    int addrlen = sizeof(server);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("error: running server!");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("error: setsockopt");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(MIRROR_1_PORT);

    if (bind(server_fd, (struct sockaddr *) &server, sizeof(server)) < 0) {
        perror("error: port bind failed\n");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("error: listen call failure\n");
        exit(EXIT_FAILURE);
    }

    printf("%s is running. listening on port %d\n", SERVER_NAME, MIRROR_1_PORT);

    while (1) {
        int client_socket = 1;
    
        if ((client_socket = accept(server_fd, (struct sockaddr *) &server, (socklen_t *) &addrlen)) < 0) {
            perror("error: accept call failure");
            exit(EXIT_FAILURE);
        }
        
        // ready to accept connections
        char connection_type[10];
        // receive connection type from connected client
        if (recv(client_socket, connection_type, sizeof(connection_type), 0) <= 0) {
            perror("error: receiving connection type failed");

            close(client_socket);
            continue;
        }

        // some client/server tried to connect and sent the TYPE

        // Check the received connection type
        if (strncmp(connection_type, "SERVER", 6) == EXIT_SUCCESS) {
            printf("connection type: SERVER\n");

            srequest(client_socket);
            continue;

        } else if (strncmp(connection_type, "CLIENT", 5) == EXIT_SUCCESS) {
            printf("connection type: CLIENT\n");

            // get other mirrors' connection count
            int serverCon = getConnectionCount(IP, SERVER_PORT);
            int mirror2 = getConnectionCount(IP, MIRROR_2_PORT);

            int goToServer = (connection >= 3 && serverCon >= 3 && mirror2 >= 3) && ((connection + serverCon + mirror2 - 9) % 3) + 1 == 1 ? 1 : 0;
            int goToMirror1 = (connection >= 3 && serverCon >= 3 && mirror2 >= 3) && ((connection + serverCon + mirror2 - 9) % 3) + 1 == 2 ? 1 : 0;
            int goToMirror2 = (connection >= 3 && serverCon >= 3 && mirror2 >= 3) && ((connection + serverCon + mirror2 - 9) % 3) + 1 == 3 ? 1 : 0;

            if(serverCon < 3 || goToServer == 1) {

                char response[100];
                sprintf(response, "please connect to server at ip: %s port: %d\n", IP, SERVER_PORT);

                send_response(client_socket, response);
                close(client_socket);
                
            } else if(connection < 3 || goToMirror1 == 1) {
                // increase number of connection
                connection++;
                char *client = inet_ntoa(server.sin_addr);
                printf("client connected: %s\n", client);
                printf("total connected clients: %d\n", connection);

                send_response(client_socket, "CONTINUE");

                if (!fork()) {
                    close(server_fd);
                    crequest(client_socket);
                    printf("client disconnected : %s\n", client);
                }
                close(client_socket);
            }  else if(mirror2 < 3 || goToMirror2 == 1) {

                char response[100];
                sprintf(response, "please connect to mirror2 at ip: %s port: %d\n", IP, MIRROR_2_PORT);

                send_response(client_socket, response);
                close(client_socket);
            }
        } else {
            printf("invalid connection type received\n");

            close(client_socket);
            continue;
        }

    }

    return EXIT_SUCCESS;
}
