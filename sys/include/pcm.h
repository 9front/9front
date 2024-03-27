#pragma	lib	"libpcm.a"
#pragma	src	"/sys/src/libpcm"

typedef struct Pcmconv Pcmconv;
typedef struct Pcmdesc Pcmdesc;

#pragma incomplete Pcmconv

struct Pcmdesc
{
	int	rate;
	int	channels;
	int	framesz;
	int	abits;	/* bits after input conversion */
	int	bits;	/* bits in input stream per sample */
	Rune	fmt;
};

extern	Pcmdesc	pcmdescdef; /* s16c2r44100 */

int	pcmdescfmt(Fmt*);
int	mkpcmdesc(char *f, Pcmdesc *d);

Pcmconv	*allocpcmconv(Pcmdesc *in, Pcmdesc *out);
void	freepcmconv(Pcmconv *c);
int	pcmconv(Pcmconv *c, void *in0, void *out0, int insz);
int	pcmratio(Pcmconv *c, int insz);
