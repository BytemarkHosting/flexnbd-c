#ifndef MODE_H
#define MODE_H


void mode(char* mode, int argc, char **argv);


#include <getopt.h>

#define GETOPT_ARG(x,s)  {(x), 1, 0, (s)}
#define GETOPT_FLAG(x,v) {(x), 0, 0, (v)}

#define OPT_HELP "help"
#define OPT_ADDR "addr"
#define OPT_REBIND_ADDR "rebind-addr"
#define OPT_BIND "bind"
#define OPT_PORT "port"
#define OPT_REBIND_PORT "rebind-port"
#define OPT_FILE "file"
#define OPT_SOCK "sock"
#define OPT_FROM "from"
#define OPT_SIZE "size"
#define OPT_DENY "default-deny"

#define CMD_SERVE  "serve"
#define CMD_LISTEN "listen"
#define CMD_READ   "read"
#define CMD_WRITE  "write"
#define CMD_ACL    "acl"
#define CMD_MIRROR "mirror"
#define CMD_STATUS "status"
#define CMD_HELP   "help"
#define LEN_CMD_MAX 7

#define PATH_LEN_MAX 1024
#define ADDR_LEN_MAX 64


#define IS_CMD(x,c) (strncmp((x),(c),(LEN_CMD_MAX)) == 0)

#define GETOPT_HELP GETOPT_FLAG( OPT_HELP, 'h' )
#define GETOPT_DENY GETOPT_FLAG( OPT_DENY, 'd' )

#define GETOPT_ADDR GETOPT_ARG( OPT_ADDR, 'l' )
#define GETOPT_REBIND_ADDR GETOPT_ARG( OPT_REBIND_ADDR, 'L')
#define GETOPT_PORT GETOPT_ARG( OPT_PORT, 'p' )
#define GETOPT_REBIND_PORT GETOPT_ARG( OPT_REBIND_PORT, 'P')
#define GETOPT_FILE GETOPT_ARG( OPT_FILE, 'f' )
#define GETOPT_SOCK GETOPT_ARG( OPT_SOCK, 's' )
#define GETOPT_FROM GETOPT_ARG( OPT_FROM, 'F' )
#define GETOPT_SIZE GETOPT_ARG( OPT_SIZE, 'S' )
#define GETOPT_BIND GETOPT_ARG( OPT_BIND, 'b' )

#ifdef DEBUG
#  define OPT_VERBOSE "verbose"
#  define SOPT_VERBOSE "v"
#  define GETOPT_VERBOSE GETOPT_FLAG( OPT_VERBOSE, 'v' )
#  define VERBOSE_LINE \
  "\t--" OPT_VERBOSE ",-" SOPT_VERBOSE "\t\tOutput debug information.\n"
#else
#  define GETOPT_VERBOSE {0}
#  define VERBOSE_LINE ""
#  define SOPT_VERBOSE ""
#endif

#define HELP_LINE \
	"\t--" OPT_HELP  ",-h       \tThis text.\n"
#define SOCK_LINE \
	"\t--" OPT_SOCK  ",-s <SOCK>\tPath to the control socket.\n"
#define BIND_LINE \
	 "\t--" OPT_BIND ",-b <ADDR>\tBind the local socket to a particular IP address.\n"


char * help_help_text;

#endif
