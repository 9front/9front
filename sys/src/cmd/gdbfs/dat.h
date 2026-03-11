int debug;
void dbg(char *fmt, ...);

enum {
	/* counting the terminating NUL is intentional */
	Minwrite = sizeof "M0000000000000000,00000000:#00",
};

enum state {
	Running = 1,
	Stopped,
	Shutdown,
};

struct
{
	int tid;
	QLock;	/* guards state transitions */
	int state;
	int pktlen;
	int wfd;
	Biobuf *rb;
	Channel *c;
} gdb;

void gdbinit(int rfd, int wfd);
void gdbshutdown(void);

void gdbreadmem(Req *r);
void gdbwritemem(Req *r);
void gdbreadreg(Req *r);
void gdbwritereg(Req *r);
void gdbstart(Req *r);
void gdbwaitstop(Req *r);
void gdbstartstop(Req *r);
void gdbstop(Req *r);
