typedef struct Rop Rop;
typedef struct Req Req;
typedef struct Trans Trans;

typedef struct Share Share;
typedef struct File File;
typedef struct Find Find;
typedef struct Tree Tree;
typedef struct Idmap Idmap;

#pragma incomplete Idmap

struct Rop
{
	int (*strpack)(uchar *, uchar *, uchar *, void *);
	int (*strunpack)(uchar *, uchar *, uchar *, void *);
	int (*namepack)(uchar *, uchar *, uchar *, void *);
	int (*nameunpack)(uchar *, uchar *, uchar *, void *);
	int (*untermstrpack)(uchar *, uchar *, uchar *, void *);
	int (*untermnamepack)(uchar *, uchar *, uchar *, void *);
};

struct Req
{
	int cmd;
	int tid;
	int pid;
	int uid;
	int mid;
	int flags;
	int flags2;

	uchar sig[8];

	uchar *lh, *rh, *rp, *re;

	Rop *o;
	char *name;
	void (*respond)(Req *r, int err);
	int (*namecmp)(char *, char *);
};

struct Trans
{
	int cmd;
	int flags;

	struct {
		struct {
			uchar *b, *p, *e;
		} param, data, setup;
	} in, out;

	Req *r;
	Rop *o;
	char *name;
	void (*respond)(Trans *t, int err);
	int (*namecmp)(char *, char *);
};

struct File
{
	int ref;
	int fd;
	int rtype;
	int dacc;
	char *path;
	void *aux;
};

struct Find
{
	int ref;
	int attr;
	char *base;
	char *pattern;
	int casesensitive;
	int index;
	Dir *dotdot;
	Dir *dot;
	Dir *dir;
	int ndir;
};

struct Share
{
	Share *next;

	char *service;
	int stype;

	char *name;
	char *root;
	char *remark;

	char *fsname;
	int namelen;
	vlong allocsize;
	vlong freesize;
	int sectorsize;
	int blocksize;

	Idmap *users;
	Idmap *groups;
};

struct Tree
{
	int tid;

	void **file;
	int nfile;

	void **find;
	int nfind;

	Share *share;
};

int debug;
int trspaces;
int needauth;
char *domain;
char *progname;
char *osname;

char *remotesys;
char *remoteuser;
int remotebuffersize;

long starttime;
int tzoff;

enum
{
	BUFFERSIZE = 0x8000,

	STATUS_INVALID_SMB				= 0x00010002,
	STATUS_SMB_BAD_TID				= 0x00050002,
	STATUS_SMB_BAD_FID				= 0x00060001,
	STATUS_OS2_INVALID_ACCESS		= 0x000C0001,
	STATUS_SMB_BAD_UID				= 0x005B0002,
	STATUS_OS2_INVALID_LEVEL  		= 0x007C0001,
	STATUS_NO_MORE_FILES			= 0x80000006,
	STATUS_INVALID_HANDLE			= 0xC0000008,
	STATUS_NO_SUCH_FILE				= 0xC000000F,
	STATUS_ACCESS_DENIED			= 0xC0000022,
	STATUS_OBJECT_NAME_NOT_FOUND	= 0xC0000034,
	STATUS_OBJECT_NAME_COLLISION	= 0xC0000035,
	STATUS_OBJECT_PATH_INVALID		= 0xC0000039,
	STATUS_OBJECT_PATH_NOT_FOUND	= 0xC000003A,
	STATUS_OBJECT_PATH_SYNTAX_BAD	= 0xC000003B,
	STATUS_SHARING_VIOLATION		= 0xC0000043,
	STATUS_LOGON_FAILURE			= 0xC000006D,
	STATUS_FILE_IS_A_DIRECTORY		= 0xC00000BA,
	STATUS_NOT_SUPPORTED			= 0xC00000BB,
	STATUS_BAD_DEVICE_TYPE			= 0xC00000CB,
	STATUS_BAD_NETWORK_NAME 		= 0xC00000CC,
	STATUS_NOT_SAME_DEVICE			= 0xC00000D4,
	STATUS_DIRECTORY_NOT_EMPTY		= 0xC0000101,

	/* resource type */
	FileTypeDisk = 0,

	/* stype */
	STYPE_DISKTREE = 0,
	STYPE_PRINTQ = 1,
	STYPE_DEVICE = 2,
	STYPE_IPC = 3,

	/* capabilities */
	CAP_UNICODE = 0x4,
	CAP_LARGEFILES = 0x8,
	CAP_NT_SMBS = 0x10,
	CAP_NT_STATUS = 0x40,
	CAP_NT_FIND = 0x200,
	CAP_UNIX = 0x800000,

	/* extended file attributes */
	ATTR_READONLY = 0x1,
	ATTR_HIDDEN = 0x2,
	ATTR_SYSTEM = 0x4,
	ATTR_DIRECTORY = 0x10,
	ATTR_ARCHIVE = 0x20,
	ATTR_NORMAL = 0x80,

	DOSMASK	 = 0x37,

	/* access */
	FILE_READ_DATA = 0x1,
	FILE_WRITE_DATA = 0x2,
	FILE_APPEND_DATA = 0x4,
	FILE_EXECUTE = 0x20,
	FILE_DELETE = 0x10000,
	GENERIC_ALL = 0x10000000,
	GENERIC_EXECUTE = 0x20000000,
	GENERIC_WRITE = 0x40000000,
	GENERIC_READ = 0x80000000,

	READMASK =
		FILE_READ_DATA |
		FILE_EXECUTE |
		GENERIC_ALL |
		GENERIC_EXECUTE |
		GENERIC_READ,

	WRITEMASK = 
		FILE_WRITE_DATA |
		FILE_APPEND_DATA |
		GENERIC_ALL |
		GENERIC_WRITE,

	/* share access */
	FILE_SHARE_NONE = 0,
	FILE_SHARE_READ = 1,
	FILE_SHARE_WRITE = 2,
	FILE_SHARE_DELETE = 4,
	FILE_SHARE_COMPAT = -1,

	/* createdisposition */
	FILE_SUPERSEDE = 0,
	FILE_OPEN,
	FILE_CREATE,
	FILE_OPEN_IF,
	FILE_OVERWRITE,
	FILE_OVERWRITE_IF,

	/* createaction */
	FILE_SUPERSEDED = 0,
	FILE_OPEND,
	FILE_CREATED,
	FILE_OVERWRITTEN,

	/* createoptions */
	FILE_DIRECTORY_FILE = 0x1,
	FILE_NON_DIRECTORY_FILE = 0x40,
	FILE_DELETE_ON_CLOSE = 0x1000,
	FILE_OPEN_BY_FILE_ID = 0x2000,
};
