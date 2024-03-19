#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>

#define MAX_BUFFER 1000

int handle_error(int statusCode, const char *msg, ...)
{
    va_list argp;
    va_start(argp, msg);
    fprintf(stderr, "Error detected: ");
    vfprintf(stderr, msg, argp);
    fprintf(stderr, "\n");
    va_end(argp);
    exit(statusCode);
}

void init_sockaddr(struct sockaddr_in *addr, int port)
{
    memset((char *)addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    addr->sin_addr.s_addr = INADDR_ANY;
}

void send_message(int connection, char *message)
{
    int message_len = strlen(message), sent_bytes = 0;
    if (send(connection, &message_len, sizeof(message_len), 0) < 0)
        handle_error(1, "Error sending on socket");

    while (sent_bytes < message_len)
    {
        int bytes_to_send = message_len - sent_bytes > MAX_BUFFER ? MAX_BUFFER : message_len - sent_bytes;
        int sent = send(connection, message + sent_bytes, bytes_to_send, 0);
        if (sent < 0)
            handle_error(1, "Error sending on socket");
        sent_bytes += sent;
    }
}

char *receive_message(int connection)
{
    int message_len;
    if (recv(connection, &message_len, sizeof(message_len), 0) < 0)
        handle_error(1, "Error reading from socket");

    char *buffer = malloc(message_len + 1);
    if (!buffer)
        handle_error(1, "Memory allocation failed");

    int received = 0;
    while (received < message_len)
    {
        int bytes = recv(connection, buffer + received, message_len - received, 0);
        if (bytes < 0)
        {
            free(buffer);
            handle_error(1, "Error reading from socket");
        }
        received += bytes;
    }
    buffer[message_len] = '\0';
    return buffer;
}

void authenticate_client(int connection)
{
    char server_signal[4] = "dec", client_signal[4] = {0};
    if (send(connection, server_signal, sizeof(server_signal), 0) < 0)
        handle_error(1, "Error sending on socket");

    int received_bytes = 0;
    while (received_bytes < sizeof(client_signal))
    {
        int bytes = recv(connection, client_signal + received_bytes, sizeof(client_signal) - received_bytes, 0);
        if (bytes < 0)
            handle_error(1, "Error reading from socket");
        received_bytes += bytes;
    }

    if (strncmp(server_signal, client_signal, sizeof(server_signal)) != 0)
    {
        close(connection);
        handle_error(2, "Authentication failed");
    }
}

void process_decryption(int connection)
{
    char *encrypted_text = receive_message(connection);
    char *decryption_key = receive_message(connection);
    int text_length = strlen(encrypted_text);

    char *decrypted_text = malloc(text_length + 1);
    if (!decrypted_text)
    {
        free(encrypted_text);
        free(decryption_key);
        close(connection);
        return;
    }

    for (int i = 0; i < text_length; ++i)
    {
        int text_char = (encrypted_text[i] == ' ') ? 26 : encrypted_text[i] - 'A';
        int key_char = (decryption_key[i] == ' ') ? 26 : decryption_key[i] - 'A';
        int decrypted_char = (text_char - key_char + 27) % 27;
        decrypted_text[i] = (decrypted_char == 26) ? ' ' : decrypted_char + 'A';
    }
    decrypted_text[text_length] = '\0';

    send_message(connection, decrypted_text);
    free(decrypted_text);
    free(encrypted_text);
    free(decryption_key);
    close(connection);
}

volatile sig_atomic_t server_active = 1;

void stop_server(int signal)
{
    server_active = 0;
}

int main(int argc, char *argv[])
{
    signal(SIGINT, stop_server);
    if (argc < 2)
        handle_error(1, "Usage: %s port_number\n", argv[0]);

    int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0)
        handle_error(1, "Error opening socket");

    int option_value = 1;
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) < 0)
        handle_error(1, "Error setting socket options");

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    init_sockaddr(&server_addr, atoi(argv[1]));

    if (bind(listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        handle_error(1, "Error binding socket");

    listen(listen_socket, 5);

    while (server_active)
    {
        int connection_fd = accept(listen_socket, (struct sockaddr *)&client_addr, &client_addr_size);
        if (connection_fd < 0)
            handle_error(1, "Error accepting connection");

        int pid = fork();
        if (pid < 0)
            handle_error(1, "Error forking process");
        else if (pid == 0)
        {
            authenticate_client(connection_fd);
            process_decryption(connection_fd);
            exit(0);
        }
        else
            close(connection_fd);
    }

    close(listen_socket);
    return 0;
}