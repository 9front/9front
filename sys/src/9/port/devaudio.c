#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

typedef struct Audioprobe Audioprobe;
struct Audioprobe {
	char *name;
	int (*probe)(Audio*);
};

enum {
	Qdir = 0,
	Qaudio,
	Qaudioctl,
	Qaudiostatus,
	Qvolume,
	Maxaudioprobes = 8,
};

static int naudioprobes;
static Audioprobe audioprobes[Maxaudioprobes];
static Audio *audiodevs;

static Dirtab audiodir[] = {
	".",	{Qdir, 0, QTDIR},	0,	DMDIR|0555,
	"audio",	{Qaudio},			0,	0666,
	"audioctl",	{Qaudioctl},			0,	0666,
	"audiostat",	{Qaudiostatus},			0,	0666,
	"volume",	{Qvolume},			0,	0666,
};

void
addaudiocard(char *name, int (*probefn)(Audio *))
{
	Audioprobe *probe;
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
	(*pp)->ctlrno = ctlrno++;
	for(i = 0; i < naudioprobes; i++){
		probe = &audioprobes[i];
		(*pp)->name = probe->name;
		while(!probe->probe(*pp)){
			pp = &(*pp)->next;
			*pp = malloc(sizeof(Audio));
			(*pp)->ctlrno = ctlrno++;
			(*pp)->name = probe->name;
		}
	}
	free(*pp);
	*pp = nil;
}

static Chan*
audioattach(char *spec)
{
	Chan *c;
	Audio *p;
	int i;
	if(spec != nil && *spec != '\0')
		i = strtol(spec, 0, 10);
	else
		i = 0;
	for(p = audiodevs; p; p = p->next)
		if(i-- == 0)
			break;
	if(p == nil)
		error(Enodev);
	c = devattach('A', spec);
	c->qid.path = Qdir;
	c->aux = p;
	if(p->attach)
		p->attach(p);
	return c;
}

static long
audioread(Chan *c, void *a, long n, vlong off)
{
	Audio *adev;
	long (*fn)(Audio *, void *, long, vlong);
	adev = c->aux;
	switch((ulong)c->qid.path){
	default:
		error("audio bugger (rd)");
	case Qaudioctl:
		fn = adev->ctl;
		break;
	case Qdir:
		if(adev->buffered)
			audiodir[Qaudio].length = adev->buffered(adev);
		return devdirread(c, a, n, audiodir, nelem(audiodir), devgen);
	case Qaudio:
		fn = adev->read;
		break;
	case Qaudiostatus:
		fn = adev->status;
		break;
	case Qvolume:
		fn = adev->volread;
		break;
	}
	if(fn == nil)
		error("not implemented");
	return fn(adev, a, n, off);
}

static long
audiowrite(Chan *c, void *a, long n, vlong off)
{
	Audio *adev;
	long (*fn)(Audio *, void *, long, vlong);
	adev = c->aux;
	switch((ulong)c->qid.path){
	default:
		error("audio bugger (wr)");
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
		error("not implemented");
	return fn(adev, a, n, off);
}

static void
audioclose(Chan *c)
{
	Audio *adev;
	adev = c->aux;
	switch((ulong)c->qid.path){
	default:
		return;
	case Qaudio:
		if(adev->close == nil)
			return;
		adev->close(adev);
		return;
	}
}

static Walkqid*
audiowalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, audiodir, nelem(audiodir), devgen);
}

static int
audiostat(Chan *c, uchar *dp, int n)
{
	Audio *adev;
	adev = c->aux;
	if(adev->buffered && (ulong)c->qid.path == Qaudio)
		audiodir[Qaudio].length = adev->buffered(adev);
	return devstat(c, dp, n, audiodir, nelem(audiodir), devgen);
}

static Chan*
audioopen(Chan *c, int omode)
{
	return devopen(c, omode, audiodir, nelem(audiodir), devgen);
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
