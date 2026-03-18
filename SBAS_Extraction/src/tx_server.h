#ifndef TX_SERVER_H
#define TX_SERVER_H

#include <stddef.h>
#include <stdint.h>

int tx_server_init(int tcp_port, int max_clients);
void tx_server_cleanup(void);
int tx_server_accept_new_clients(void);
int tx_server_broadcast(const uint8_t *data, size_t length);
int tx_server_client_count(void);

#endif /* TX_SERVER_H */


