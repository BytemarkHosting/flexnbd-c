#include "params.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


void syntax()
{
	fprintf(stderr, 
	"Syntax: flexnbd serve  <IP address> <port> <file> [ip addresses ...]\n"
	"        flexnbd read   <IP address> <port> <offset> <length> > data\n"
	"        flexnbd write  <IP address> <port> <offset> <length> < data\n"
	"        flexnbd write  <IP address> <port> <offset> <data file>\n"
	"        flexnbd mirror <IP address> <port> <target IP> <target port>\n"
	);
	exit(1);
}

#define IS_IP_VALID_CHAR(x) ( ((x) >= '0' && (x) <= '9' ) || \
                              ((x) >= 'a' && (x) <= 'f')  || \
                              ((x) >= 'A' && (x) <= 'F' ) || \
                               (x) == ':' || (x) == '.'      \
                            )
int parse_ip_to_sockaddr(struct sockaddr* out, char* src)
{
	char temp[64];
	struct sockaddr_in *v4  = (struct sockaddr_in *) out;	
	struct sockaddr_in6 *v6 = (struct sockaddr_in6 *) out;
	
	/* allow user to start with [ and end with any other invalid char */
	{
		int i=0, j=0;
		if (src[i] == '[')
			i++;
		for (; i<64 && IS_IP_VALID_CHAR(src[i]); i++)
			temp[j++] = src[i];
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

int parse_acl(struct ip_and_mask*** out, int max, char **entries)
{
	int i;
	
	if (max == 0) {
		*out = NULL;
		return 0;
	}
	
	for (i = 0; i < max; i++) {
#               define MAX_MASK_BITS (outentry->ip.family == AF_INET ? 32 : 128)
		int j;
		struct ip_and_mask* outentry = (*out)[i];
		
		if (parse_ip_to_sockaddr(&outentry->ip.generic, entries[i]) == 0)
			return i;
			
		for (j=0; entries[i][j] && entries[i][j] != '/'; j++)
			;
		if (entries[i][j] == '/') {
			outentry->mask = atoi(entries[i]+j+1);
			if (outentry->mask < 1 || outentry->mask > MAX_MASK_BITS)
				return i;
		}
		else
			outentry->mask = MAX_MASK_BITS;
#		undef MAX_MASK_BITS

		debug("acl entry %d has mask %d", i, outentry->mask);
	}
	
	return max;
}

void params_serve(
	struct mode_serve_params* out, 
	char* s_ip_address, 
	char* s_port, 
	char* s_file,
	int acl_entries,
	char** s_acl_entries
)
{
	int parsed;
	
	out->tcp_backlog = 10; /* does this need to be settable? */
	
	if (s_ip_address == NULL)
		SERVER_ERROR("No IP address supplied");
	if (s_port == NULL)
		SERVER_ERROR("No port number supplied");
	if (s_file == NULL)
		SERVER_ERROR("No filename supplied");
	
	if (parse_ip_to_sockaddr(&out->bind_to.generic, s_ip_address) == 0)
		SERVER_ERROR("Couldn't parse server address '%s' (use 0 if "
		  "you want to bind to all IPs)", s_ip_address);
	
	out->acl_entries = acl_entries;
	parsed = parse_acl(&out->acl, acl_entries, s_acl_entries);
	if (parsed != acl_entries)
		SERVER_ERROR("Bad ACL entry '%s'", s_acl_entries[parsed]);
	
	out->bind_to.v4.sin_port = atoi(s_port);
	if (out->bind_to.v4.sin_port < 0 || out->bind_to.v4.sin_port > 65535)
		SERVER_ERROR("Port number must be >= 0 and <= 65535");
	out->bind_to.v4.sin_port = htobe16(out->bind_to.v4.sin_port);
	
	out->filename = s_file;
}

void params_readwrite(
	int write_not_read,
	struct mode_readwrite_params* out,
	char* s_ip_address, 
	char* s_port,
	char* s_from,
	char* s_length_or_filename
)
{
	if (s_ip_address == NULL)
		SERVER_ERROR("No IP address supplied");
	if (s_port == NULL)
		SERVER_ERROR("No port number supplied");
	if (s_from == NULL)
		SERVER_ERROR("No from supplied");
	if (s_length_or_filename == NULL)
		SERVER_ERROR("No length supplied");
	
	if (parse_ip_to_sockaddr(&out->connect_to.generic, s_ip_address) == 0)
		SERVER_ERROR("Couldn't parse connection address '%s'", 
		s_ip_address);
	
	out->connect_to.v4.sin_port = atoi(s_port);
	if (out->connect_to.v4.sin_port < 0 || out->connect_to.v4.sin_port > 65535)
		SERVER_ERROR("Port number must be >= 0 and <= 65535");
	out->connect_to.v4.sin_port = htobe16(out->connect_to.v4.sin_port);

	out->from  = atol(s_from);
	
	if (write_not_read) {
		if (s_length_or_filename[0]-48 < 10) {
			out->len   = atol(s_length_or_filename);
			out->data_fd = 0;
		}
		else {
			out->data_fd = open(
			  s_length_or_filename, O_RDONLY);
			SERVER_ERROR_ON_FAILURE(out->data_fd,
			  "Couldn't open %s", s_length_or_filename);
			out->len = lseek64(out->data_fd, 0, SEEK_END);
			SERVER_ERROR_ON_FAILURE(out->len,
			  "Couldn't find length of %s", s_length_or_filename);
			SERVER_ERROR_ON_FAILURE(
				lseek64(out->data_fd, 0, SEEK_SET),
				"Couldn't rewind %s", s_length_or_filename
			);
		}
	}
	else {
		out->len = atol(s_length_or_filename);
		out->data_fd = 1;
	}
}

void do_serve(struct mode_serve_params* params);
void do_read(struct mode_readwrite_params* params);
void do_write(struct mode_readwrite_params* params);

void mode(char* mode, int argc, char **argv)
{
	union mode_params params;
	
	if (strcmp(mode, "serve") == 0) {
		if (argc >= 3) {
			params_serve(&params.serve, argv[0], argv[1], argv[2], argc-3, argv+3);
			do_serve(&params.serve);
		}
		else {
			syntax();
		}
	}
	else if (strcmp(mode, "read") == 0 ) {
		if (argc == 4) {
			params_readwrite(0, &params.readwrite, argv[0], argv[1], argv[2], argv[3]);
			do_read(&params.readwrite);
		}
		else {
			syntax();
		}
	}
	else if (strcmp(mode, "write") == 0 ) {
		if (argc == 4) {
			params_readwrite(1, &params.readwrite, argv[0], argv[1], argv[2], argv[3]);
			do_write(&params.readwrite);
		}
		else {
			syntax();
		}
	}
	else {
		syntax();
	}
	exit(0);
}

int main(int argc, char** argv)
{
	error_init();
	
	if (argc < 2)
		syntax();
	mode(argv[1], argc-2, argv+2);
	
	return 0;
}

