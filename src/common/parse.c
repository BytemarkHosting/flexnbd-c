#include "parse.h"
#include "util.h"

int atoi(const char *nptr);

#define IS_IP_VALID_CHAR(x) ( ((x) >= '0' && (x) <= '9' ) || \
                              ((x) >= 'a' && (x) <= 'f')  || \
                              ((x) >= 'A' && (x) <= 'F' ) || \
                               (x) == ':' || (x) == '.'      \
                            )

/* FIXME: should change this to return negative on error like everything else */
int parse_ip_to_sockaddr(struct sockaddr *out, char *src)
{
    NULLCHECK(out);
    NULLCHECK(src);

    char temp[64];
    struct sockaddr_in *v4 = (struct sockaddr_in *) out;
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *) out;

    /* allow user to start with [ and end with any other invalid char */
    {
	int i = 0, j = 0;
	if (src[i] == '[') {
	    i++;
	}
	for (; i < 64 && IS_IP_VALID_CHAR(src[i]); i++) {
	    temp[j++] = src[i];
	}
	temp[j] = 0;
    }

    if (temp[0] == '0' && temp[1] == '\0') {
	v4->sin_family = AF_INET;
	v4->sin_addr.s_addr = INADDR_ANY;
	return 1;
    }

    if (inet_pton(AF_INET, temp, &v4->sin_addr) == 1) {
	out->sa_family = AF_INET;
	return 1;
    }

    if (inet_pton(AF_INET6, temp, &v6->sin6_addr) == 1) {
	out->sa_family = AF_INET6;
	return 1;
    }

    return 0;
}


int parse_to_sockaddr(struct sockaddr *out, char *address)
{
    struct sockaddr_un *un = (struct sockaddr_un *) out;

    NULLCHECK(address);

    if (address[0] == '/') {
	un->sun_family = AF_UNIX;
	strncpy(un->sun_path, address, 108);	/* FIXME: linux only */
	return 1;
    }

    return parse_ip_to_sockaddr(out, address);
}

int parse_acl(struct ip_and_mask (**out)[], int max, char **entries)
{
    struct ip_and_mask *list;
    int i;

    if (max == 0) {
	*out = NULL;
	return 0;
    } else {
	list = xmalloc(max * sizeof(struct ip_and_mask));
	*out = (struct ip_and_mask(*)[]) list;
	debug("acl alloc: %p", *out);
    }


    for (i = 0; i < max; i++) {
	int j;
	struct ip_and_mask *outentry = &list[i];
#		define MAX_MASK_BITS (outentry->ip.family == AF_INET ? 32 : 128)
	if (parse_ip_to_sockaddr(&outentry->ip.generic, entries[i]) == 0) {
	    return i;
	}

	for (j = 0; entries[i][j] && entries[i][j] != '/'; j++);	// increment j!

	if (entries[i][j] == '/') {
	    outentry->mask = atoi(entries[i] + j + 1);
	    if (outentry->mask < 1 || outentry->mask > MAX_MASK_BITS) {
		return i;
	    }
	} else {
	    outentry->mask = MAX_MASK_BITS;
	}
#		undef MAX_MASK_BITS
	debug("acl ptr[%d]: %p %d", i, outentry, outentry->mask);
    }

    for (i = 0; i < max; i++) {
	debug("acl entry %d @ %p has mask %d", i, list[i], list[i].mask);
    }

    return max;
}


void parse_port(char *s_port, struct sockaddr_in *out)
{
    NULLCHECK(s_port);

    int raw_port;

    raw_port = atoi(s_port);
    if (raw_port < 0 || raw_port > 65535) {
	fatal("Port number must be >= 0 and <= 65535");
    }
    out->sin_port = htobe16(raw_port);
}
