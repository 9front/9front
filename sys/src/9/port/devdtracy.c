#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<dtracy.h>

static Lock *machlocks;

typedef struct DTKChan DTKChan;
typedef struct DTKAux DTKAux;
QLock dtracylock;

struct DTKChan {
	DTChan *ch;
	int ref;
	int idx;
};
struct DTKAux {
	char *str;
};

static void
prog(DTKChan *p, char *s)
{
	DTClause *c;
	int rc;

	dtcrun(p->ch, DTCSTOP);
	dtcreset(p->ch);
	while(*s != 0){
		s = dtclunpack(s, &c);
		if(s == nil)
			error("invalid program");
		rc = dtcaddcl(p->ch, c);
		dtclfree(c);
		if(rc < 0){
			dtcreset(p->ch);
			error(up->syserrstr);
		}
	}
}

enum {
	/* Qdir */
	Qclone = 1,
	Qprobes = 2,
};

enum {
	Qdir,
	Qctl,
	Qprog,
	Qbuf,
	Qepid,
	Qaggbuf,
};

static Dirtab dtracydir[] = {
	"ctl",	{ Qctl, 0, 0 }, 0,	0660,
	"prog", { Qprog, 0, 0 }, 0,	0660,
	"buf",	{ Qbuf, 0, 0, }, 0,	0440,
	"epid",	{ Qepid, 0, 0 }, 0,	0440,
	"aggbuf",	{ Qaggbuf, 0, 0 }, 0,	0440,
};

enum {
	CMstop,
	CMgo,
};

static Cmdtab dtracyctlmsg[] = {
	CMstop,		"stop",		1,
	CMgo,		"go",		1,
};

static DTKChan **dtktab;
static int ndtktab;

static DTKChan *
dtklook(vlong n)
{
	if((uvlong)n >= ndtktab) return nil;
	return dtktab[n];
}
#define QIDPATH(q,e) ((q) + 1 << 8 | (e))
#define SLOT(q) ((vlong)((q).path >> 8) - 1)
#define FILE(q) ((int)(q).path & 0xff)

static DTKChan *
dtknew(void)
{
	DTKChan *p;
	DTKChan **newtab;
	int i;
	
	p = malloc(sizeof(DTKChan));
	if(p == nil) error(Enomem);
	for(i = 0; i < ndtktab; i++)
		if(dtktab[i] == nil){
			dtktab[i] = p;
			p->idx = i;
			break;
		}
	if(i == ndtktab){
		newtab = realloc(dtktab, (ndtktab + 1) * sizeof(DTKChan *));
		if(newtab == nil) error(Enomem);
		dtktab = newtab;
		dtktab[ndtktab] = p;
		p->idx = ndtktab++;
	}
	p->ch = dtcnew();
	return p;
}

static void
dtkfree(DTKChan *p)
{
	int idx;
	
	idx = p->idx;
	dtcfree(p->ch);
	free(p);
	dtktab[idx] = nil;
}

static int dtracyen;

static void
dtracyinit(void)
{
	dtracyen = getconf("*dtracy") != nil;
	if(!dtracyen) return;
	machlocks = smalloc(sizeof(Lock) * conf.nmach);
	dtinit(conf.nmach);
}

static Chan*
dtracyattach(char *spec)
{
	if(!dtracyen)
		error("*dtracy= not set");
	return devattach(L'Δ', spec);
}

static int
dtracygen(Chan *c, char *, Dirtab *, int, int s, Dir *dp)
{
	Dirtab *tab;
	uvlong path;

	if(s == DEVDOTDOT){
		devdir(c, (Qid){Qdir, 0, QTDIR}, "#Δ", 0, eve, 0555, dp);
		return 1;
	}
	if(c->qid.path == Qdir){
		if(s-- == 0) goto clone;
		if(s-- == 0) goto probes;
		if(s >= ndtktab) return -1;
		if(dtklook(s) == nil) return 0;
		sprint(up->genbuf, "%d", s);
		devdir(c, (Qid){QIDPATH(s, Qdir), 0, QTDIR}, up->genbuf, 0, eve, DMDIR|0555, dp);
		return 1;
	}
	if(c->qid.path == Qclone){
	clone:
		strcpy(up->genbuf, "clone");
		devdir(c, (Qid){Qclone, 0, QTFILE}, up->genbuf, 0, eve, 0444, dp);
		return 1;
	}
	if(c->qid.path == Qprobes){
	probes:
		strcpy(up->genbuf, "probes");
		devdir(c, (Qid){Qprobes, 0, QTFILE}, up->genbuf, 0, eve, 0444, dp);
		return 1;		
	}
	if(s >= nelem(dtracydir))
		return -1;
	tab = &dtracydir[s];
	path = QIDPATH(SLOT(c->qid), 0);
	devdir(c, (Qid){tab->qid.path|path, tab->qid.vers, tab->qid.type}, tab->name, tab->length, eve, tab->perm, dp);
	return 1;
}

static Walkqid*
dtracywalk(Chan *c, Chan *nc, char **name, int nname)
{
	Walkqid *rc;

	eqlock(&dtracylock);
	if(waserror()){
		qunlock(&dtracylock);
		nexterror();
	}
	rc = devwalk(c, nc, name, nname, nil, 0, dtracygen);
	qunlock(&dtracylock);
	poperror();
	return rc;
}

static int
dtracystat(Chan *c, uchar *dp, int n)
{
	int rc;

	eqlock(&dtracylock);
	if(waserror()){
		qunlock(&dtracylock);
		nexterror();
	}
	rc = devstat(c, dp, n, nil, 0, dtracygen);
	qunlock(&dtracylock);
	poperror();
	return rc;	
}

static Chan*
dtracyopen(Chan *c, int omode)
{
	DTKChan *p;
	Chan *ch;

	eqlock(&dtracylock);
	if(waserror()){
		qunlock(&dtracylock);
		nexterror();
	}
	if(c->qid.path == Qclone){
		if(!iseve()) error(Eperm);
		p = dtknew();
		c->qid.path = QIDPATH(p->idx, Qctl);
	}
	if(c->qid.path == Qprobes){
		p = nil;
	}else{
		p = dtklook(SLOT(c->qid));
		if(SLOT(c->qid) >= 0 && p == nil) error(Enonexist);
		if(FILE(c->qid) != Qdir && !iseve()) error(Eperm);
	}
	ch = devopen(c, omode, nil, 0, dtracygen);
	if(p != nil) p->ref++;
	qunlock(&dtracylock);
	poperror();
	ch->aux = smalloc(sizeof(DTKAux));
	return ch;
}

static void
dtracyclose(Chan *ch)
{
	DTKAux *aux;
	DTKChan *p;

	if(ch->aux != nil){
		eqlock(&dtracylock);
		p = dtklook(SLOT(ch->qid));
		if(p != nil && --p->ref == 0)
			dtkfree(p);
		qunlock(&dtracylock);
		aux = ch->aux;
		free(aux->str);
		free(ch->aux);
		ch->aux = nil;
	}
}

static int
epidread(DTKAux *aux, DTChan *c, char *a, long n, vlong off)
{
	Fmt f;
	DTEnab *e;

	if(off == 0){
		free(aux->str);
		aux->str = nil;
	}
	if(aux->str == nil){
		fmtstrinit(&f);
		for(e = c->enab; e != nil; e = e->channext)
			fmtprint(&f, "%d %d %d %s\n", e->epid, e->gr->id, e->gr->reclen, e->prob->name);
		aux->str = fmtstrflush(&f);
	}
	return readstr(off, a, n, aux->str);
}

static long
lockedread(DTChan *c, void *a, long n, int(*readf)(DTChan *, void *, int))
{
	long rc;

	if(waserror()){
		qunlock(&dtracylock);
		nexterror();
	}
	eqlock(&dtracylock);
	rc = readf(c, a, n);
	qunlock(&dtracylock);
	poperror();
	return rc;
}

static long
handleread(DTChan *c, void *a, long n, int(*readf)(DTChan *, void *, int))
{
	long rc, m;
	int i;

	for(;;){
		rc = lockedread(c, a, n, readf);
		if(rc < 0) return -1;
		if(rc > 0) break;
		tsleep(&up->sleep, return0, 0, 250);
	}
	m = rc;
	for(i = 0; i < 3 && m < n/2; i++){
		tsleep(&up->sleep, return0, 0, 50);
		rc = lockedread(c, (uchar *)a + m, n - m, readf);
		if(rc < 0) break;
		m += rc;
	}
	return m;
}

static long
probesread(DTKAux *aux, char *a, long n, vlong off)
{
	Fmt f;
	DTProbe **l;
	int i, nl;
	
	if(aux->str == nil){
		fmtstrinit(&f);
		nl = dtplist(&l);
		for(i = 0; i < nl; i++)
			fmtprint(&f, "%s\n", l[i]->name);
		dtfree(l);
		aux->str = fmtstrflush(&f);
	}
	return readstr(off, a, n, aux->str);
}

static long
dtracyread(Chan *c, void *a, long n, vlong off)
{
	int rc;
	DTKChan *p;
	DTChan *ch;

	eqlock(&dtracylock);
	if(waserror()){
		qunlock(&dtracylock);
		nexterror();
	}
	if(SLOT(c->qid) == -1)
		switch((int)c->qid.path){
		case Qdir:
			rc = devdirread(c, a, n, nil, 0, dtracygen);
			goto out;
		case Qprobes:
			rc = probesread(c->aux, a, n, off);
			goto out;
		default:
			error(Egreg);
		}
	p = dtklook(SLOT(c->qid));
	if(p == nil) error(Enonexist);
	switch(FILE(c->qid)){
	case Qdir:
		rc = devdirread(c, a, n, nil, 0, dtracygen);
		break;
	case Qctl:
		sprint(up->genbuf, "%d", p->idx);
		rc = readstr(off, a, n, up->genbuf);
		break;
	case Qbuf:
		ch = p->ch;
		qunlock(&dtracylock);
		poperror();
		return handleread(ch, a, n, dtcread);
	case Qaggbuf:
		ch = p->ch;
		qunlock(&dtracylock);
		poperror();
		return handleread(ch, a, n, dtcaggread);
	case Qepid:
		rc = epidread(c->aux, p->ch, a, n, off);
		break;
	default:
		error(Egreg);
		return 0;
	}
out:
	qunlock(&dtracylock);
	poperror();
	return rc;
}

static long
dtracywrite(Chan *c, void *a, long n, vlong)
{
	int rc;
	DTKChan *p;
	Cmdbuf *cb;
	Cmdtab *ct;

	eqlock(&dtracylock);
	if(waserror()){
		qunlock(&dtracylock);
		nexterror();
	}
	if(SLOT(c->qid) == -1)
		switch((int)c->qid.path){
		case Qdir:
			error(Eperm);
		default:
			error(Egreg);
		}
	p = dtklook(SLOT(c->qid));
	if(p == nil) error(Enonexist);
	switch(FILE(c->qid)){
	case Qdir:
		error(Eperm);
		return 0;
	case Qctl:
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, dtracyctlmsg, nelem(dtracyctlmsg));
		switch(ct->index){
		case CMstop: dtcrun(p->ch, DTCSTOP); break;
		case CMgo: dtcrun(p->ch, DTCGO); break;
		default:
			error(Egreg);
		}
		poperror();
		free(cb);
		rc = n;
		break;
	case Qprog:
		{
			char *buf;
			
			buf = smalloc(n+1);
			if(waserror()){
				free(buf);
				nexterror();
			}
			memmove(buf, a, n);
			prog(p, buf);
			free(buf);
			poperror();
			rc = n;
			break;
		}
	default:
		error(Egreg);
		return 0;
	}
	qunlock(&dtracylock);
	poperror();
	return rc;
}


Dev dtracydevtab = {
	L'Δ',
	"dtracy",
	
	devreset,
	dtracyinit,
	devshutdown,
	dtracyattach,
	dtracywalk,
	dtracystat,
	dtracyopen,
	devcreate,
	dtracyclose,
	dtracyread,
	devbread,
	dtracywrite,
	devbwrite,
	devremove,
	devwstat,
};

void *
dtmalloc(ulong n)
{
	void *v;

	v = smalloc(n);
	setmalloctag(v, getcallerpc(&n));
	return v;
}

void
dtfree(void *v)
{
	free(v);
}

void *
dtrealloc(void *v, ulong n)
{
	v = realloc(v, n);
	if(v != nil)
		setrealloctag(v, getcallerpc(&v));
	return v;
}

int
dtmachlock(int i)
{
	while(i < 0) {
		i = dtmachlock(m->machno);
		if(i == m->machno)
			return i;
		dtmachunlock(i);
		i = -1;
	}
	ilock(&machlocks[i]);
	return i;
}

void
dtmachunlock(int i)
{
	iunlock(&machlocks[i]);
}

void
dtcoherence(void)
{
	coherence();
}

uvlong
dttime(void)
{
	return fastticks(nil);
}

uvlong
dtgetvar(int v)
{
	switch(v){
	case DTV_PID:
		return up != nil ? up->pid : 0;
	default:
		return 0;
	}
}

extern int peek(char *, char *, int);

int
dtpeek(uvlong addr, void *buf, int len)
{
	uintptr a;
	
	a = addr;
	if(len == 0) return 0;
	if(a != addr || a > -(uintptr)len || len < 0) return -1;
	if(up == nil || up->privatemem || a >= KZERO) return -1;
	return peek((void *)a, buf, len) > 0 ? -1 : 0;
}
