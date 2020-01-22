typedef char s8int;
typedef short s16int;
typedef int s32int;
typedef vlong s64int;

extern u32int r[16];
extern u32int ps;
extern u32int curpc;
extern int trace;

#define U32(x) ((x)[0] | (x)[1] << 8 | (x)[2] << 16 | (x)[3] << 24)

typedef struct Segment Segment;
typedef struct Chan Chan;

struct Segment {
	enum {
		SEGRO = 1,
	} flags;
	u32int start, size;
	u32int *data;
};

extern Segment segs[3];

enum {
	STACKSIZE = 16*1024*1024
};

enum {
	EPERM = 1,
	ENOENT = 2,
	EIO = 5,
	EBADF = 9,
	EINVAL = 22,
	EMFILE = 24,
	ENOTTY = 25,
};

struct Chan {
	int fd;
	enum {
		DONTCLOSE = 1,
		DIR = 2,
		FAKETTY = 4,
	} flags;
	char *buf, *bufp, *bufe;
};

enum { NCHANS = 128 };

enum {
	FLAGN = 8,
	FLAGZ = 4,
	FLAGV = 2,
	FLAGC = 1,
};
