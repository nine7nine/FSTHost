#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>    //write
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr

int serv_get_sock ( uint16_t port ) {
	struct sockaddr_in server;

	//Create socket
	int socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1) {
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
	setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &ofc, sizeof ofc);

	//Bind
	int ret = bind (socket_desc,(struct sockaddr *) &server , sizeof(server));
	if ( ret < 0 ) {
		//print the error message
		perror("bind failed. Error");
		return 0;
	}
	puts("bind done");

	//Listen
	listen(socket_desc , 3);
     
	return socket_desc;
}

int serv_get_client ( int socket_desc ) {
	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	int c = sizeof(struct sockaddr_in);
     
	//accept connection from an incoming client
	struct sockaddr_in client;
	int client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c);
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

bool serv_send_client_data ( int client_sock, char* msg, int msg_len ) {
	char data[msg_len + 2];
	snprintf ( data, sizeof data, "%s\n", msg );
	int write_size = write ( client_sock , data, sizeof data );
	return ( write_size == msg_len ) ? true : false;
}

bool serv_client_get_data ( int client_sock, char* msg, int msg_max_len ) {
	//Receive a message from client
	int read_size = recv (client_sock , msg , msg_max_len, 0);
	if (read_size == 0) {
		puts("Client disconnected");
		close ( client_sock );
		return false;
	} else if (read_size < 0) {
		perror("recv failed");
		close ( client_sock );
		return false;
	}
	msg[read_size] = '\0'; /* make sure there is end */

	strip_trailing ( msg, '\n' );
	strip_trailing ( msg, '\r' );

	// send the message back to client
	char buf[100];
	snprintf ( buf, sizeof buf, "%s", "OK" );
	serv_send_client_data ( client_sock, buf, strlen(buf) );

	if ( !strcasecmp ( msg, "quit" ) ) {
		puts ( "GOT QUIT" );
		close ( client_sock );
		return false;
	}

	return true;
}

void serv_close_socket ( int socket_desc ) {
	close ( socket_desc );
}
