typedef struct Audio Audio;
typedef struct Volume Volume;

struct Audio
{
	char *name;

	void *ctlr;
	void *mixer;

	Ref audioopenr;
	Ref audioopenw;

	long (*read)(Audio *, void *, long, vlong);
	long (*write)(Audio *, void *, long, vlong);
	void (*close)(Audio *, int);

	long (*volread)(Audio *, void *, long, vlong);
	long (*volwrite)(Audio *, void *, long, vlong);

	long (*ctl)(Audio *, void *, long, vlong);
	long (*status)(Audio *, void *, long, vlong);
	long (*buffered)(Audio *);

	int delay;
	int speed;

	int ctlrno;
	Audio *next;
};

enum {
	Left,
	Right,
	Stereo,
	Absolute,
};

#define Mono Left

struct Volume
{
	char *name;
	int reg;
	int range;
	int type;
	int cap;
};

extern void addaudiocard(char *, int (*)(Audio *));
extern long genaudiovolread(Audio *adev, void *a, long n, vlong off,
	Volume *vol, int (*volget)(Audio *, int, int *),
	ulong caps);
extern long genaudiovolwrite(Audio *adev, void *a, long n, vlong off,
	Volume *vol, int (*volset)(Audio *, int, int *),
	ulong caps);
