#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <poll.h>

#include "serv.h"

#define MAX_CLIENTS 3
#define POLL_SIZE MAX_CLIENTS + 1

static struct pollfd fds[POLL_SIZE];
static serv_client_callback client_callback;
static void* serv_usr_data = NULL;

static int serv_get_client ( int serv_fd ) {
	//Accept and incoming connection
	printf("Waiting for incoming connections...");
	int c = sizeof(struct sockaddr_in);
     
	//accept connection from an incoming client
	struct sockaddr_in client;
	int client_sock = accept(serv_fd, (struct sockaddr *)&client, (socklen_t*)&c);
	if (client_sock < 0) {
		perror("accept failed");
		return 0;
	}
	puts("Connection accepted");

	return client_sock;
}

static void strip_trailing ( char* string, char chr ) {
	char* c = strrchr ( string, chr );
	if ( c ) *c = '\0';
}

static bool serv_client_get_data ( int client_sock, char* msg, size_t msg_max_len ) {
	//Receive a message from client
	ssize_t read_size = read (client_sock , msg , msg_max_len );
	if (read_size == 0) {
		puts("Client disconnected");
		return false;
	} else if (read_size < 0) {
		perror("recv failed");
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
	}

	//Create listener socket
	fds[0].fd = socket(AF_INET , SOCK_STREAM , 0);
	if (fds[0].fd == -1) {
		printf("Could not create socket");
		return 2;
	}
	puts("Socket created");

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
		perror("bind failed. Error");
		return 0;
	}
	getsockname ( fds[0].fd, (struct sockaddr *) &server , &addrlen );
	printf ("bind done, port: %d\n", ntohs( server.sin_port ) );

	//Listen
	listen(fds[0].fd, MAX_CLIENTS);	

	return fds[0].fd;
}

bool serv_send_client_data ( int client_sock, char* msg, int msg_len ) {
	char data[msg_len + 2];
	snprintf ( data, sizeof data, "%s\n", msg );
	size_t len = sizeof(data) - 1;
	ssize_t write_size = write ( client_sock , data, len );
	return ( write_size == len ) ? true : false;
}


void serv_poll () {
	if ( fds[0].fd == -1 ) return;

	//wait for an activity on one of the sockets, don't block
	int activity = poll(fds, POLL_SIZE, 0);
	if (activity < 1) return;

	int i;
	for ( i = 0; i < POLL_SIZE; i ++ ) {
		if ( fds[i].revents == 0 ) continue;

		if( fds[i].revents != POLLIN) {
			printf("Err revents = %d\n", fds[i].revents);
			return;
		}

		// Server socket
		if ( i == 0 ) {
			int g;
			for ( g = 1; g < POLL_SIZE; g++ ) {
				if ( fds[g].fd == -1 ) {
					fds[g].fd = serv_get_client ( fds[0].fd );
					fds[g].revents = 0;
					break;
				}
			}
		// Client socket
		} else {
			char msg[64];
			if ( ! serv_client_get_data( fds[i].fd, msg, sizeof msg ) ||
			     ! client_callback ( msg, fds[i].fd, serv_usr_data )
			) {
				close ( fds[i].fd );
				fds[i].fd = -1;
			}
		}
	}
}

void serv_close_socket () {
	close ( fds[0].fd );
}
