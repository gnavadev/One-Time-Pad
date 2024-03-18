#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define BUFFER_SIZE 1000

void error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Client error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

void setupAddressStruct(struct sockaddr_in *address, int portNumber, char *hostname)
{
    if (!address || !hostname)
    {
        error("Invalid input to setupAddressStruct");
        return;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    char portStr[6]; // Enough to store max port number 65535 and null terminator
    snprintf(portStr, sizeof(portStr), "%d", portNumber);

    if (getaddrinfo(hostname, portStr, &hints, &res) != 0)
    {
        error("Failed to get host info");
        return;
    }

    if (res->ai_family == AF_INET)
    { // Sanity check
        memcpy(address, res->ai_addr, sizeof(struct sockaddr_in));
    }
    else
    {
        error("Host found but not IPv4");
    }

    freeaddrinfo(res); // Free the linked list allocated by getaddrinfo
}

void sendData(int sock, const char *data)
{
    int len = strlen(data);
    if (send(sock, &len, sizeof(len), 0) < 0)
        error("Unable to write to socket");
    if (send(sock, data, len, 0) < 0)
        error("Unable to write to socket");
}

char *receive(int sock)
{
    // Get length of data
    int len;
    if (recv(sock, &len, sizeof(len), 0) < 0)
    {
        error(1, "Unable to read from socket");
    }

    // Allocate memory for the result
    char *result = malloc(len + 1);
    if (!result)
    {
        error(1, "Unable to allocate memory");
    }

    // Receive data in chunks based on BUFFER_SIZE
    int totalReceived = 0, charsRead;
    while (totalReceived < len)
    {
        int chunkSize = len - totalReceived > BUFFER_SIZE ? BUFFER_SIZE : len - totalReceived;
        charsRead = recv(sock, result + totalReceived, chunkSize, 0);
        if (charsRead < 0)
        {
            free(result);
            error(1, "Unable to read from socket");
        }
        totalReceived += charsRead;
    }

    // Null-terminate the received string
    result[len] = '\0';
    return result;
}

void validate(int sock)
{
    char clientMsg[4] = "enc", serverMsg[4] = {0};

    if (send(sock, clientMsg, sizeof(clientMsg), 0) < 0)
    {
        error("Unable to write to socket");
        return;
    }

    int totalReceived = 0;
    while (totalReceived < sizeof(serverMsg))
    {
        int received = recv(sock, serverMsg + totalReceived, sizeof(serverMsg) - totalReceived, 0);
        if (received < 0)
        {
            error("Unable to read from socket");
            return;
        }
        else if (received == 0)
        {
            error("Connection closed by server");
            return;
        }
        totalReceived += received;
    }

    if (strncmp(clientMsg, serverMsg, sizeof(clientMsg)) != 0)
    {
        close(sock);
        error("Server validation failed");
    }
}

char *stringFromFile(char *path)
{
    // Open file at path
    FILE *file = fopen(path, "r");
    if (!file)
        error(0, "Unable to open file: %s", path);

    // Seek file to get sieze (len)
    fseek(file, 0, SEEK_END);
    size_t len = ftell(file) - 1;
    fseek(file, 0, SEEK_SET);

    // Create buffer, error of unable
    char *buffer = (char *)malloc(len + 1);
    if (!buffer)
    {
        fclose(file);
        error(0, "Unable to allocate memory");
    }

    // Copy file contents to buffer
    for (int i = 0; i < len; i++)
    {
        char c = fgetc(file);
        // Error if invalid char found
        if ((c < 'A' || c > 'Z') && c != ' ')
        {
            free(buffer);
            fclose(file);
            error(0, "Invalid character found in file %s: %c, %d", path, c, c);
        }
        buffer[i] = c;
    }
    buffer[len] = '\0';

    // Close file & return string
    fclose(file);
    return buffer;
}

int main(int argc, char *argv[])
{
    // Check usage & args
    if (argc < 4)
        error(0, "USAGE: %s port\n", argv[0]);

    // Init and validate text/key
    char *text = stringFromFile(argv[1]);
    char *key = stringFromFile(argv[2]);
    if (strlen(text) > strlen(key))
        error(0, "Key shorter than text");

    // Create the socket that will listen for connections
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        error(0, "Unable to open socket");

    // Set up the address struct for the server socket
    struct sockaddr_in server;
    setupAddressStruct(&server, atoi(argv[3]), "localhost");

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        error(0, "Unable to connect to server");

    // Validate connection, send data & print encrypted text
    validate(sock);
    sendData(sock, text);
    sendData(sock, key);
    printf("%s\n", receive(sock));

    // Close the listening socket
    close(sock);
    return 0;
}