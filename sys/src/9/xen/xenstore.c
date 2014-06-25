#include <u.h>
#include <libc.h>

typedef ulong uint32_t;

enum xsd_sockmsg_type
{
    XS_DEBUG,
    XS_DIRECTORY,
    XS_READ,
    XS_GET_PERMS,
    XS_WATCH,
    XS_UNWATCH,
    XS_TRANSACTION_START,
    XS_TRANSACTION_END,
    XS_INTRODUCE,
    XS_RELEASE,
    XS_GET_DOMAIN_PATH,
    XS_WRITE,
    XS_MKDIR,
    XS_RM,
    XS_SET_PERMS,
    XS_WATCH_EVENT,
    XS_ERROR,
    XS_IS_DOMAIN_INTRODUCED
};

struct xsd_sockmsg
{
    uint32_t type;  /* XS_??? */
    uint32_t req_id;/* Request identifier, echoed in daemon's response.  */
    uint32_t tx_id; /* Transaction id (0 if not related to a transaction). */
    uint32_t len;   /* Length of data following this. */

    /* Generally followed by nul-terminated string(s). */
};

char*
xscmd(int fd, enum xsd_sockmsg_type cmd, char *s, char *val)
{
	static char buf[512];
	struct xsd_sockmsg *msg;
	char *arg;
	static ulong reqid = 1;
	int n;

	msg = (struct xsd_sockmsg*)buf;
	arg = buf + sizeof(*msg);
	if(cmd != XS_WATCH_EVENT){
		msg->type = cmd;
		msg->req_id = reqid++;
		msg->tx_id = 0;
		msg->len = strlen(s)+1;
		if (val != 0) {
			msg->len += strlen(val);
			if (msg->type == XS_WATCH)
				msg->len++;
		}
		strcpy(arg, s);
		if (val != 0)
			strcpy(arg+strlen(s)+1, val);
		if (write(fd, buf, sizeof(*msg)+msg->len) < 0)
			sysfatal("write: %r");
	}
	if ((n = read(fd, buf, sizeof(*msg))) != sizeof(*msg))
		sysfatal("read hdr %d: %r", n);
	fprint(2, "type %lud req_id %lud len %lud\n", msg->type, msg->req_id, msg->len);
	if ((n = read(fd, arg, msg->len)) != msg->len)
		sysfatal("read data %d: %r", n);
	if (cmd == XS_DIRECTORY || cmd == XS_WATCH_EVENT) {
		for (s = arg; s < arg+msg->len; s++) {
			if (*s == 0) *s = ',';
			else if (*s < 32) *s += '0';
		}
	}
	arg[msg->len] = 0;
	return arg;
}

void
usage(void)
{
	sysfatal("Usage: xenstore [lrwdme] path [value]\n");
}

void
main(int argc, char *argv[])
{
	int fd;

	if (argc != 3 && argc != 4)
		usage();
	if(access("/dev/xenstore", AEXIST) < 0)
		bind("#x", "/dev", MAFTER);
	fd = open("/dev/xenstore", ORDWR);
	if (fd < 0)
		sysfatal("/dev/xenstore: %r");
	switch (argv[1][0]) {
	default:
		usage();
		break;
	case 'r':
		print("%s\n", xscmd(fd, XS_READ, argv[2], 0));
		break;
	case 'l':
		print("%s\n", xscmd(fd, XS_DIRECTORY, argv[2], 0));
		break;
	case 'm':
		print("%s\n", xscmd(fd, XS_MKDIR, argv[2], 0));
		break;
	case 'd':
		print("%s\n", xscmd(fd, XS_RM, argv[2], 0));
		break;
	case 'w':
		if (argc != 4)
			usage();
		print("%s\n", xscmd(fd, XS_WRITE, argv[2], argv[3]));
		break;
	case 'e':
		if (argc != 4)
			usage();
		print("%s\n", xscmd(fd, XS_WATCH, argv[2], argv[3]));
		close(fd);
		fd = open("/dev/xenwatch", OREAD);
		if (fd < 0)
			sysfatal("/dev/xenwatch: %r");
		for (;;)
			print("%s\n", xscmd(fd, XS_WATCH_EVENT, 0, 0));
	}
}
