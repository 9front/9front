typedef struct Msg Msg;
typedef struct Track Track;
typedef struct Chan Chan;
typedef struct Inst Inst;

enum{
	Rate = 44100,
	Ninst = 128 + 81-35+1,
	Nchan = 16,
	Percch = 9,
};

struct Msg{
	int type;
	Chan *chan;
	int arg1;
	int arg2;
	usize size;
};
struct Track{
	uchar *buf;
	usize bufsz;
	uchar *cur;
	uchar *run;
	double Δ;
	double t;
	int latch;
	int ended;
	void *aux;
};
extern Track *tracks;

#pragma incomplete Inst
struct Chan{
	Inst *i;
	int vol;
	int bend;
	int pan;
};
extern Chan chan[16];
extern Inst *inst;

enum{
	Cnoteoff,
	Cnoteon,
	Cbankmsb,
	Cchanvol,
	Cpan,
	Cprogram,
	Cpitchbend,
	Ceot,
	Ctempo,
	Ckeyafter,
	Cchanafter,
	Csysex,
	Cunknown,
};

extern int mfmt, ntrk, div, tempo;
extern int trace, stream;
extern vlong tic;
extern int samprate;
extern Biobuf *inbf, *outbf;

void*	emalloc(ulong);
void	dprint(char*, ...);
int	readmid(char*);
void	writemid(char*);
u32int	peekvar(Track*);
void	translate(Track*, int, Msg*);
int	nextev(Track*);
void	evloop(void);
double	delay(double);
vlong	ns2tic(double);
void	initmid(void);

Biobuf*	eopen(char*, int);
Biobuf*	efdopen(int, int);
u8int	get8(Track*);
u16int	get16(Track*);
u32int	get32(Track*);

/* application-defined */
void	event(Track*);
void	samp(double);

#pragma	varargck	argpos	dprint	1
