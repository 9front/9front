#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#define	MAXCONF 64
static char *confname[MAXCONF];
static char *confval[MAXCONF];
static int nconf;
static char maxmem[256];
static int cpus;
static char ncpu[256];

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

typedef struct Devtree Devtree;
struct Devtree
{
	uchar	*base;
	uchar	*end;
	char	*stab;
	char	path[1024];
};

enum {
	DtHeader	= 0xd00dfeed,
	DtBeginNode	= 1,
	DtEndNode	= 2,
	DtProp		= 3,
	DtEnd		= 9,
};

static u32int
beget4(uchar *p)
{
	return (u32int)p[0]<<24 | (u32int)p[1]<<16 | (u32int)p[2]<<8 | (u32int)p[3];
}

static void
devtreeprop(char *path, char *key, void *val, int len)
{
	uvlong addr;
	uchar *p = val;

	if((strncmp(path, "/memory", 7) == 0 || strncmp(path, "/memory@0", 9) == 0)
	&& strcmp(key, "reg") == 0){
		if(findconf("*maxmem") < 0 && len == 16){
			p += 4;	/* ignore */
			addr = (uvlong)beget4(p+4)<<32 | beget4(p);
			addr += beget4(p+8);
			snprint(maxmem, sizeof(maxmem), "%#llux", addr);
			addconf("*maxmem", maxmem);
		}
		return;
	}
	if(strncmp(path, "/cpus/cpu", 9) == 0 && strcmp(key, "reg") == 0){
		cpus++;
		return;
	}
	if(strncmp(path, "/chosen", 7) == 0 && strcmp(key, "bootargs") == 0){
		if(len > BOOTARGSLEN)
			len = BOOTARGSLEN;
		memmove(BOOTARGS, val, len);
		plan9iniinit(BOOTARGS, 1);
		return;
	}
}

static uchar*
devtreenode(Devtree *t, uchar *p, char *cp)
{
	uchar *e = (uchar*)t->stab;
	char *s;
	int n;

	if(p+4 > e || beget4(p) != DtBeginNode)
		return nil;
	p += 4;
	if((s = memchr((char*)p, 0, e - p)) == nil)
		return nil;
	n = s - (char*)p;
	cp += n;
	if(cp >= &t->path[sizeof(t->path)])
		return nil;
	memmove(cp - n, (char*)p, n);
	*cp = 0;
	p += (n + 4) & ~3;
	while(p+12 <= e && beget4(p) == DtProp){
		n = beget4(p+4);
		if(p + 12 + n > e)
			return nil;
		s = t->stab + beget4(p+8);
		if(s < t->stab || s >= (char*)t->end
		|| memchr(s, 0, (char*)t->end - s) == nil)
			return nil;
		devtreeprop(t->path, s, p+12, n);
		p += 12 + ((n + 3) & ~3);
	}
	while(p+4 <= e && beget4(p) == DtBeginNode){
		*cp = '/';
		p = devtreenode(t, p, cp+1);
		if(p == nil)
			return nil;
	}
	if(p+4 > e || beget4(p) != DtEndNode)
		return nil;
	return p+4;
}

static int
parsedevtree(uchar *base, uintptr len)
{
	Devtree t[1];
	u32int total;

	if(len < 28 || beget4(base) != DtHeader)
		return -1;
	total = beget4(base+4);
	if(total < 28 || total > len)
		return -1;
	t->base = base;
	t->end = t->base + total;
	t->stab = (char*)base + beget4(base+12);
	if(t->stab >= (char*)t->end)
		return -1;
	devtreenode(t, base + beget4(base+8), t->path);
	return  0;
}

void
bootargsinit(void)
{
	void *va = KADDR(DTBADDR);
	uintptr len = cankaddr(DTBADDR);

	plan9iniinit(BOOTARGS, 0);
	if(parsedevtree(va, len) == 0){
		/* user can provide fewer ncpu */
		if(findconf("*ncpu") < 0){
			snprint(ncpu, sizeof(ncpu), "%d", cpus);
			addconf("*ncpu", ncpu);
		}
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

	if(nconf < 0){
		/* use defaults when there was no configuration */
		ksetenv("console", "0", 1);
		return;
	}

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
