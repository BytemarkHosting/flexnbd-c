#define OPTIONS_H

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

#define CMD_SERVE  "serve"
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
#define GETOPT_PORT GETOPT_ARG( OPT_PORT, 'p' )
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


static struct option serve_options[] = {
	GETOPT_HELP,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_FILE,
	GETOPT_SOCK,
	GETOPT_DENY,
	GETOPT_VERBOSE,
	{0}
};
static char serve_short_options[] = "hl:p:f:s:d" SOPT_VERBOSE;
static char serve_help_text[] =
	"Usage: flexnbd " CMD_SERVE " <options> [<acl address>*]\n\n"
	"Serve FILE from ADDR:PORT, with an optional control socket at SOCK.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to serve on.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to serve on.\n"
	"\t--" OPT_FILE ",-f <FILE>\tThe file to serve.\n"
	"\t--" OPT_DENY ",-d\tDeny connections by default unless in ACL.\n"
	SOCK_LINE
	VERBOSE_LINE;

static struct option read_options[] = {
	GETOPT_HELP,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_FROM,
	GETOPT_SIZE,
	GETOPT_BIND,
	GETOPT_VERBOSE,
	{0}
};
static char read_short_options[] = "hl:p:F:S:b:" SOPT_VERBOSE;
static char read_help_text[] =
	"Usage: flexnbd " CMD_READ " <options>\n\n"
	"Read SIZE bytes from a server at ADDR:PORT to stdout, starting at OFFSET.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to read from.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to read from.\n"
	"\t--" OPT_FROM ",-F <OFFSET>\tByte offset to read from.\n"
	"\t--" OPT_SIZE ",-S <SIZE>\tBytes to read.\n"
	BIND_LINE
	VERBOSE_LINE;


static struct option *write_options = read_options;
static char *write_short_options = read_short_options;
static char write_help_text[] =
	"Usage: flexnbd " CMD_WRITE" <options>\n\n"
	"Write SIZE bytes from stdin to a server at ADDR:PORT, starting at OFFSET.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to write to.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to write to.\n"
	"\t--" OPT_FROM ",-F <OFFSET>\tByte offset to write from.\n"
	"\t--" OPT_SIZE ",-S <SIZE>\tBytes to write.\n"
	BIND_LINE
	VERBOSE_LINE;

struct option acl_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_VERBOSE,
	{0}
};
static char acl_short_options[] = "hs:" SOPT_VERBOSE;
static char acl_help_text[] =
	"Usage: flexnbd " CMD_ACL " <options> [<acl address>+]\n\n"
	"Set the access control list for a server with control socket SOCK.\n\n"
	HELP_LINE
	SOCK_LINE
	VERBOSE_LINE;

struct option mirror_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_BIND,
	GETOPT_VERBOSE,
	{0}
};
static char mirror_short_options[] = "hs:l:p:b:" SOPT_VERBOSE;
static char mirror_help_text[] =
	"Usage: flexnbd " CMD_MIRROR " <options>\n\n"
	"Start mirroring from the server with control socket SOCK to one at ADDR:PORT.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to mirror to.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to mirror to.\n"
	SOCK_LINE
	BIND_LINE
	VERBOSE_LINE;


struct option status_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_VERBOSE,
	{0}
};
static char status_short_options[] = "hs:" SOPT_VERBOSE;
static char status_help_text[] =
	"Usage: flexnbd " CMD_STATUS " <options>\n\n"
	"Get the status for a server with control socket SOCK.\n\n"
	HELP_LINE
	SOCK_LINE
	VERBOSE_LINE;

static char help_help_text[] =
	"Usage: flexnbd <cmd> [cmd options]\n\n"
	"Commands:\n"
	"\tflexnbd serve\n"
	"\tflexnbd read\n"
	"\tflexnbd write\n"
	"\tflexnbd acl\n"
	"\tflexnbd mirror\n"
	"\tflexnbd status\n"
	"\tflexnbd help\n\n"
	"See flexnbd help <cmd> for further info\n";

