#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "tx_server.h"


static SOCKET g_server_socket = INVALID_SOCKET;
static SOCKET *g_client_sockets = NULL;
static int g_max_clients = 0;
static int g_client_count = 0;

int tx_server_init(int tcp_port, int max_clients)
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
		fprintf(stderr, "WSAStartup failed\n");
		return -1;
	}

	g_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_server_socket == INVALID_SOCKET) {
		fprintf(stderr, "Failed to create server socket: %d\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	int opt = 1;
	setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(tcp_port);

	if (bind(g_server_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "Bind failed: %d\n", WSAGetLastError());
		closesocket(g_server_socket);
		WSACleanup();
		return -1;
	}

	if (listen(g_server_socket, max_clients) == SOCKET_ERROR) {
		fprintf(stderr, "Listen failed: %d\n", WSAGetLastError());
		closesocket(g_server_socket);
		WSACleanup();
		return -1;
	}

	// Set the server socket to non-blocking so accept() will not block the main loop
	u_long mode = 1;
	ioctlsocket(g_server_socket, FIONBIO, &mode);

	g_client_sockets = (SOCKET*)malloc(sizeof(SOCKET) * max_clients);
	if (!g_client_sockets) {
		fprintf(stderr, "Failed to allocate client sockets\n");
		closesocket(g_server_socket);
		WSACleanup();
		return -1;
	}
	for (int i = 0; i < max_clients; i++) g_client_sockets[i] = INVALID_SOCKET;
	g_max_clients = max_clients;
	g_client_count = 0;

	printf("TCP server initialized on port %d\n", tcp_port);
	return 0;
}

void tx_server_cleanup(void)
{
	if (g_client_sockets) {
		for (int i = 0; i < g_max_clients; i++) {
			if (g_client_sockets[i] != INVALID_SOCKET) closesocket(g_client_sockets[i]);
		}
		free(g_client_sockets);
		g_client_sockets = NULL;
	}
	if (g_server_socket != INVALID_SOCKET) closesocket(g_server_socket);
	WSACleanup();
	printf("TCP server cleaned up\n");
}

int tx_server_accept_new_clients(void)
{
	struct sockaddr_in client_addr;
	int addr_len = sizeof(client_addr);
	SOCKET new_socket = accept(g_server_socket, (struct sockaddr*)&client_addr, &addr_len);
	if (new_socket == INVALID_SOCKET) {
		int error = WSAGetLastError();
		if (error != WSAEWOULDBLOCK) {
			fprintf(stderr, "Accept failed: %d\n", error);
		}
		return 0;
	}
	// Set client socket non-blocking as well to avoid send() blocking
	u_long cmode = 1;
	ioctlsocket(new_socket, FIONBIO, &cmode);
	for (int i = 0; i < g_max_clients; i++) {
		if (g_client_sockets[i] == INVALID_SOCKET) {
			g_client_sockets[i] = new_socket;
			g_client_count++;
			printf("New client connected: %s:%d (slot %d)\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), i);
			return 1;
		}
	}
	printf("Max clients reached, rejecting connection\n");
	closesocket(new_socket);
	return 0;
}

int tx_server_broadcast(const uint8_t *data, size_t length)
{
	int total_sent = 0;
	for (int i = 0; i < g_max_clients; i++) {
		if (g_client_sockets[i] != INVALID_SOCKET) {
			int result = send(g_client_sockets[i], (const char*)data, (int)length, 0);
			if (result == SOCKET_ERROR) {
				int error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK) {
					printf("Client %d disconnected (error %d)\n", i, error);
					closesocket(g_client_sockets[i]);
					g_client_sockets[i] = INVALID_SOCKET;
					g_client_count--;
				}
			} else {
				total_sent += result;
			}
		}
	}
	return total_sent;
}

int tx_server_client_count(void)
{
	return g_client_count;
}


