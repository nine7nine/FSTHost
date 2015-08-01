#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <poll.h>
#include <sys/stat.h>

#include "log/log.h"
#include "serv.h"

#define INFO log_info
#define DEBUG log_debug
#define ERROR log_error

#define MAX_CLIENTS 3
#define POLL_SIZE MAX_CLIENTS + 1
#define PORT_DIR "/tmp/fsthost"

static struct pollfd fds[POLL_SIZE];
static uint8_t client_changes[POLL_SIZE];
static serv_client_callback client_callback;
static void* serv_usr_data = NULL;
static bool initialized = false;
static char* port_file = NULL;

static int serv_get_client ( int serv_fd ) {
	//Accept and incoming connection
	INFO("Waiting for incoming connections...");
	int c = sizeof(struct sockaddr_in);
     
	//accept connection from an incoming client
	struct sockaddr_in client;
	int client_sock = accept(serv_fd, (struct sockaddr *)&client, (socklen_t*)&c);
	if (client_sock < 0) {
		ERROR("accept failed");
		return 0;
	}
	INFO("Connection accepted");

	return client_sock;
}

static void strip_trailing ( char* string, char chr ) {
	char* c = strrchr ( string, chr );
	if ( c ) *c = '\0';
}

static bool serv_save_port_number ( uint16_t port ) {
	char path[64];

	mkdir ( PORT_DIR, 0777 );

	pid_t pid = getpid();
	snprintf( path, sizeof path, "%s/%d.%d.port", PORT_DIR, pid, port );

	FILE* f = fopen(path, "w");
	if ( ! f ) {
		ERROR ( "Can't open file: %s", path );
		return false;
	}

	fprintf( f, "%d", port );
	fclose(f);

	port_file = strdup( path );

	return true;
}

static bool serv_client_get_data ( int client_sock, char* msg, size_t msg_max_len ) {
	//Receive a message from client
	ssize_t read_size = read (client_sock , msg , msg_max_len );
	if (read_size == 0) {
		INFO("Client disconnected");
		return false;
	} else if (read_size < 0) {
		ERROR("recv failed");
		return false;
	}

	// Message normalize
	msg[read_size] = '\0'; /* make sure there is end */

	strip_trailing ( msg, '\n' );
	strip_trailing ( msg, '\r' );

	return true;
}

int serv_init ( uint16_t port, serv_client_callback cb, void* data ) {
	struct sockaddr_in server;
	socklen_t addrlen = sizeof(server);

	client_callback = cb;
	serv_usr_data = data;
	memset(fds, 0 , sizeof(fds));

	int i;
	for ( i = 0; i < POLL_SIZE; i++ ) {
		fds[i].fd = -1;
		fds[i].events = POLLIN;
		client_changes[i] = 0;
	}

	//Create listener socket
	fds[0].fd = socket(AF_INET , SOCK_STREAM , 0);
	if (fds[0].fd == -1) {
		ERROR("Could not create socket");
		return 2;
	}
	INFO("Socket created");

	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl( INADDR_ANY );
	server.sin_port = htons( port );

	// Allow reuse this port ( TIME_WAIT issue )
	int ofc = 1;
	setsockopt(fds[0].fd, SOL_SOCKET, SO_REUSEADDR, &ofc, sizeof ofc);

	//Bind
	int ret = bind (fds[0].fd,(struct sockaddr *) &server , addrlen);
	if ( ret < 0 ) {
		ERROR("bind failed. Error");
		return 0;
	}
	getsockname ( fds[0].fd, (struct sockaddr *) &server , &addrlen );
	port = ntohs( server.sin_port ) ;
	INFO("bind done, port: %d", port);
	serv_save_port_number ( port );

	//Listen
	listen(fds[0].fd, MAX_CLIENTS);

	initialized = true;

	return fds[0].fd;
}

bool serv_send_client_data ( int client_sock, const char* msg ) {
	size_t msg_len = strlen(msg);
	char data[msg_len + 2];
	sprintf ( data, "%s\n", msg );
	size_t len = sizeof(data) - 1;
	ssize_t write_size = write ( client_sock , data, len );
	return ( write_size == len ) ? true : false;
}

void serv_poll ( uint8_t changes ) {
	if ( ! initialized ) return;

	// Update changes
	int i;
	for ( i = 0; i < POLL_SIZE; i++ )
		client_changes[i] |= changes;

	//wait for an activity on one of the sockets, don't block
	int activity = poll(fds, POLL_SIZE, 0);
	if (activity < 1) return;

	for ( i = 0; i < POLL_SIZE; i++ ) {
		if ( fds[i].revents == 0 ) continue;

		if( fds[i].revents != POLLIN) {
			ERROR("Err revents = %d", fds[i].revents);
			return;
		}

		// Server socket
		if ( i == 0 ) {
			int g;
			for ( g = 1; g < POLL_SIZE; g++ ) {
				if ( fds[g].fd == -1 ) {
					fds[g].fd = serv_get_client ( fds[0].fd );
					fds[g].revents = 0;
					client_changes[g] = 0;
					break;
				}
			}
		// Client socket
		} else {
			char msg[64];
			if ( ! serv_client_get_data( fds[i].fd, msg, sizeof msg ) ||
			     ! client_callback ( msg, fds[i].fd, &client_changes[i], serv_usr_data )
			) {
				close ( fds[i].fd );
				fds[i].fd = -1;
			}
		}
	}
}

void serv_close () {
	if ( ! initialized ) return;

	// Close clients
	int i;
	for ( i = 0; i < POLL_SIZE; i ++ ) {
		if ( fds[i].fd != -1 ) {
			close ( fds[i].fd );
			fds[i].fd = -1; // just for beauty ;-)
		}
	}

	// Close server
	close ( fds[0].fd );

	if ( port_file ) {
		unlink ( port_file );
		free ( port_file );
	}
}

