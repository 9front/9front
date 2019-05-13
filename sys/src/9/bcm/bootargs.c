#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#define BOOTARGS	((char*)CONFADDR)
#define BOOTARGSLEN	((KZERO+REBOOTADDR)-CONFADDR)

#define	MAXCONF 64
static char *confname[MAXCONF];
static char *confval[MAXCONF];
static int nconf;

typedef struct Atag Atag;
struct Atag {
	u32int	size;	/* size of atag in words, including this header */
	u32int	tag;	/* atag type */
	union {
		u32int	data[1];	/* actually [size-2] */
		/* AtagMem */
		struct {
			u32int	size;
			u32int	base;
		} mem;
		/* AtagCmdLine */
		char	cmdline[1];	/* actually [4*(size-2)] */
	};
};

enum {
	AtagNone	= 0x00000000,
	AtagCore	= 0x54410001,
	AtagMem		= 0x54410002,
	AtagCmdline	= 0x54410009,
};

static int
findconf(char *k)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], k) == 0)
			return i;
	return -1;
}

static void
addconf(char *k, char *v)
{
	int i;

	i = findconf(k);
	if(i < 0){
		if(nconf >= MAXCONF)
			return;
		i = nconf++;
		confname[i] = k;
	}
	confval[i] = v;
}

static void
plan9iniinit(char *s, int cmdline)
{
	char *toks[MAXCONF];
	int i, c, n;
	char *v;

	if((c = *s) < ' ' || c >= 0x80)
		return;
	if(cmdline)
		n = tokenize(s, toks, MAXCONF);
	else
		n = getfields(s, toks, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(toks[i][0] == '#')
			continue;
		v = strchr(toks[i], '=');
		if(v == nil)
			continue;
		*v++ = '\0';
		addconf(toks[i], v);
	}
}

void
bootargsinit(void)
{
	static char maxmem[11];
	char x, *e;
	Atag *a;

	e = BOOTARGS;
	a = (Atag*)e;
	if(a->tag != AtagCore){
		plan9iniinit(e, 0);
		return;
	}
	while(a->tag != AtagNone){
		e += a->size * sizeof(u32int);
		if(a->size < 2 || e < (char*)a || e > &BOOTARGS[BOOTARGSLEN])
			break;
		switch(a->tag){
		case AtagMem:
			if(findconf("*maxmem") < 0){
				snprint(maxmem, sizeof(maxmem), "%ud", a->mem.base+a->mem.size);
				addconf("*maxmem", maxmem);
			}
			break;
		case AtagCmdline:
			x = *e;
			*e = 0;
			plan9iniinit(a->cmdline, 1);
			*e = x;
			break;
		}
		if(e > &BOOTARGS[BOOTARGSLEN-8])
			break;
		a = (Atag*)e;
	}
}

char*
getconf(char *name)
{
	int i;

	if((i = findconf(name)) < 0)
		return nil;
	return confval[i];
}

void
setconfenv(void)
{
	int i;

	for(i = 0; i < nconf; i++){
		if(confname[i][0] != '*')
			ksetenv(confname[i], confval[i], 0);
		ksetenv(confname[i], confval[i], 1);
	}
}

void
writeconf(void)
{
	char *p, *q;
	int n;

	p = getconfenv();
	if(waserror()) {
		free(p);
		nexterror();
	}

	/* convert to name=value\n format */
	for(q=p; *q; q++) {
		q += strlen(q);
		*q = '=';
		q += strlen(q);
		*q = '\n';
	}
	n = q - p + 1;
	if(n >= BOOTARGSLEN)
		error("kernel configuration too large");
	memmove(BOOTARGS, p, n);
	memset(BOOTARGS+n, 0, BOOTARGSLEN-n);
	poperror();
	free(p);
}
