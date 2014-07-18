#ifndef __serv_h__
#define __serv_h__

#include <stdint.h>
#include <stdbool.h>

int serv_get_sock ( uint16_t port );
int serv_get_client ( int socket_desc );
bool serv_send_client_data ( int client_sock, char* msg, int msg_len );
bool serv_client_get_data ( int client_sock, char* msg, int msg_max_len );
void serv_close_socket ( int socket_desc );

#endif /* __serv_h__ */
