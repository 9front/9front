#pragma lib "libgio.a"
#pragma src "/sys/src/libgio"

typedef struct ReadWriter ReadWriter;

struct ReadWriter {
	RWLock;
	int (*open)(ReadWriter*);
	int (*close)(ReadWriter*);
	long (*pread)(ReadWriter*, void*, long, vlong);
	long (*pwrite)(ReadWriter*, void*, long, vlong);
	void *aux;
	u64int offset;
	u64int length;
};

ReadWriter* getrdstruct(int);
int gopen(ReadWriter*, void*);
int gclose(int);
long gread(int, void*, long, vlong);
long gwrite(int, void*, long, vlong);
vlong gseek(int, vlong, int);
int fd2gio(int);

