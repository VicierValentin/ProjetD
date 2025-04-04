#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8081        // Port to listen on
#define BUFSIZE 1024     // Size of the receive buffer

FILE *fp = NULL;

int main() {
    int sockfd, clientfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFSIZE] = {0};
    ssize_t bytes_received;
    int image_counter = 1; // Counter for creating unique filenames
    char file_path[256]; // To hold the file path
    char state = 0;

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Allow immediate reuse of the port if the server is restarted
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Set up the server address (listening on all interfaces)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    // Bind the socket to the specified port and address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming TCP connections
    if (listen(sockfd, 5) < 0) {
        perror("Error listening on socket");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server waiting for connection on port %d...\n", PORT);

    // Accept the first client connection (this example handles one connection at a time)
    clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
    if (clientfd < 0) {
        perror("Error accepting connection");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Client connected. Waiting to receive photo data...\n");

    while(1){
        // Receive data from the client until the connection closes
        if (recv(clientfd, buffer, BUFSIZE, 0) > 0) {
            switch (state){
                case 0:
                    if(strncmp(buffer,"START", sizeof("START")) == 0){
                        state = 1;
                        // Generate the file path for the current image
                        snprintf(file_path, sizeof(file_path), "./images/image_%d.jpeg", image_counter);
                        printf("Photo incomming...\n");
                        // Open the file for writing
                        memset(buffer, 0, BUFSIZE);
                        fp = fopen(file_path, "wb");
                        if (fp == NULL) {
                            perror("Error opening file");
                            state = 0;
                            break;
                        }
                    }
                break;

                case 1:
                    if(strncmp(buffer, "STOP!", sizeof("STOP!")) == 0){
                        state = 2;
                        break;
                    }
                    if (fwrite(buffer, 1, BUFSIZE, fp) <0 ) {
                        perror("Error writing to file");
                        fclose(fp);
                        state = 0;
                        break;
                    }
                break;

                case 2:
                    memset(buffer, 0, BUFSIZE);
                    fclose(fp); // Close the file after writing
                    printf("Image saved as: %s\n", file_path);
                    image_counter++; // Increment the counter for the next image
                    printf("Photo received successfully.\n");
                    state = 0;
                    send(clientfd, "ACK", sizeof("ACK"), 0);
                break;
            }
        }
    }
    // Clean up: close file and sockets
    close(clientfd);
    close(sockfd);

    return 0;
}
