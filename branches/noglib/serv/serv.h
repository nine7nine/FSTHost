#ifndef __serv_h__
#define __serv_h__

#include <stdint.h>
#include <stdbool.h>

typedef bool (*serv_client_callback) ( char* msg, int client_sock, void* data );

int serv_init ( uint16_t port, serv_client_callback cb, void* data );
void serv_close ();
void serv_poll ();
bool serv_send_client_data ( int client_sock, char* msg, int msg_len );

#endif /* __serv_h__ */
