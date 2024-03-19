#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define MAX_BUFFER 1000

void report_error(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    fprintf(stderr, "Error in Decryption Client: ");
    vfprintf(stderr, msg, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

void initializeSocketAddress(struct sockaddr_in *addr, int port, char *host)
{
    if (!addr || !host)
        report_error("Invalid parameters for initializeSocketAddress");

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portString[6];
    snprintf(portString, sizeof(portString), "%d", port);

    if (getaddrinfo(host, portString, &hints, &result) != 0)
        report_error("Could not obtain address info");

    if (result->ai_family == AF_INET)
        memcpy(addr, result->ai_addr, sizeof(struct sockaddr_in));
    else
        report_error("Non-IPv4 address encountered");

    freeaddrinfo(result);
}

void transmitData(int socket_fd, const char *data)
{
    int length = strlen(data);
    if (send(socket_fd, &length, sizeof(length), 0) < 0)
        report_error("Failed to send data length");
    if (send(socket_fd, data, length, 0) < 0)
        report_error("Failed to send data");
}

char *receiveData(int socket_fd)
{
    int data_length;
    if (recv(socket_fd, &data_length, sizeof(data_length), 0) < 0)
        report_error("Failed to receive data length");

    char *buffer = malloc(data_length + 1);
    if (!buffer)
        report_error("Memory allocation failed");

    int received = 0, bytes;
    while (received < data_length)
    {
        int to_read = data_length - received > MAX_BUFFER ? MAX_BUFFER : data_length - received;
        bytes = recv(socket_fd, buffer + received, to_read, 0);
        if (bytes < 0)
            report_error("Failed to receive data");
        received += bytes;
    }

    buffer[data_length] = '\0';
    return buffer;
}

void performValidation(int sock_fd)
{
    char msgFromClient[4] = "dec", msgFromServer[4] = {0};

    if (send(sock_fd, msgFromClient, sizeof(msgFromClient), 0) < 0)
        report_error("Error sending validation message");

    int receivedBytes = 0;
    while (receivedBytes < sizeof(msgFromServer))
    {
        int bytes = recv(sock_fd, msgFromServer + receivedBytes, sizeof(msgFromServer) - receivedBytes, 0);
        if (bytes < 0)
            report_error("Error receiving validation response");
        else if (bytes == 0)
            report_error("Server closed connection unexpectedly");
        receivedBytes += bytes;
    }

    if (strncmp(msgFromClient, msgFromServer, sizeof(msgFromClient)) != 0)
    {
        close(sock_fd);
        report_error("Validation with server failed");
    }
}

char *readStringFromFile(char *filePath)
{
    FILE *filePtr = fopen(filePath, "r");
    if (!filePtr)
        report_error("Failed to open file: %s", filePath);

    fseek(filePtr, 0, SEEK_END);
    size_t fileLen = ftell(filePtr) - 1;
    fseek(filePtr, 0, SEEK_SET);

    char *fileContent = (char *)malloc(fileLen + 1);
    if (!fileContent)
    {
        fclose(filePtr);
        report_error("Memory allocation failed for file content");
    }

    for (int i = 0; i < fileLen; i++)
    {
        char ch = fgetc(filePtr);
        if ((ch < 'A' || ch > 'Z') && ch != ' ')
        {
            free(fileContent);
            fclose(filePtr);
            report_error("File contains invalid character: %s, %c", filePath, ch);
        }
        fileContent[i] = ch;
    }
    fileContent[fileLen] = '\0';

    fclose(filePtr);
    return fileContent;
}

int main(int argc, char *argv[])
{
    if (argc < 4)
        report_error("Usage: %s <text file> <key file> <port>", argv[0]);
    char *plaintext = readStringFromFile(argv[1]);
    char *encryptionKey = readStringFromFile(argv[2]);
    if (strlen(plaintext) > strlen(encryptionKey))
        report_error("The encryption key is shorter than the plaintext");

    int connection_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connection_fd < 0)
        report_error("Failed to create socket");

    struct sockaddr_in serverAddress;
    initializeSocketAddress(&serverAddress, atoi(argv[3]), "localhost");

    if (connect(connection_fd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
        report_error("Failed to connect to the server");

    performValidation(connection_fd);
    transmitData(connection_fd, plaintext);
    transmitData(connection_fd, encryptionKey);
    printf("%s\n", receiveData(connection_fd));

    close(connection_fd);
    return 0;
}