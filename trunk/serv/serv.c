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

static void strip_trailing ( char* string, char chr ) {
	char* c = strrchr ( string, chr );
	if ( c ) *c = '\0';
}

static bool serv_save_port_number ( Serv* serv ) {
	char path[64];

	mkdir ( SERV_PORT_DIR, 0777 );

	pid_t pid = getpid();
	snprintf( path, sizeof path, "%s/%d.%d.port", SERV_PORT_DIR, pid, serv->port );

	FILE* f = fopen(path, "w");
	if ( ! f ) {
		ERROR ( "Can't open file: %s", path );
		return false;
	}

	fprintf( f, "%d", serv->port );
	fclose(f);

	serv->port_file = strdup( path );

	return true;
}

static void serv_client_close ( ServClient* client ) {
	if ( client->fd != -1 ) {
		close ( client->fd );
		client->fd = -1;
	}
	client->closed = true;
}

static void serv_client_get_data ( ServClient* client ) {
	char msg[64];

	//Receive a message from client
	ssize_t read_size = read (client->fd, msg, sizeof msg );
	if (read_size == 0) {
		INFO("Client disconnected");
		serv_client_close( client );
	} else if (read_size < 0) {
		ERROR("recv failed");
		serv_client_close( client );
	} else {
		// Message normalize
		msg[read_size] = '\0'; /* make sure there is end */
		strip_trailing ( msg, '\n' );
		strip_trailing ( msg, '\r' );
	}

	if ( ! client->callback(client, msg) )
		serv_client_close( client );
}

static int serv_get_client_fd ( Serv* serv ) {
	//Accept and incoming connection
	INFO("Waiting for incoming connections...");
	int c = sizeof(struct sockaddr_in);
     
	//accept connection from an incoming client
	struct sockaddr_in client;
	int client_sock = accept(serv->fd, (struct sockaddr *)&client, (socklen_t*)&c);
	if (client_sock < 0) {
		ERROR("accept failed");
		return 0;
	}
	INFO("Connection accepted");

	return client_sock;
}

static void serv_client_open ( Serv* serv ) {
	/* Find space for new client */
	int i;
	for ( i = 0; i < SERV_MAX_CLIENTS; i++ ) {
		if ( serv->clients[i].fd == -1 ) {
			ServClient* client = &serv->clients[i];
			client->fd = serv_get_client_fd( serv );
			client->closed = false;
			client->data = NULL;
			return;
		}
	}
}

Serv* serv_init ( uint16_t port, serv_client_callback cb ) {
	struct sockaddr_in server;
	socklen_t addrlen = sizeof(server);

	//Create listener socket
	int fd = socket(AF_INET , SOCK_STREAM , 0);
	if (fd == -1) {
		ERROR("Could not create socket");
		return NULL;
	}
	INFO("Socket created");

	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl( INADDR_ANY );
	server.sin_port = htons( port );

	// Allow reuse this port ( TIME_WAIT issue )
	int ofc = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &ofc, sizeof ofc);

	//Bind
	int ret = bind (fd,(struct sockaddr *) &server , addrlen);
	if ( ret < 0 ) {
		ERROR("bind failed. Error");
		return NULL;
	}
	getsockname ( fd, (struct sockaddr *) &server , &addrlen );

	//Listen
	listen(fd, SERV_MAX_CLIENTS);

	// Alloc new object
	Serv* serv = malloc( sizeof(Serv) );
	serv->fd = fd;
	serv->port = ntohs( server.sin_port );

	// Init clients
	int i;
	for ( i = 0; i < SERV_MAX_CLIENTS; i++ ) {
		ServClient* client = &serv->clients[i];
		client->fd = -1;
		client->number = i;
		client->callback = cb;
		client->closed = true;
		client->data = NULL;
	}

	serv_save_port_number( serv );

	INFO("Serv start on port: %d", serv->port);

	return serv;
}

bool serv_client_send_data ( ServClient* client, const char* msg ) {
	size_t msg_len = strlen(msg);
	char data[msg_len + 2];
	sprintf ( data, "%s\n", msg );
	size_t len = sizeof(data) - 1;
	ssize_t write_size = write ( client->fd, data, len );
	return ( write_size == len ) ? true : false;
}

void serv_poll (Serv* serv) {
	struct pollfd fds[SERV_POLL_SIZE];

	int i;
	for ( i = 0; i < SERV_POLL_SIZE; i++ ) {
		if ( i == 0 ) {
			fds[i].fd = serv->fd;
		} else {
			fds[i].fd = serv->clients[i-1].fd;
		}

		fds[i].events = POLLIN;
		fds[i].revents = 0;
	}

	//wait for an activity on one of the sockets, don't block
	int activity = poll(fds, SERV_POLL_SIZE, 0);
	if (activity < 1) return;

	for ( i = 0; i < SERV_POLL_SIZE; i++ ) {
		if ( fds[i].revents != POLLIN) {
			if ( fds[i].revents != 0 )
				ERROR("FDS: %d, Err revents = %d", i, fds[i].revents);

			continue;
		}

		// Server socket i.e. new client
		if ( i == 0 ) {
			serv_client_open(serv);
		// Client socket
		} else {
			serv_client_get_data( &serv->clients[i] );
		}
	}
}

void serv_close (Serv* serv) {
	// Close all clients
	int i;
	for ( i = 0; i < SERV_MAX_CLIENTS; i ++ )
		serv_client_close( &serv->clients[i] );

	// Close server
	close ( serv->fd );

	// Remove port file ( if exists ;-)
	if ( serv->port_file ) {
		unlink ( serv->port_file );
		free ( serv->port_file );
	}

	free(serv);
}
