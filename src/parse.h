#ifndef __PARSE_H
#define __PARSE_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

union mysockaddr {
	unsigned short      family;
	struct sockaddr     generic;
        struct sockaddr_in  v4;
        struct sockaddr_in6 v6;
};

struct ip_and_mask {
	union mysockaddr ip;
	int              mask;
};

int parse_ip_to_sockaddr(struct sockaddr* out, char* src);
int parse_acl(struct ip_and_mask (**out)[0], int max, char **entries);

#endif

