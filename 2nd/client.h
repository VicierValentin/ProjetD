#ifndef CLIENT_H  // Include guard to prevent multiple inclusions
#define CLIENT_H

// Function prototype for initializing the client
// Returns: 0 on success, a negative error code on failure
int init_client();
void send_and_delete_photo(const char *file_path);
#endif // CLIENT_H
