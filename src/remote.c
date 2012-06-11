#include "ioutil.h"
#include "util.h"

#include <stdlib.h>
#include <sys/un.h>

static const int max_response=1024;

void do_remote_command(char* command, char* socket_name, int argc, char** argv)
{
	char newline=10;
	int i;
	int exit_status;
	int remote = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un address;
	char response[max_response];
	
	memset(&address, 0, sizeof(address));
	
	FATAL_IF_NEGATIVE(remote, "Couldn't create client socket");
	
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, socket_name, sizeof(address.sun_path));
	
	FATAL_IF_NEGATIVE(
		connect(remote, (struct sockaddr*) &address, sizeof(address)),
		"Couldn't connect to %s", socket_name
	);
	
	write(remote, command, strlen(command));
	write(remote, &newline, 1);
	for (i=0; i<argc; i++) {
		write(remote, argv[i], strlen(argv[i]));
		write(remote, &newline, 1);
	}
	write(remote, &newline, 1);
	
	FATAL_IF_NEGATIVE(
		read_until_newline(remote, response, max_response),
		"Couldn't read response from %s", socket_name
	);
	
	exit_status = atoi(response);
	if (exit_status > 0) {
		fprintf(stderr, "%s\n", strchr(response, ':')+2);
	}
	
	exit(atoi(response));
	
	close(remote);
}

