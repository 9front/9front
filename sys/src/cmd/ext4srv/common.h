typedef struct Opts Opts;
typedef struct Part Part;

struct Opts {
	char *group;
	int cachewb;
	int asroot;
	int rdonly;

	int fstype;
	int blksz;
	int inodesz;
	u32int ninode;
	char *label;
};

struct Part {
	Ref;
	QLock;
	Part *prev, *next;

	char *partdev;

	struct ext4_mountpoint mp;
	struct ext4_blockdev bdev;
	struct ext4_blockdev_iface bdif;
	struct ext4_sblock *sb;
	struct ext4_lock oslocks;
	Qid qid;
	Qid qidmask;
	Groups groups;
	int f;
	uchar blkbuf[];
};

Part *openpart(char *dev, Opts *opts);
void closepart(Part *p);
void closeallparts(void);
void statallparts(void);
void syncallparts(void);
