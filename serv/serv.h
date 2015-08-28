#ifndef __serv_h__
#define __serv_h__

#include <stdint.h>
#include <stdbool.h>

#define SERV_MAX_CLIENTS 3
#define SERV_POLL_SIZE SERV_MAX_CLIENTS + 1
#define SERV_PORT_DIR "/tmp/fsthost"

typedef bool (*serv_client_callback) ( char* msg, int client_sock, int client_number, void* data );

int serv_init ( uint16_t port, serv_client_callback cb, void* data );
void serv_close ();
void serv_poll ();
bool serv_send_client_data ( int client_sock, const char* msg );

#endif /* __serv_h__ */
