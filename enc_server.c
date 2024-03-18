#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>

#define BUFFER_SIZE 1000

int error(int exitCode, const char *format, ...)
{
	// Retrieve additional arguments
	va_list args;
	va_start(args, format);

	// Print error to stderr
	fprintf(stderr, "Client error: ");
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");

	// End var arg list & exit
	va_end(args);
	exit(exitCode);
}

void setupAddressStruct(struct sockaddr_in *address, int portNumber)
{
	// Clear out the address struct
	memset((char *)address, '\0', sizeof(*address));

	// The address should be network capable
	address->sin_family = AF_INET;

	// Store the port number
	address->sin_port = htons(portNumber);

	// Allow a client at any address to connect to this server
	address->sin_addr.s_addr = INADDR_ANY;
}

void sendData(int sock, char *data)
{
    // Get length of data
    int len = (int)strlen(data);
    int totalSent = 0;

    // Send the length of the data
    if (send(sock, &len, sizeof(len), 0) < 0)
        error(1, "Unable to write to socket");

    // Loop over send() for the data
    while (totalSent < len)
    {
        // Determine the number of characters to send in this iteration
        int charsToSend = len - totalSent;
        if (charsToSend > BUFFER_SIZE)
            charsToSend = BUFFER_SIZE;

        // Send data
        int charsSent = send(sock, data + totalSent, charsToSend, 0);
        if (charsSent < 0)
            error(1, "Unable to write to socket");

        // Update total sent
        totalSent += charsSent;
    }
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
		error(1, "Unable to write to socket");
		return;
	}

	int totalReceived = 0;
	while (totalReceived < sizeof(serverMsg))
	{
		int received = recv(sock, serverMsg + totalReceived, sizeof(serverMsg) - totalReceived, 0);
		if (received < 0)
		{
			error(1, "Unable to read from socket");
			return;
		}
		else if (received == 0)
		{
			error(2, "Connection closed by server");
			return;
		}
		totalReceived += received;
	}

	if (strncmp(clientMsg, serverMsg, sizeof(clientMsg)) != 0)
	{
		close(sock);
		error(2, "Client not enc_client");
	}
}

void handleOtpComm(int sock)
{
	// Receive text and key
	char *text = receive(sock);
	char *key = receive(sock);

	// Determine the length of the text to avoid multiple strlen calls
	int len = strlen(text);

	// Allocate memory for result based on the text length
	char *result = (char *)malloc(len + 1);
	if (result == NULL)
	{
		// Handle memory allocation failure
		free(text);
		free(key);
		close(sock);
		return;
	}

	// Pre-calculate the value for space
	int spaceVal = 26; // 26 represents the space

	// Perform encryption
	for (int i = 0; i < len; i++)
	{
		int txtVal = (text[i] == ' ') ? spaceVal : text[i] - 'A';
		int keyVal = (key[i] == ' ') ? spaceVal : key[i] - 'A';
		int encVal = (txtVal + keyVal) % 27;
		result[i] = (encVal == spaceVal) ? ' ' : encVal + 'A';
	}
	result[len] = '\0';

	// Send the encrypted text back, free the allocated memory, and close the socket
	sendData(sock, result);
	free(result);
	free(text);
	free(key);
	close(sock);
}

volatile sig_atomic_t keep_running = 1;

void sigint_handler(int signum)
{
	keep_running = 0;
}

int main(int argc, const char *argv[])
{
	// Register SIGINT handler
	signal(SIGINT, sigint_handler);

	// Check usage & args
	if (argc < 2)
		error(1, "USAGE: %s port\n", argv[0]);

	// Create the socket that will listen for connections
	int listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSock < 0)
		error(1, "Unable to open socket");

	// Set socket option to allow address reuse
	int optval = 1;
	if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
		error(1, "Unable to set socket options");

	// Set up the address struct for the server socket
	struct sockaddr_in server, client;
	socklen_t clientSize = sizeof(client);
	setupAddressStruct(&server, atoi(argv[1]));

	// Associate the socket to the port
	if (bind(listenSock, (struct sockaddr *)&server, sizeof(server)) < 0)
		error(1, "Unable to bind socket");

	// Start listening for connections. Allow up to 5 connections to queue up
	listen(listenSock, 5);

	// Main loop
	while (keep_running)
	{
		// Accept the connection request which creates a connection socket
		int sock = accept(listenSock, (struct sockaddr *)&client, &clientSize);
		if (sock < 0)
			error(1, "Unable to accept connection");

		// Fork children to handle client connections
		int pid = fork();
		switch (pid)
		{
		case -1:
			// Fork error
			error(1, "Unable to fork child");
			break;
		case 0:
			// Child case
			validate(sock);
			handleOtpComm(sock);
			exit(0);
		default:
			// Parent case
			close(sock);
		}
	}

	// Close the listening socket
	close(listenSock);
	return 0;
}