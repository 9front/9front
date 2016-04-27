#pragma src "/sys/src/libregexp"
#pragma lib "libregexp.a"
enum
{
	OANY = 0,
	OBOL,
	OCLASS,
	OEOL,
	OJMP,
	ONOTNL,
	ORUNE,
	OSAVE,
	OSPLIT,
	OUNSAVE,
};

typedef struct Resub Resub;
typedef struct Reinst Reinst;
typedef struct Reprog Reprog;
typedef struct Rethread Rethread;

#pragma incomplete Reinst
#pragma incomplete Rethread

struct Resub
{
	union
	{
		char *sp;
		Rune *rsp;
	};
	union
	{
		char *ep;
		Rune *rep;
	};
};
struct Reprog
{
	Reinst *startinst;
	Rethread *threads;
	char *regstr;
	int len;
	int nthr;
};

Reprog*	regcomp(char*);
Reprog*	regcomplit(char*);
Reprog*	regcompnl(char*);
void	regerror(char*);
int	regexec(Reprog*, char*, Resub*, int);
void	regsub(char*, char*, int, Resub*, int);
int	rregexec(Reprog*, Rune*, Resub*, int);
void	rregsub(Rune*, Rune*, int, Resub*, int);
int	reprogfmt(Fmt *);
