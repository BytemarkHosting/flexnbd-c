#ifndef PARSE_H
#define PARSE_H

#include <sys/socket.h>
#include <sys/un.h>

#include <arpa/inet.h>
#include <unistd.h>

union mysockaddr {
	unsigned short      family;
	struct sockaddr     generic;
        struct sockaddr_in  v4;
        struct sockaddr_in6 v6;
        struct sockaddr_un  un;
};

struct ip_and_mask {
	union mysockaddr ip;
	int              mask;
};

int parse_ip_to_sockaddr(struct sockaddr* out, char* src);
int parse_to_sockaddr(struct sockaddr* out, char* src);
int parse_acl(struct ip_and_mask (**out)[], int max, char **entries);
void parse_port( char *s_port, struct sockaddr_in *out );

#endif

