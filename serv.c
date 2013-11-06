#include <stdio.h>
#include <string.h>    //strlen
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h>    //write
#include <stdbool.h>

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

bool serv_client_get_data ( int client_sock ) {
	//Receive a message from client
	char client_message[2000];
	int read_size = recv (client_sock , client_message , sizeof client_message, 0);
	if (read_size == 0) {
		puts("Client disconnected");
		close ( client_sock );
		return false;
	} else if (read_size < 0) {
		perror("recv failed");
		close ( client_sock );
		return false;
	}
	client_message[read_size] = '\0';

	char* newline = strrchr ( client_message, '\n' );
	if ( newline ) *newline = '\0';

	char* cr = strrchr ( client_message, '\r' );
	if ( cr ) *cr = '\0';

	char buf[100];
	snprintf ( buf, sizeof buf, "%s", "CMD OK\n" );

	// send the message back to client
	read_size = write ( client_sock , buf, strlen(buf) );

	if ( !strcmp ( client_message, "quit" ) ) {
		puts ( "GOT QUIT" );
		close ( client_sock );
		return false;
	}

	return true;
}
