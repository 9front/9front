#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/audioif.h"

typedef struct Audioprobe Audioprobe;
typedef struct Audiochan Audiochan;

struct Audioprobe
{
	char *name;
	int (*probe)(Audio*);
};

struct Audiochan
{
	QLock;

	Chan *owner;
	Audio *adev;

	char *data;
	char buf[4000+1];
};

enum {
	Qdir = 0,
	Qaudio,
	Qaudioctl,
	Qaudiostat,
	Qvolume,
};

static Dirtab audiodir[] = {
	".",	{Qdir, 0, QTDIR},	0,	DMDIR|0555,
	"audio",	{Qaudio},	0,	0666,
	"audioctl",	{Qaudioctl},	0,	0222,
	"audiostat",	{Qaudiostat},	0,	0444,
	"volume",	{Qvolume},	0,	0666,
};


static int naudioprobes;
static Audioprobe audioprobes[16];
static Audio *audiodevs;

static char Evolume[] = "illegal volume specifier";
static char Ebusy[] = "device is busy";

void
addaudiocard(char *name, int (*probefn)(Audio *))
{
	Audioprobe *probe;

	if(naudioprobes >= nelem(audioprobes))
		return;

	probe = &audioprobes[naudioprobes++];
	probe->name = name;
	probe->probe = probefn;
}

static void
audioreset(void)
{
	int i, ctlrno = 0;
	Audio **pp;
	Audioprobe *probe;

	pp = &audiodevs;
	*pp = malloc(sizeof(Audio));

	for(i=0; i<naudioprobes; i++){
		probe = &audioprobes[i];

		for(;;){
			if(*pp == nil){
				print("audio: no memory\n");
				break;
			}
			memset(*pp, 0, sizeof(Audio));
			(*pp)->ctlrno = ctlrno;
			(*pp)->name = probe->name;
			if(probe->probe(*pp))
				break;

			ctlrno++;
			pp = &(*pp)->next;
			*pp = malloc(sizeof(Audio));
		}
	}

	free(*pp);
	*pp = nil;
}

static Audiochan*
audioclone(Chan *c, Audio *adev)
{
	Audiochan *ac;

	ac = malloc(sizeof(Audiochan));
	if(ac == nil){
		cclose(c);
		return nil;
	}

	c->aux = ac;
	ac->owner = c;
	ac->adev = adev;
	ac->data = nil;

	return ac;
}

static Chan*
audioattach(char *spec)
{
	static ulong attached = 0;
	Audiochan *ac;
	Audio *adev;
	Chan *c;
	ulong i;

	i = strtoul(spec, nil, 10);
	for(adev = audiodevs; adev; adev = adev->next)
		if(adev->ctlrno == i)
			break;
	if(adev == nil)
		error(Enodev);

	c = devattach('A', spec);
	c->qid.path = Qdir;

	if((ac = audioclone(c, adev)) == nil)
		error(Enomem);

	i = 1<<adev->ctlrno;
	if((attached & i) == 0){
		static char *settings[] = {
			"speed 44100",
			"delay 1764",	/* 40 ms */
			"master 100",
			"audio 100",
			"head 100",
			"recgain 0",
		};

		attached |= i;
		for(i=0; i<nelem(settings) && adev->volwrite; i++){
			strcpy(ac->buf, settings[i]);
			if(!waserror()){
				adev->volwrite(adev, ac->buf, strlen(ac->buf), 0);
				poperror();
			}
		}
	}

	return c;
}

static Chan*
audioopen(Chan *c, int omode)
{
	Audiochan *ac;
	Audio *adev;
	int mode;

	ac = c->aux;
	adev = ac->adev;
	if(c->qid.path == Qaudio){
		mode = openmode(omode);
		if(waserror()){
			if(mode == OREAD || mode == ORDWR)
				decref(&adev->audioopenr);
			nexterror();
		}
		if(mode == OREAD || mode == ORDWR)
			if(incref(&adev->audioopenr) != 1)
				error(Ebusy);

		if(waserror()){
			if(mode == OWRITE || mode == ORDWR)
				decref(&adev->audioopenw);
			nexterror();
		}
		if(mode == OWRITE || mode == ORDWR)
			if(incref(&adev->audioopenw) != 1)
				error(Ebusy);

		c = devopen(c, omode, audiodir, nelem(audiodir), devgen);
		poperror();
		poperror();
		return c;
	}
	return devopen(c, omode, audiodir, nelem(audiodir), devgen);
}

static long
audioread(Chan *c, void *a, long n, vlong off)
{
	Audiochan *ac;
	Audio *adev;
	long (*fn)(Audio *, void *, long, vlong);

	ac = c->aux;
	adev = ac->adev;

	fn = nil;
	switch((ulong)c->qid.path){
	case Qdir:
		audiodir[Qaudio].length = adev->buffered ? adev->buffered(adev) : 0;
		return devdirread(c, a, n, audiodir, nelem(audiodir), devgen);
	case Qaudio:
		fn = adev->read;
		break;
	case Qaudiostat:
		fn = adev->status;
		break;
	case Qvolume:
		fn = adev->volread;
		break;
	}
	if(fn == nil)
		error(Egreg);

	eqlock(ac);
	if(waserror()){
		qunlock(ac);
		nexterror();
	}
	switch((ulong)c->qid.path){
	case Qaudiostat:
	case Qvolume:
		/* generate the text on first read */
		if(ac->data == nil || off == 0){
			long l;

			ac->data = nil;
			l = fn(adev, ac->buf, sizeof(ac->buf)-1, 0);
			if(l < 0)
				l = 0;
			ac->buf[l] = 0;
			ac->data = ac->buf;
		}
		/* then serve all requests from buffer */
		n = readstr(off, a, n, ac->data);
		break;

	default:
		n = fn(adev, a, n, off);
	}
	qunlock(ac);
	poperror();
	return n;
}

static long
audiowrite(Chan *c, void *a, long n, vlong off)
{
	Audiochan *ac;
	Audio *adev;
	long (*fn)(Audio *, void *, long, vlong);

	ac = c->aux;
	adev = ac->adev;

	fn = nil;
	switch((ulong)c->qid.path){
	case Qaudio:
		fn = adev->write;
		break;
	case Qaudioctl:
		fn = adev->ctl;
		break;
	case Qvolume:
		fn = adev->volwrite;
		break;
	}
	if(fn == nil)
		error(Egreg);

	eqlock(ac);
	if(waserror()){
		qunlock(ac);
		nexterror();
	}
	switch((ulong)c->qid.path){
	case Qaudioctl:
	case Qvolume:
		if(n >= sizeof(ac->buf))
			error(Etoobig);

		/* copy data to audiochan buffer so it can be modified */
		ac->data = nil;
		memmove(ac->buf, a, n);
		ac->buf[n] = 0;
		a = ac->buf;
		off = 0;
	}
	n = fn(adev, a, n, off);
	qunlock(ac);
	poperror();
	return n;
}

static void
audioclose(Chan *c)
{
	Audiochan *ac;
	Audio *adev;

	ac = c->aux;
	adev = ac->adev;
	if((c->qid.path == Qaudio) && (c->flag & COPEN)){
		if(adev->close){
			if(!waserror()){
				adev->close(adev, c->mode);
				poperror();
			}
		}
		if(c->mode == OWRITE || c->mode == ORDWR)
			decref(&adev->audioopenw);
		if(c->mode == OREAD || c->mode == ORDWR)
			decref(&adev->audioopenr);
	}
	if(ac->owner == c){
		ac->owner = nil;
		c->aux = nil;
		free(ac);
	}
}

static Walkqid*
audiowalk(Chan *c, Chan *nc, char **name, int nname)
{
	Audiochan *ac;
	Audio *adev;
	Walkqid *wq;

	ac = c->aux;
	adev = ac->adev;
	wq = devwalk(c, nc, name, nname, audiodir, nelem(audiodir), devgen);
	if(wq && wq->clone){
		if(audioclone(wq->clone, adev) == nil){
			free(wq);
			wq = nil;
		}
	}
	return wq;
}

static int
audiostat(Chan *c, uchar *dp, int n)
{
	Audiochan *ac;
	Audio *adev;

	ac = c->aux;
	adev = ac->adev;
	if((ulong)c->qid.path == Qaudio)
		audiodir[Qaudio].length = adev->buffered ? adev->buffered(adev) : 0;
	return devstat(c, dp, n, audiodir, nelem(audiodir), devgen);
}

/*
 * audioread() made sure the buffer is big enougth so a full volume
 * table can be serialized in one pass.
 */
long
genaudiovolread(Audio *adev, void *a, long n, vlong,
	Volume *vol, int (*volget)(Audio *, int, int *), ulong caps)
{
	int i, j, r, v[2];
	char *p, *e;

	p = a;
	e = p + n;
	for(i = 0; vol[i].name != 0; ++i){
		if(vol[i].cap && (vol[i].cap & caps) == 0)
			continue;
		v[0] = 0;
		v[1] = 0;
		if((*volget)(adev, i, v) != 0)
			continue;
		if(vol[i].type == Absolute)
			p += snprint(p, e - p, "%s %d\n", vol[i].name, v[0]);
		else {
			r = abs(vol[i].range);
			if(r == 0)
				continue;
			for(j=0; j<2; j++){
				if(v[j] < 0)
					v[j] = 0;
				if(v[j] > r)
					v[j] = r;
				if(vol[i].range < 0)
					v[j] = r - v[j];
				v[j] = (v[j]*100)/r;
			}
			switch(vol[i].type){
			case Left:
				p += snprint(p, e - p, "%s %d\n", vol[i].name, v[0]);
				break;
			case Right:
				p += snprint(p, e - p, "%s %d\n", vol[i].name, v[1]);
				break;
			case Stereo:
				p += snprint(p, e - p, "%s %d %d\n", vol[i].name, v[0], v[1]);
				break;
			}
		}
	}

	return p - (char*)a;
}

/*
 * genaudiovolwrite modifies the buffer that gets passed to it. this
 * is ok as long as it is called from inside Audio.volwrite() because
 * audiowrite() copies the data to Audiochan.buf[] and inserts a
 * terminating \0 byte before calling Audio.volwrite().
 */
long
genaudiovolwrite(Audio *adev, void *a, long n, vlong,
	Volume *vol, int (*volset)(Audio *, int, int *), ulong caps)
{
	int ntok, i, j, r, v[2];
	char *p, *e, *x, *tok[4];

	p = a;
	e = p + n;

	for(;p < e; p = x){
		if(x = strchr(p, '\n'))
			*x++ = 0;
		else
			x = e;
		ntok = tokenize(p, tok, 4);
		if(ntok <= 0)
			continue;
		if(ntok == 1){
			tok[1] = tok[0];
			tok[0] = "master";
			ntok = 2;
		}
		for(i = 0; vol[i].name != 0; i++){
			if(vol[i].cap && (vol[i].cap & caps) == 0)
				continue;
			if(cistrcmp(vol[i].name, tok[0]))
				continue;
	
			if((ntok>2) && (!cistrcmp(tok[1], "out") || !cistrcmp(tok[1], "in")))
				memmove(tok+1, tok+2, --ntok);

			v[0] = 0;
			v[1] = 0;
			if(ntok > 1)
				v[0] = v[1] = atoi(tok[1]);
			if(ntok > 2)
				v[1] = atoi(tok[2]);
			if(vol[i].type == Absolute)
				(*volset)(adev, i, v);
			else {
				r = abs(vol[i].range);
				for(j=0; j<2; j++){
					v[j] = (50+(v[j]*r))/100;
					if(v[j] < 0)
						v[j] = 0;
					if(v[j] > r)
						v[j] = r;
					if(vol[i].range < 0)
						v[j] = r - v[j];
				}
				(*volset)(adev, i, v);
			}
			break;
		}
		if(vol[i].name == nil)
			error(Evolume);
	}

	return n;
}

Dev audiodevtab = {
	'A',
	"audio",
	audioreset,
	devinit,
	devshutdown,
	audioattach,
	audiowalk,
	audiostat,
	audioopen,
	devcreate,
	audioclose,
	audioread,
	devbread,
	audiowrite,
	devbwrite,
	devremove,
	devwstat,
};
