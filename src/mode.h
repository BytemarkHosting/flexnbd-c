#ifndef MODE_H
#define MODE_H


void mode(char* mode, int argc, char **argv);


#include <getopt.h>

#define GETOPT_ARG(x,s)  {(x), 1, 0, (s)}
#define GETOPT_FLAG(x,v) {(x), 0, 0, (v)}

#define OPT_HELP "help"
#define OPT_ADDR "addr"
#define OPT_BIND "bind"
#define OPT_PORT "port"
#define OPT_FILE "file"
#define OPT_SOCK "sock"
#define OPT_FROM "from"
#define OPT_SIZE "size"
#define OPT_DENY "default-deny"
#define OPT_UNLINK "unlink"
#define OPT_CONNECT_ADDR "conn-addr"
#define OPT_CONNECT_PORT "conn-port"
#define OPT_KILLSWITCH "killswitch"

#define CMD_SERVE  "serve"
#define CMD_LISTEN "listen"
#define CMD_READ   "read"
#define CMD_WRITE  "write"
#define CMD_ACL    "acl"
#define CMD_MIRROR "mirror"
#define CMD_BREAK  "break"
#define CMD_STATUS "status"
#define CMD_HELP   "help"
#define LEN_CMD_MAX 7

#define PATH_LEN_MAX 1024
#define ADDR_LEN_MAX 64


#define IS_CMD(x,c) (strncmp((x),(c),(LEN_CMD_MAX)) == 0)

#define GETOPT_HELP GETOPT_FLAG( OPT_HELP, 'h' )
#define GETOPT_DENY GETOPT_FLAG( OPT_DENY, 'd' )
#define GETOPT_ADDR GETOPT_ARG( OPT_ADDR, 'l' )
#define GETOPT_PORT GETOPT_ARG( OPT_PORT, 'p' )
#define GETOPT_FILE GETOPT_ARG( OPT_FILE, 'f' )
#define GETOPT_SOCK GETOPT_ARG( OPT_SOCK, 's' )
#define GETOPT_FROM GETOPT_ARG( OPT_FROM, 'F' )
#define GETOPT_SIZE GETOPT_ARG( OPT_SIZE, 'S' )
#define GETOPT_BIND GETOPT_ARG( OPT_BIND, 'b' )
#define GETOPT_UNLINK GETOPT_ARG( OPT_UNLINK, 'u' )
#define GETOPT_CONNECT_ADDR GETOPT_ARG( OPT_CONNECT_ADDR, 'C' )
#define GETOPT_CONNECT_PORT GETOPT_ARG( OPT_CONNECT_PORT, 'P' )
#define GETOPT_KILLSWITCH   GETOPT_ARG( OPT_KILLSWITCH,   'k' )

#define OPT_VERBOSE "verbose"
#define SOPT_VERBOSE "v"
#define GETOPT_VERBOSE GETOPT_FLAG( OPT_VERBOSE, 'v' )
#define VERBOSE_LINE \
  "\t--" OPT_VERBOSE ",-" SOPT_VERBOSE "\t\tOutput debug information.\n"

#ifdef DEBUG
# define VERBOSE_LOG_LEVEL 0
#else
# define VERBOSE_LOG_LEVEL 1
#endif

#define QUIET_LOG_LEVEL 4

#define OPT_QUIET "quiet"
#define SOPT_QUIET "q"
#define GETOPT_QUIET GETOPT_FLAG( OPT_QUIET, 'q' )
#define QUIET_LINE \
	"\t--" OPT_QUIET ",-" SOPT_QUIET "\t\tOutput only fatal information.\n"


#define HELP_LINE \
	"\t--" OPT_HELP  ",-h       \tThis text.\n"
#define SOCK_LINE \
	"\t--" OPT_SOCK  ",-s <SOCK>\tPath to the control socket.\n"
#define BIND_LINE \
	 "\t--" OPT_BIND ",-b <BIND-ADDR>\tBind the local socket to a particular IP address.\n"


char * help_help_text;

#endif

