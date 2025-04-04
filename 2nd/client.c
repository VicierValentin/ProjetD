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

int sock = 0;

int init_client() {
    
    struct sockaddr_in server_addr;

    // Create a TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
    }

    // Set up the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Keep trying to connect to the server until successful
    while ((connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr))) < 0) {
        perror("Connection to server failed");
        printf("Retrying in %d seconds...\n", 5);
        sleep(5); // Wait before retrying
    }
    connected = 1;

    printf("Connected to the server.\n");

    return 0;
}

void send_and_delete_photo(const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    if(!connected)init_client();

    printf("Sending photo: %s\n", file_path);

    const char* header = "START";
    if (send(sock, header, sizeof("START"), 0) < 0) {
        perror("Error sending header");
        fclose(file);
        //handle server deconnection
        connected = 0;
        return;
    }
    printf("Start send\n");
    
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
    printf("Sending STOP!");
    const char* footer = "STOP!";
    if (send(sock, footer, sizeof("STOP!"), 0) < 0) {
        perror("Error sending footer");
        fclose(file);
        //handle server deconnection
        connected = 0;
        return;
    }

    shutdown(sock, SHUT_WR); // Signal no more data to send

    char recBuf[10] = {0};
    printf("Waiting ack\n");
    char stop = 0;
    while(!stop){
        bytes_read = recv(sock, recBuf, sizeof(recBuf), 0);
        if(strncmp(recBuf, "ACK", sizeof("ACK")) == 0){
            stop = 1;
            printf("ACK Received\n");
        }
    }
    
    fclose(file);
    // Delete the file after sending it
    if (remove(file_path) == 0) {
        printf("Photo deleted successfully: %s\n", file_path);
    } else {
        perror("Error deleting photo");
    }
    close(sock);
    connected = 0;
}