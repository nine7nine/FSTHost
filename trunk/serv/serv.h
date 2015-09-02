#ifndef __serv_h__
#define __serv_h__

#include <stdint.h>
#include <stdbool.h>

#define SERV_MAX_CLIENTS 3
#define SERV_POLL_SIZE SERV_MAX_CLIENTS + 1
#define SERV_PORT_DIR "/tmp/fsthost"

typedef struct _ServClient ServClient;
typedef bool (*serv_client_callback) ( ServClient* client, char* msg );

struct _ServClient {
	int fd; /* descriptior */
	int number; /* TODO: userfull only for news (?!?) */
	serv_client_callback callback;
	void* data;
	bool closed;
};

typedef struct _Serv {
	int fd; /* descriptor */
	uint16_t port;
	void* serv_usr_data;
	char* port_file;
	ServClient clients[SERV_MAX_CLIENTS];
} Serv;

Serv* serv_init ( uint16_t port, serv_client_callback cb );
void serv_close (Serv* serv);
void serv_poll (Serv* serv);
bool serv_client_send_data ( ServClient* client, const char* msg );

#endif /* __serv_h__ */
