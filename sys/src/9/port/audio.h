
typedef struct Audio Audio;
struct Audio {
	Audio *next;
	char *name;
	void *ctlr;
	void *mixer;
	void (*attach)(Audio *);
	long (*read)(Audio *, void *, long, vlong);
	long (*write)(Audio *, void *, long, vlong);
	long (*volread)(Audio *, void *, long, vlong);
	long (*volwrite)(Audio *, void *, long, vlong);
	void (*close)(Audio *);
	long (*ctl)(Audio *, void *, long, vlong);
	long (*status)(Audio *, void *, long, vlong);
	long (*buffered)(Audio *);
	int ctlrno;
};

void addaudiocard(char *name, int (*probefn)(Audio *));
void ac97mixreset(Audio *, void (*wr)(Audio*,int,ushort), ushort (*rr)(Audio*,int));
int ac97hardrate(Audio *adev, int rate);
