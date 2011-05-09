#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

enum {
	LENHDR = 4,

	MAGIC = 0xFF | ('S'<<8) | ('M'<<16) | ('B'<<24),

	SMB_FLAGS_CASE_INSENSITIVE = 0x08,
	SMB_FLAGS_CANONICALIZED_PATHS = 0x10,
	SMB_FLAGS_REPLY = 0x80,

	NOCASEMASK = SMB_FLAGS_CASE_INSENSITIVE | SMB_FLAGS_CANONICALIZED_PATHS,

	SMB_FLAGS2_LONG_NAMES = 0x0001,
	SMB_FLAGS2_EAS = 0x0002,
	SMB_FLAGS2_IS_LONG_NAME = 0x0040,
	SMB_FLAGS2_NT_STATUS = 0x4000,
	SMB_FLAGS2_UNICODE = 0x8000,
};

static int casesensitive = 0;

static void
respond(Req *r, int err)
{
	int n, flags, flags2;

	if(err && !(r->flags2 & SMB_FLAGS2_NT_STATUS))
		err = doserror(err);
	flags = (r->flags & (r->namecmp != strcmp ? NOCASEMASK : 0)) |
		SMB_FLAGS_REPLY;
	flags2 = (r->flags2 & (SMB_FLAGS2_NT_STATUS | 
		SMB_FLAGS2_LONG_NAMES | SMB_FLAGS2_UNICODE)) | 
		SMB_FLAGS2_IS_LONG_NAME;
	if(r->cmd != 0x73) /* SMB_COM_SESSION_SETUP_ANDX */
		memset(r->sig, 0, sizeof(r->sig));
	n = pack(r->rh, r->rh, r->rh+32, "lblbww[]__wwww",
		MAGIC, r->cmd, err, flags,  flags2, r->pid>>16, r->sig, r->sig+sizeof(r->sig),
		r->tid, r->pid & 0xFFFF, r->uid, r->mid);
	if(err){
		r->rp = r->rh+n;
		r->rp += pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}");
	}
	if(debug > 1)
		dumphex("respond", r->rh, r->rp);
	if(debug)
		fprint(2, "respond: err=%x\n\n", err);
	n = r->rp - r->rh;
	r->lh[0] = 0;
	r->lh[1] = 0;
	r->lh[2] = n>>8 & 0xFF;
	r->lh[3] = n & 0xFF;
	write(1, r->lh, LENHDR+n);
}

static void
receive(uchar *h, uchar *e)
{
	static uchar buffer[LENHDR + BUFFERSIZE];
	static Rop rop8 = {
		.strpack = smbstrpack8,
		.strunpack = smbstrunpack8,
		.namepack = smbnamepack8,
		.nameunpack = smbnameunpack8,
		.untermstrpack = smbuntermstrpack8,
		.untermnamepack = smbuntermnamepack8,
	}, rop16 = {
		.strpack = smbstrpack16,
		.strunpack = smbstrunpack16,
		.namepack = smbnamepack16,
		.nameunpack = smbnameunpack16,
		.untermstrpack = smbuntermstrpack16,
		.untermnamepack = smbuntermnamepack16,
	};

	uchar *sig;
	int n, hpid, magic;
	Req r;

	if(debug > 1)
		dumphex("receive", h, e);
	if((n = unpack(h, h, e, "lb____bww{.________}__wwww", &magic,
		&r.cmd, &r.flags, &r.flags2, &hpid, &sig, &r.tid, &r.pid, &r.uid, &r.mid)) == 0){
		logit("bad smb header");
		return;
	}
	if(magic != MAGIC){
		logit("bad smb magic");
		return;
	}
	r.pid |= hpid<<16;
	r.lh = buffer;
	r.rh = r.lh + LENHDR;
	r.rp = r.rh + n;
	r.re = r.rh + remotebuffersize;
	r.o = (r.flags2 & SMB_FLAGS2_UNICODE) ? &rop16 : &rop8;
	memmove(r.sig, sig, sizeof(r.sig));
	r.name = nil;
	r.respond = respond;
	r.namecmp = ((r.flags & NOCASEMASK) && !casesensitive) ? cistrcmp : strcmp;
	smbcmd(&r, r.cmd, h, h+n, e);
}

static void
serve(void)
{
	static uchar buffer[LENHDR + BUFFERSIZE];
	uchar *m, *me;
	uchar *p, *pe;
	uchar *hl;
	int n;

	p = hl = buffer;
	pe = p + LENHDR+BUFFERSIZE;

	for(;;){
		n = read(0, p, pe - p);
		if(n <= 0)
			break;
		p += n;
Next:
		if(p - hl < LENHDR)
			continue;
		n = hl[2]<<8 | hl[3];
		m = hl + LENHDR;
		me = m + n;
		if(me > pe){
			logit("message too big");
			break;
		}
		if(me > p)
			continue;
		receive(m, me);
		n = p - me;
		p = hl + n;
		if(n > 0){
			memmove(hl, me, n);
			goto Next;
		}
	}
}

void
main(int argc, char *argv[])
{
	static struct {
		char *s;
		int *v;
	} opts[] = {
		{ "trspaces", &trspaces },
		{ "casesensitive", &casesensitive },
		{ nil, nil }
	}, *o;

	char *log, *opt;
	Tm *tm;
	int pid;

	debug = 0;
	trspaces = 0;
	needauth = 1;
	domain = "WORKGROUP";
	progname = "cifsd";
	osname = "Plan 9";
	log = nil;

	ARGBEGIN {
	case 't':
		needauth = 0;
		break;
	case 'd':
		debug++;
		break;
	case 'f':
		log = EARGF(exits("bad arg"));
		break;
	case 'w':
		domain = EARGF(exits("bad arg"));
		break;
	case 'o':
		opt = EARGF(exits("bad arg"));
		for(o=opts; o->s; o++)
			if(strcmp(opt, o->s) == 0){
				*o->v = 1;
				break;
			}
		if(o->s == nil)
			exits("bad arg");
		break;
	} ARGEND

	close(2);
	if(!log || open(log, OWRITE) < 0){
		open("/dev/null", OWRITE);
		debug = 0;
	}

	remotesys = argc ? getremote(argv[argc-1]) : nil;
	remoteuser = nil;
	remotebuffersize = BUFFERSIZE;
	starttime = time(nil);
	pid = getpid();
	srand(starttime ^ pid);
	tm = localtime(starttime);
	tzoff = tm->tzoff;

	logit("started [%d]", pid);
	serve();
	logoff();
	logit("exited [%d]", pid);
}
