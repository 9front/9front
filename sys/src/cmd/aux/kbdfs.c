#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <keyboard.h>
#include <9p.h>

enum {
	Nscan=	128,

	Qroot=	0,
	Qkbd,
	Qkbin,
	Qkbmap,
	Qcons,
	Qconsctl,
	Nqid,

	Rawon=	0,
	Rawoff,
	Kbdflush,

	STACK = 8*1024,
};

typedef struct Key Key;
typedef struct Scan Scan;

struct Key {
	int	down;
	int	c;
	Rune	r;
};

struct Scan {
	int	esc1;
	int	esc2;
	int	caps;
	int	num;
	int	shift;
	int	ctl;
	int	alt;
	int	altgr;
	int	leds;
};

struct Qtab {
	char *name;
	int mode;
	int type;
} qtab[Nqid] = {
	"/",
		DMDIR|0500,
		QTDIR,

	"kbd",
		0600,
		0,

	"kbin",
		0200,	
		0,

	"kbmap",
		0600,	
		0,

	"cons",
		0600,	
		0,

	"consctl",
		0600,
		0,
};

char Eshort[] = "read count too small";
char Ebadarg[] = "invalid argument";
char Eperm[] = "permission denied";
char Einuse[] = "file in use";
char Enonexist[] = "file does not exist";
char Ebadspec[] = "bad attach specifier";
char Ewalk[] = "walk in non directory";
char Efront[] = "the front fell off";

int scanfd;
int ledsfd;
int consfd;

int kbdopen;
int consopen;
int consctlopen;

int debug;

Channel *keychan;	/* Key */

Channel *reqchan;	/* Req* */
Channel *ctlchan;	/* int */

Channel *rawchan;	/* Rune */
Channel *linechan;	/* char * */
Channel *kbdchan;	/* char* */

/*
 * The codes at 0x79 and 0x7b are produced by the PFU Happy Hacking keyboard.
 * A 'standard' keyboard doesn't produce anything above 0x58.
 */
Rune kbtab[Nscan] = 
{
[0x00]	0,	0x1b,	'1',	'2',	'3',	'4',	'5',	'6',
[0x08]	'7',	'8',	'9',	'0',	'-',	'=',	'\b',	'\t',
[0x10]	'q',	'w',	'e',	'r',	't',	'y',	'u',	'i',
[0x18]	'o',	'p',	'[',	']',	'\n',	Kctl,	'a',	's',
[0x20]	'd',	'f',	'g',	'h',	'j',	'k',	'l',	';',
[0x28]	'\'',	'`',	Kshift,	'\\',	'z',	'x',	'c',	'v',
[0x30]	'b',	'n',	'm',	',',	'.',	'/',	Kshift,	'*',
[0x38]	Kalt,	' ',	Kctl,	KF|1,	KF|2,	KF|3,	KF|4,	KF|5,
[0x40]	KF|6,	KF|7,	KF|8,	KF|9,	KF|10,	Knum,	Kscroll,	'7',
[0x48]	'8',	'9',	'-',	'4',	'5',	'6',	'+',	'1',
[0x50]	'2',	'3',	'0',	'.',	0,	0,	0,	KF|11,
[0x58]	KF|12,	0,	0,	0,	0,	0,	0,	0,
[0x60]	0,	0,	0,	0,	0,	0,	0,	0,
[0x68]	0,	0,	0,	0,	0,	0,	0,	0,
[0x70]	0,	0,	0,	0,	0,	0,	0,	0,
[0x78]	0,	Kdown,	0,	Kup,	0,	0,	0,	0,
};

Rune kbtabshift[Nscan] =
{
[0x00]	0,	0x1b,	'!',	'@',	'#',	'$',	'%',	'^',
[0x08]	'&',	'*',	'(',	')',	'_',	'+',	'\b',	'\t',
[0x10]	'Q',	'W',	'E',	'R',	'T',	'Y',	'U',	'I',
[0x18]	'O',	'P',	'{',	'}',	'\n',	Kctl,	'A',	'S',
[0x20]	'D',	'F',	'G',	'H',	'J',	'K',	'L',	':',
[0x28]	'"',	'~',	Kshift,	'|',	'Z',	'X',	'C',	'V',
[0x30]	'B',	'N',	'M',	'<',	'>',	'?',	Kshift,	'*',
[0x38]	Kalt,	' ',	Kctl,	KF|1,	KF|2,	KF|3,	KF|4,	KF|5,
[0x40]	KF|6,	KF|7,	KF|8,	KF|9,	KF|10,	Knum,	Kscroll,	'7',
[0x48]	'8',	'9',	'-',	'4',	'5',	'6',	'+',	'1',
[0x50]	'2',	'3',	'0',	'.',	0,	0,	0,	KF|11,
[0x58]	KF|12,	0,	0,	0,	0,	0,	0,	0,
[0x60]	0,	0,	0,	0,	0,	0,	0,	0,
[0x68]	0,	0,	0,	0,	0,	0,	0,	0,
[0x70]	0,	0,	0,	0,	0,	0,	0,	0,
[0x78]	0,	Kup,	0,	Kup,	0,	0,	0,	0,
};

Rune kbtabesc1[Nscan] =
{
[0x00]	0,	0,	0,	0,	0,	0,	0,	0,
[0x08]	0,	0,	0,	0,	0,	0,	0,	0,
[0x10]	0,	0,	0,	0,	0,	0,	0,	0,
[0x18]	0,	0,	0,	0,	'\n',	Kctl,	0,	0,
[0x20]	0,	0,	0,	0,	0,	0,	0,	0,
[0x28]	0,	0,	Kshift,	0,	0,	0,	0,	0,
[0x30]	0,	0,	0,	0,	0,	'/',	0,	Kprint,
[0x38]	Kaltgr,	0,	0,	0,	0,	0,	0,	0,
[0x40]	0,	0,	0,	0,	0,	0,	Kbreak,	Khome,
[0x48]	Kup,	Kpgup,	0,	Kleft,	0,	Kright,	0,	Kend,
[0x50]	Kdown,	Kpgdown,	Kins,	Kdel,	0,	0,	0,	0,
[0x58]	0,	0,	0,	0,	0,	0,	0,	0,
[0x60]	0,	0,	0,	0,	0,	0,	0,	0,
[0x68]	0,	0,	0,	0,	0,	0,	0,	0,
[0x70]	0,	0,	0,	0,	0,	0,	0,	0,
[0x78]	0,	Kup,	0,	0,	0,	0,	0,	0,
};

Rune kbtabaltgr[Nscan] =
{
[0x00]	0,	0,	0,	0,	0,	0,	0,	0,
[0x08]	0,	0,	0,	0,	0,	0,	0,	0,
[0x10]	0,	0,	0,	0,	0,	0,	0,	0,
[0x18]	0,	0,	0,	0,	'\n',	Kctl,	0,	0,
[0x20]	0,	0,	0,	0,	0,	0,	0,	0,
[0x28]	0,	0,	Kshift,	0,	0,	0,	0,	0,
[0x30]	0,	0,	0,	0,	0,	'/',	0,	Kprint,
[0x38]	Kaltgr,	0,	0,	0,	0,	0,	0,	0,
[0x40]	0,	0,	0,	0,	0,	0,	Kbreak,	Khome,
[0x48]	Kup,	Kpgup,	0,	Kleft,	0,	Kright,	0,	Kend,
[0x50]	Kdown,	Kpgdown,	Kins,	Kdel,	0,	0,	0,	0,
[0x58]	0,	0,	0,	0,	0,	0,	0,	0,
[0x60]	0,	0,	0,	0,	0,	0,	0,	0,
[0x68]	0,	0,	0,	0,	0,	0,	0,	0,
[0x70]	0,	0,	0,	0,	0,	0,	0,	0,
[0x78]	0,	Kup,	0,	0,	0,	0,	0,	0,
};

Rune kbtabctl[Nscan] =
{
[0x00]	0,	'', 	'', 	'', 	'', 	'', 	'', 	'', 
[0x08]	'', 	'', 	'', 	'', 	'', 	'', 	'\b',	'\t',
[0x10]	'', 	'', 	'', 	'', 	'', 	'', 	'', 	'\t',
[0x18]	'', 	'', 	'', 	'', 	'\n',	Kctl,	'', 	'', 
[0x20]	'', 	'', 	'', 	'\b',	'\n',	'', 	'', 	'', 
[0x28]	'', 	0, 	Kshift,	'', 	'', 	'', 	'', 	'', 
[0x30]	'', 	'', 	'', 	'', 	'', 	'', 	Kshift,	'\n',
[0x38]	Kalt,	0, 	Kctl,	'', 	'', 	'', 	'', 	'', 
[0x40]	'', 	'', 	'', 	'', 	'', 	'', 	'', 	'', 
[0x48]	'', 	'', 	'', 	'', 	'', 	'', 	'', 	'', 
[0x50]	'', 	'', 	'', 	'', 	0,	0,	0,	'', 
[0x58]	'', 	0,	0,	0,	0,	0,	0,	0,
[0x60]	0,	0,	0,	0,	0,	0,	0,	0,
[0x68]	0,	0,	0,	0,	0,	0,	0,	0,
[0x70]	0,	0,	0,	0,	0,	0,	0,	0,
[0x78]	0,	'', 	0,	'\b',	0,	0,	0,	0,
};

void reboot(void);

/*
 * Scan code processing
 */
void
kbdputsc(Scan *scan, int c)
{
	Key key;

	/*
	 *  e0's is the first of a 2 character sequence, e1 the first
	 *  of a 3 character sequence (on the safari)
	 */
	if(c == 0xe0){
		scan->esc1 = 1;
		return;
	} else if(c == 0xe1){
		scan->esc2 = 2;
		return;
	}

	key.down = (c & 0x80) == 0;
	key.c = c & 0x7f;

	if(key.c >= Nscan)
		return;

	if(scan->esc1)
		key.r = kbtabesc1[key.c];
	else if(scan->shift)
		key.r = kbtabshift[key.c];
	else if(scan->altgr)
		key.r = kbtabaltgr[key.c];
	else if(scan->ctl)
		key.r = kbtabctl[key.c];
	else
		key.r = kbtab[key.c];

	switch(key.r){
	case Spec|0x60:
		key.r = Kshift;
		break;
	case Spec|0x62:
		key.r = Kctl;
		break;
	case Spec|0x63:
		key.r = Kalt;
		break;
	}

	if(scan->caps && key.r<='z' && key.r>='a')
		key.r += 'A' - 'a';

	if(scan->ctl && scan->alt && key.r == Kdel)
		reboot();

	send(keychan, &key);

	if(scan->esc1)
		scan->esc1 = 0;
	else if(scan->esc2)
		scan->esc2--;

	switch(key.r){
	case Kshift:
		scan->shift = key.down;
		break;
	case Kctl:
		scan->ctl = key.down;
		break;
	case Kaltgr:
		scan->altgr = key.down;
		break;
	case Kalt:
		scan->alt = key.down;
		break;
	case Knum:
		scan->num ^= key.down;
		break;
	case Kcaps:
		scan->caps ^= key.down;
		break;
	}
}

void
setleds(Scan *scan, int leds)
{
	char buf[8];

	if(ledsfd < 0 || scan->leds == leds)
		return;
	leds &= 7;
	snprint(buf, sizeof(buf), "%d", leds);
	pwrite(ledsfd, buf, strlen(buf), 0);
	scan->leds = leds;
}

/*
 * Read scan codes from scanfd
 */ 
void
scanproc(void *)
{
	uchar buf[64];
	Scan scan;
	int i, n;

	threadsetname("scanproc");

	memset(&scan, 0, sizeof scan);
	while((n = read(scanfd, buf, sizeof buf)) > 0){
		for(i=0; i<n; i++)
			kbdputsc(&scan, buf[i]);
		setleds(&scan, (scan.num<<1) | (scan.caps<<2));
	}
}

char*
utfconv(Rune *r, int n)
{
	char *s, *p;
	int l;

	l = runenlen(r, n) + 1;
	s = emalloc9p(l);
	for(p = s; n > 0; r++, n--)
		p += runetochar(p, r);
	*p = 0;
	return s;
}

/*
 * Read key events from keychan and produce characters to
 * rawchan and keystate in kbdchan. this way here is only
 * one global keystate even if multiple keyboards are used.
 */
void
keyproc(void *)
{
	int cb[Nscan];
	Rune rb[Nscan];
	Key key;
	int i, nb;
	char *s;

	threadsetname("keyproc");

	nb = 0;
	while(recv(keychan, &key) > 0){
		if(key.down){
			switch(key.r){
			case 0:
			case Kcaps:
			case Knum:
			case Kshift:
			case Kalt:
			case Kctl:
			case Kaltgr:
				break;
			default:
				nbsend(rawchan, &key.r);
			}
		}

		for(i=0; i<nb && cb[i] != key.c; i++)
			;
		if(!key.down){
			if(i < nb){
				memmove(cb+i, cb+i+1, (nb-i+1) * sizeof(cb[0]));
				memmove(rb+i, rb+i+1, (nb-i+1) * sizeof(rb[0]));
				nb--;
			}
		} else if(i == nb && nb < nelem(cb) && key.r){
			cb[nb] = key.c;
			rb[nb] = key.r;
			nb++;
		}
		s = utfconv(rb, nb);
		if(nbsendp(kbdchan, s) <= 0)
			free(s);
	}
}

/*
 * Read characters from consfd (serial console)
 */ 
void
consproc(void *)
{
	char *p, *e, *x, buf[64];
	int n, cr;
	Rune r;

	threadsetname("consproc");

	cr = 0;
	p = buf;
	e = buf + sizeof(buf);
	while((n = read(consfd, p, e - p)) > 0){
		x = buf + n;
		while(p < x && fullrune(p, x - p)){
			p += chartorune(&r, p);
			if(r){
				if(r == '\n' && cr){
					cr = 0;
					continue;
				}
				if(cr = (r == '\r'))
					r = '\n';
				send(rawchan, &r);
			}
		}
		n = x - p;
		memmove(buf, p, n);
		p = buf + n;
	}
}

/*
 * Cook lines for cons
 */
void
lineproc(void *aux)
{
	Rune rb[256], r;
	Channel *cook;
	int nr, done;
	
	cook = aux;

	threadsetname("lineproc");

	for(;;){
		nr = 0;
		done = 0;
		do {
			recv(cook, &r);
			switch(r){
			case '\0':	/* flush */
				nr = 0;
				continue;
			case '\b':	/* backspace */
			case Knack:	/* ^U */
				while(nr > 0){
					nr--;
					fprint(1, "\b");
					if(r == '\b')
						break;
				}
				continue;
			case Keof:	/* ^D */
				done = 1;
				break;
			case '\n':
				done = 1;
				/* no break */
			default:
				rb[nr++] = r;
				fprint(1, "%C", r);
			}
		} while(!done && nr < nelem(rb));
		sendp(linechan, utfconv(rb, nr));
	}
}

/*
 * Queue reads to cons and kbd, flushing and
 * relay data between 9p and rawchan / kbdchan.
 */
void
ctlproc(void *)
{
	struct {
		Req *h;
		Req **t;
	} qcons, qkbd, *q;
	enum { Areq, Actl, Araw, Aline, Akbd, Aend };
	Alt a[Aend+1];
	Req *req;
	Fid *fid;
	Rune r;
	char *s, *b, *p, *e;
	int c, n, raw;
	Channel *cook;

	threadsetname("ctlproc");

	cook = chancreate(sizeof(Rune), 0);

	if(scanfd >= 0)
		proccreate(scanproc, nil, STACK);
	if(consfd >= 0)
		proccreate(consproc, nil, STACK);

	threadcreate(keyproc, nil, STACK);
	threadcreate(lineproc, cook, STACK);

	raw = 0;

	b = p = e = nil;

	qcons.h = nil;
	qcons.t = &qcons.h;
	qkbd.h = nil;
	qkbd.t = &qkbd.h;

	memset(a, 0, sizeof a);

	a[Areq].c = reqchan;
	a[Areq].v = &req;
	a[Areq].op = CHANRCV;

	a[Actl].c = ctlchan;
	a[Actl].v = &c;
	a[Actl].op = CHANRCV;

	a[Araw].c = rawchan;
	a[Araw].v = &r;
	a[Araw].op = CHANRCV;

	a[Aline].c = linechan;
	a[Aline].v = &s;
	a[Aline].op = CHANRCV;

	a[Akbd].c = kbdchan;
	a[Akbd].v = &s;
	a[Akbd].op = CHANRCV;

	a[Aend].op = CHANEND;

	for(;;){
		s = nil;

		a[Araw].op = (b == nil) ? CHANRCV : CHANNOP;
		a[Aline].op = (b == nil) ? CHANRCV : CHANNOP;
		a[Akbd].op = qkbd.h || !kbdopen ? CHANRCV : CHANNOP;

		switch(alt(a)){
		case Areq:
			fid = req->fid;
			if(req->ifcall.type == Tflush){
				Req **rr;

				fid = req->oldreq->fid;
				q = fid->qid.path == Qcons ? &qcons : &qkbd;
				for(rr = &q->h; *rr && *rr != req->oldreq; rr = &((*rr)->aux))
					;
				if(*rr == req->oldreq){
					if((*rr = req->oldreq->aux) == nil)
						q->t = rr;
					req->oldreq->aux = nil;
					respond(req->oldreq, "interrupted");
				}
				respond(req, nil);
			} else if(req->ifcall.type == Tread){
				q = fid->qid.path == Qcons ? &qcons : &qkbd;
				req->aux = nil;
				*q->t = req;
				q->t = &req->aux;
				goto Havereq;
			} else
				respond(req, Efront);
			break;

		case Actl:
			switch(c){
			case Rawoff:
			case Rawon:
				if(raw = (c == Rawon)){
					while(s = nbrecvp(linechan))
						free(s);
					r = '\0';
					send(cook, &r);
					free(b);
					b = nil;
				}
				break;
			case Kbdflush:
				while(s = nbrecvp(kbdchan))
					free(s);
				break;
			}
			break;

		case Araw:
			if(raw){
				s = emalloc9p(UTFmax+1);
				s[runetochar(s, &r)] = 0;
			} else {
				nbsend(cook, &r);
				break;
			}
			/* no break */

		case Aline:
			b = s;
			p = s;
			e = s + strlen(s);

		Havereq:
			while(b && (req = qcons.h)){
				if((qcons.h = req->aux) == nil)
					qcons.t = &qcons.h;
				n = e - p;
				if(req->ifcall.count < n)
					n = req->ifcall.count;
				req->ofcall.count = n;
				memmove(req->ofcall.data, p, n);
				respond(req, nil);
				p += n;
				if(p >= e){
					free(b);
					b = nil;
				}
			}
			break;

		case Akbd:
			if(req = qkbd.h){
				if((qkbd.h = req->aux) == nil)
					qkbd.t = &qkbd.h;
				n = strlen(s) + 1;
				if(n > req->ifcall.count)
					respond(req, Eshort);
				else {
					req->ofcall.count = n;
					memmove(req->ofcall.data, s, n);
					respond(req, nil);
				}
			}
			free(s);
			break;
		}
	}
}

/*
 * Keyboard layout maps
 */

Rune*
kbmapent(int t, int sc)
{
	if(sc < 0 || sc >= Nscan)
		return nil;
	switch(t){
	default:
		return nil;
	case 0:
		return &kbtab[sc];
	case 1:
		return &kbtabshift[sc];
	case 2:
		return &kbtabesc1[sc];
	case 3:
		return &kbtabaltgr[sc];
	case 4:
		return &kbtabctl[sc];
	}
}

void
kbmapread(Req *req)
{
	char tmp[3*12+1];
	int t, sc, off, n;
	Rune *rp;

	off = req->ifcall.offset/(sizeof(tmp)-1);
	t = off/Nscan;
	sc = off%Nscan;
	if(rp = kbmapent(t, sc))
		sprint(tmp, "%11d %11d %11d\n", t, sc, *rp);
	else
		*tmp = 0;
	n = strlen(tmp);
	if(req->ifcall.count < n)
		n = req->ifcall.count;
	req->ofcall.count = n;
	memmove(req->ofcall.data, tmp, n);
	respond(req, nil);
}

void
kbmapwrite(Req *req)
{
	char line[100], *lp, *b;
	Rune r, *rp;
	int sc, t, l;
	Fid *f;

	f = req->fid;
	b = req->ifcall.data;
	l = req->ifcall.count;
	lp = line;
	if(f->aux){
		strcpy(line, f->aux);
		lp = line+strlen(line);
		free(f->aux);
		f->aux = nil;
	}
	while(--l >= 0) {
		*lp++  = *b++;
		if(lp[-1] == '\n' || lp == &line[sizeof(line)-1]) {
			*lp = 0;
			if(*line == 0){
			Badarg:
				respond(req, Ebadarg);
				return;
			}
			if(*line == '\n' || *line == '#'){
				lp = line;
				continue;
			}
			lp = line;
			while(*lp == ' ' || *lp == '\t')
				lp++;
			t = strtoul(line, &lp, 0);
			sc = strtoul(lp, &lp, 0);
			while(*lp == ' ' || *lp == '\t')
				lp++;
			if((rp = kbmapent(t, sc)) == nil)
				goto Badarg;
			r = 0;
			if(*lp == '\'' && lp[1])
				chartorune(&r, lp+1);
			else if(*lp == '^' && lp[1]){
				chartorune(&r, lp+1);
				if(0x40 <= r && r < 0x60)
					r -= 0x40;
				else
					goto Badarg;
			}else if(*lp == 'M' && ('1' <= lp[1] && lp[1] <= '5'))
				r = 0xF900+lp[1]-'0';
			else if(*lp>='0' && *lp<='9') /* includes 0x... */
				r = strtoul(lp, &lp, 0);
			else
				goto Badarg;
			*rp = r;
			lp = line;
		}
	}
	if(lp != line){
		l = lp-line;
		f->aux = lp = emalloc9p(l+1);
		memmove(lp, line, l);
		lp[l] = 0;
	}
	req->ofcall.count = req->ifcall.count;
	respond(req, nil);
}

/*
 * Filesystem
 */

static char*
getauser(void)
{
	static char user[64];
	int fd;
	int n;

	if(*user)
		return user;
	if((fd = open("/dev/user", OREAD)) < 0)
		strcpy(user, "none");
	else {
		n = read(fd, user, (sizeof user)-1);
		close(fd);
		if(n < 0)
			strcpy(user, "none");
		else
			user[n] = 0;
	}
	return user;
}

static int
fillstat(ulong qid, Dir *d)
{
	struct Qtab *t;

	memset(d, 0, sizeof *d);
	d->uid = getauser();
	d->gid = getauser();
	d->muid = "";
	d->qid = (Qid){qid, 0, 0};
	d->atime = time(0);
	t = qtab + qid;
	d->name = t->name;
	d->qid.type = t->type;
	d->mode = t->mode;
	return 1;
}

static void
fsattach(Req *r)
{
	char *spec;

	spec = r->ifcall.aname;
	if(spec && spec[0]){
		respond(r, Ebadspec);
		return;
	}
	r->fid->qid = (Qid){Qroot, 0, QTDIR};
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	fillstat((ulong)r->fid->qid.path, &r->d);
	r->d.name = estrdup9p(r->d.name);
	r->d.uid = estrdup9p(r->d.uid);
	r->d.gid = estrdup9p(r->d.gid);
	r->d.muid = estrdup9p(r->d.muid);
	respond(r, nil);
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i;
	ulong path;

	path = fid->qid.path;
	switch(path){
	case Qroot:
		if (strcmp(name, "..") == 0) {
			*qid = (Qid){Qroot, 0, QTDIR};
			fid->qid = *qid;
			return nil;
		}
		for(i = fid->qid.path; i<Nqid; i++){
			if(strcmp(name, qtab[i].name) != 0)
				continue;
			*qid = (Qid){i, 0, 0};
			fid->qid = *qid;
			return nil;
		}
		return Enonexist;
		
	default:
		return Ewalk;
	}
}

static void
fsopen(Req *r)
{
	Fid *f;
	static int need[4] = { 4, 2, 6, 1 };
	struct Qtab *t;
	int n;

	f = r->fid;
	t = qtab + f->qid.path;
	n = need[r->ifcall.mode & 3]<<6;
	if((n & t->mode) != n)
		respond(r, Eperm);
	else{
		f->aux = nil;
		switch((ulong)f->qid.path){
		case Qkbd:
			if(kbdopen){
				respond(r, Einuse);
				return;
			}
			kbdopen++;
			sendul(ctlchan, Kbdflush);
			break;
		case Qcons:
			consopen++;
			break;
		case Qconsctl:
			consctlopen++;
			break;
		}
		respond(r, nil);
	}
}

static int
readtopdir(Fid*, uchar *buf, long off, int cnt, int blen)
{
	int i, m, n;
	long pos;
	Dir d;

	n = 0;
	pos = 0;
	for (i = 1; i < Nqid; i++){
		fillstat(i, &d);
		m = convD2M(&d, &buf[n], blen-n);
		if(off <= pos){
			if(m <= BIT16SZ || m > cnt)
				break;
			n += m;
			cnt -= m;
		}
		pos += m;
	}
	return n;
}

static void
fsread(Req *r)
{
	Fid *f;

	f = r->fid;
	switch((ulong)f->qid.path){
	default:
		respond(r, Efront);
		return;

	case Qroot:
		r->ofcall.count = readtopdir(f, (void*)r->ofcall.data, r->ifcall.offset,
			r->ifcall.count, r->ifcall.count);
		break;

	case Qkbd:
	case Qcons:
		sendp(reqchan, r);
		return;

	case Qkbmap:
		kbmapread(r);
		return;
	}
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	Fid *f;
	char *p;
	int n, i;

	f = r->fid;
	switch((ulong)f->qid.path){
	default:
		respond(r, Efront);
		return;

	case Qcons:
		n = r->ifcall.count;
		if(write(1, r->ifcall.data, n) != n){
			responderror(r);
			return;
		}
		r->ofcall.count = n;
		break;

	case Qconsctl:
		p = r->ifcall.data;
		n = r->ifcall.count;
		if(n >= 5 && memcmp(p, "rawon", 5) == 0)
			sendul(ctlchan, Rawon);
		else if(n >= 6 && memcmp(p, "rawoff", 6) == 0)
			sendul(ctlchan, Rawoff);
		else {
			respond(r, Ebadarg);
			return;
		}
		r->ofcall.count = n;
		break;

	case Qkbin:
		if(f->aux == nil){
			f->aux = emalloc9p(sizeof(Scan));
			memset(f->aux, 0, sizeof(Scan));
		}
		for(i=0; i<r->ifcall.count; i++)
			kbdputsc((Scan*)f->aux, (uchar)r->ifcall.data[i]);
		r->ofcall.count = i;
		break;

	case Qkbmap:
		kbmapwrite(r);
		return;

	}
	respond(r, nil);
}

static void
fsflush(Req *r)
{
	switch((ulong)r->oldreq->fid->qid.path) {
	case Qkbd:
	case Qcons:
		sendp(reqchan, r);
		return;
	}
	respond(r, nil);
}

static void
fsdestroyfid(Fid *f)
{
	void *p;

	if(f->omode != -1)
		switch((ulong)f->qid.path){
		case Qkbin:
		case Qkbmap:
			if(p = f->aux){
				f->aux = nil;
				free(p);
			}
			break;
		case Qkbd:
			kbdopen--;
			break;
		case Qcons:
			consopen--;
			break;
		case Qconsctl:
			if(--consctlopen == 0)
				sendul(ctlchan, Rawoff);
			break;
		}
}

static void
fsend(Srv*)
{
	threadexitsall(nil);
}

Srv fs = {
	.attach=			fsattach,
	.walk1=			fswalk1,
	.open=			fsopen,
	.read=			fsread,
	.write=			fswrite,
	.stat=			fsstat,
	.flush=			fsflush,
	.destroyfid=		fsdestroyfid,
	.end=			fsend,
};

void
reboot(void)
{
	int fd;

	if(debug)
		return;

	if((fd = open("/dev/reboot", OWRITE)) < 0){
		fprint(2, "can't open /dev/reboot: %r\n");
		return;
	}
	fprint(fd, "reboot\n");
	close(fd);
}

void
elevate(void)
{
	char buf[128];
	Dir *d, nd;
	int fd;

	if(debug)
		return;

	snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
	if((fd = open(buf, OWRITE)) < 0){
		fprint(2, "can't open %s: %r\n", buf);
		return;
	}

	/* get higher than normal priority */
	fprint(fd, "pri 16\n");

	/* always present in physical memory */
	fprint(fd, "noswap\n");

	/* dont let anybody kill us */
	if(d = dirfstat(fd)){
		nulldir(&nd);
		nd.mode = d->mode & ~0222;
		dirfwstat(fd, &nd);
		free(d);
	}

	close(fd);
	
}

void
usage(void)
{
	fprint(2, "usage: %s [ -dD ] [ -s srv ] [ -m mntpnt ] [ file ]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char** argv)
{
	char *mtpt = "/dev";
	char *srv = nil;

	consfd = -1;

	ARGBEGIN{
	case 'd':
		debug++;
		break;
	case 'D':
		chatty9p++;
		break;
	case 's':
		srv = EARGF(usage());
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if((scanfd = open("/dev/scancode", OREAD)) < 0)
		fprint(2, "%s: warning: can't open /dev/scancode: %r\n", argv0);
	if((ledsfd = open("/dev/leds", OWRITE)) < 0)
		fprint(2, "%s: warning: can't open /dev/leds: %r\n", argv0);

	if(*argv)
		if((consfd = open(*argv, OREAD)) < 0)
			fprint(2, "%s: warning: can't open %s: %r\n", argv0, *argv);

	keychan = chancreate(sizeof(Key), 8);
	reqchan = chancreate(sizeof(Req*), 0);
	ctlchan = chancreate(sizeof(int), 0);
	rawchan = chancreate(sizeof(Rune), 32);
	linechan = chancreate(sizeof(char*), 16);
	kbdchan = chancreate(sizeof(char*), 16);

	if(!(keychan && reqchan && ctlchan && rawchan && linechan && kbdchan))
		sysfatal("allocating chans");

	elevate();
	procrfork(ctlproc, nil, STACK, RFNAMEG|RFNOTEG);
	threadpostmountsrv(&fs, srv, mtpt, MBEFORE);
}
