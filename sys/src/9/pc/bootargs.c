#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#define	MAXCONF 64
static char *confname[MAXCONF];
static char *confval[MAXCONF];
static int nconf;

/* screen.c */
extern char* rgbmask2chan(char *buf, int depth, u32int rm, u32int gm, u32int bm);

/* vgavesa.c */
extern char* vesabootscreenconf(char*, char*, uchar*);

static void
multibootargs(void)
{
	extern ulong multibootptr;
	ulong *multiboot;
	char *cp, *ep;
	ulong *m, l;

	if(multibootptr == 0)
		return;

	multiboot = (ulong*)KADDR(multibootptr);

	cp = BOOTARGS;
	ep = cp + BOOTARGSLEN-1;

	/* memory map */
	if((multiboot[0] & (1<<6)) != 0 && (l = multiboot[11]) >= 24){
		cp = seprint(cp, ep, "*e820=");
		m = KADDR(multiboot[12]);
		while(m[0] >= 20 && m[0]+4 <= l){
			uvlong base, size;
			m++;
			base = ((uvlong)m[0] | (uvlong)m[1]<<32);
			size = ((uvlong)m[2] | (uvlong)m[3]<<32);
			cp = seprint(cp, ep, "%.1lux %.16llux %.16llux ",
				m[4] & 0xF, base, base+size);
			l -= m[-1]+4;
			m = (ulong*)((uintptr)m + m[-1]);
		}
		cp[-1] = '\n';
	}

	if((multiboot[0] & (1<<12)) != 0 && multiboot[22] != 0){	/* framebuffer */
		uchar *p = (uchar*)multiboot + 112;
		int depth = multiboot[27] & 0xFF;
		char chan[32];

		switch((multiboot[27]>>8) & 0xFF){
		case 0:
			snprint(chan, sizeof chan, "m%d", depth);
			if(0){
		case 1:
			rgbmask2chan(chan, depth,
				(1UL<<p[1])-1 << p[0],
				(1UL<<p[3])-1 << p[2],
				(1UL<<p[5])-1 << p[4]);
			}
			cp = seprint(cp, ep, "*bootscreen=%dx%dx%d %s %#lux\n",
				(int)multiboot[24]*8 / depth,
				(int)multiboot[26],
				depth,
				chan,
				multiboot[22]);
		}
	} else
	if((multiboot[0] & (1<<11)) != 0 && multiboot[19] != 0)		/* vbe mode info */
		cp = vesabootscreenconf(cp, ep, KADDR(multiboot[19]));

	/* plan9.ini passed as the first module */
	if((multiboot[0] & (1<<3)) != 0 && multiboot[5] > 0 && multiboot[6] != 0){
		m = KADDR(multiboot[6]);
		cp = seprint(cp, ep, "%.*s\n", (int)(m[1] - m[0]), (char*)KADDR(m[0]));
	}

	/* command line */
	if((multiboot[0] & (1<<2)) != 0 && multiboot[4] != 0){
		int i, n = tokenize(KADDR(multiboot[4]), confval, MAXCONF);
		for(i=0; i<n; i++)
			cp = seprint(cp, ep, "%s\n", confval[i]);
	}

	*cp = 0;
}

void
bootargsinit(void)
{
	int i, j, n;
	char *cp, *line[MAXCONF], *p, *q;

	multibootargs();

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;	/* where b.com leaves its config */
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getfields(cp, line, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		for(j = 0; j < nconf; j++){
			if(cistrcmp(confname[j], line[i]) == 0)
				break;
		}
		confname[j] = line[i];
		confval[j] = cp;
		if(j == nconf)
			nconf++;
	}
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
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
	memset(BOOTLINE, 0, BOOTLINELEN);
	poperror();
	free(p);
}
