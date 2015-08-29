#ifndef __serv_h__
#define __serv_h__

#include <stdint.h>
#include <stdbool.h>

#define SERV_MAX_CLIENTS 3
#define SERV_POLL_SIZE SERV_MAX_CLIENTS + 1
#define SERV_PORT_DIR "/tmp/fsthost"

typedef bool (*serv_client_callback) ( Client* client, char* msg );

typedef struct _ServClient {
	int fd; /* descriptior */
	void* data;
	bool closed;
} ServClient;

typedef struct _Serv {
	int fd; /* descriptor */
	serv_client_callback client_callback;
	void* serv_usr_data;
	const char* port_file;
	ServClient clients[SERV_MAX_CLIENTS];
} Serv;

Serv* serv_init ( uint16_t port, serv_client_callback cb, void* data );
void serv_close (Serv* serv);
void serv_poll (Serv* serv);
bool serv_client_send_data ( ServClient* client, const char* msg );

#endif /* __serv_h__ */
