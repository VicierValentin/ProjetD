#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"  // Server's IP address (change as needed)
#define SERVER_PORT 8081       // Server's port number
#define BUFFER_SIZE 1024       // Buffer size for data chunks
#define IMAGE_DIR "./images/"   // Directory containing the photos

char connected = 0;

char file_prefix[] = "image_";
char file_suffix[] = ".jpeg";

char jpegPath[30] = {0};

int sock, serverfd = 0;

char *check_image_files(const char *directory) {
    DIR *dir;
    struct dirent *entry;

    // Open the directory
    dir = opendir(directory);
    if (dir == NULL) {
        perror("Error opening directory");
        return NULL; // Return NULL if the directory can't be opened
    }

    // Iterate through directory entries
    while ((entry = readdir(dir)) != NULL) {
        // Check if the file name starts with "image_" and ends with ".jpeg"
        if (strncmp(entry->d_name, file_prefix, strlen(file_prefix)) == 0 &&
            strstr(entry->d_name, file_suffix) != NULL) {
            // Found a matching file
            char *matching_file = malloc(strlen(entry->d_name) + 1);
            if (matching_file == NULL) {
                perror("Error allocating memory");
                closedir(dir);
                return NULL;
            }
            strcpy(matching_file, entry->d_name); // Copy the file name to return
            closedir(dir);
            return matching_file; // Return the file name
        }
    }

    // Close the directory
    closedir(dir);
    return NULL; // Return NULL if no matching files are found
}

void send_and_delete_photo(const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    printf("Sending photo: %s\n", file_path);

    const char* header = "START";
    if (send(sock, header, sizeof("START"), 0) < 0) {
        perror("Error sending header");
        fclose(file);
        //handle server deconnection
        connected = 0;
        return;
    }
    
    // Read from the file and send data in chunks
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            perror("Error sending data");
            fclose(file);
            //handle server deconnection
            connected = 0;
            return;
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
    
    const char* footer = "STOP!";
    if (send(sock, footer, sizeof("STOP!"), 0) < 0) {
        perror("Error sending footer");
        fclose(file);
        //handle server deconnection
        connected = 0;
        return;
    }

    fclose(file);

    char recBuf[10] = {0};
    printf("Waiting ack\n");
    char stop = 0;
    while(!stop){
        bytes_read = recv(serverfd, recBuf, sizeof(recBuf), 0);
        if(strncmp(recBuf, "ACK", sizeof("ACK")) == 0){
            stop = 1;
        }
    }
    printf("ACK Receive\n");
    // Delete the file after sending it
    if (remove(file_path) == 0) {
        printf("Photo deleted successfully: %s\n", file_path);
    } else {
        perror("Error deleting photo");
    }
}

void init_client() {
    
    struct sockaddr_in server_addr;

    // Open the directory containing the images
    DIR *dir = opendir(IMAGE_DIR);
    if (dir == NULL) {
        perror("Error opening image directory");
        exit(EXIT_FAILURE);
    }

    // Create a TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        closedir(dir);
        exit(EXIT_FAILURE);
    }

    // Set up the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        closedir(dir);
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Keep trying to connect to the server until successful
    while ((serverfd = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr))) < 0) {
        perror("Connection to server failed");
        printf("Retrying in %d seconds...\n", 5);
        sleep(5); // Wait before retrying
    }
    connected = 1;

    printf("Connected to the server.\n");

    /*
    while(1){
        usleep(5000);
        if(!connected){
            while (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connection to server failed");
                printf("Retrying in %d seconds...\n", 5);
                sleep(5); // Wait before retrying
            }
            connected = 1;
        }
    }
    */


    return 0;
}

int old_init_client() {
    pthread_t client_thread;
    int result;
    connected = 0;

    printf("Initializing client...\n");

    // Create the thread
    //result = pthread_create(&client_thread, NULL, client_thread_function, NULL);
    if (result != 0) {
        fprintf(stderr, "Error creating thread: %s\n", strerror(result));
        return -1;
    }

    printf("Client initialized and thread created successfully.\n");
    return 0;
}
