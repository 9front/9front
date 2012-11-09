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
	Qkbdin,
	Qkbin,
	Qkbmap,
	Qcons,
	Qconsctl,
	Nqid,

	Rawon=	0,
	Rawoff,

	STACK = 8*1024,
};

typedef struct Key Key;
typedef struct Scan Scan;

struct Key {
	int	down;
	Rune	b;	/* button, unshifted key */
	Rune	r;	/* rune, shifted key */
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

	"kbdin",
		0200,
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
char Ephase[] = "the front fell off";
char Eintr[] = "interrupted";

int scanfd;
int ledsfd;
int consfd;
int mctlfd;
int notefd;

int kbdopen;
int consctlopen;
int quiet = 0;

int debug;

Channel *keychan;	/* chan(Key) */

Channel *kbdreqchan;	/* chan(Req*) */
Channel *consreqchan;	/* chan(Req*) */

Channel *ctlchan;	/* chan(int) */

Channel *rawchan;	/* chan(Rune) */
Channel *runechan;	/* chan(Rune) */

Channel *conschan;	/* chan(char*) */
Channel *kbdchan;	/* chan(char*) */

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
[0x40]	KF|6,	KF|7,	KF|8,	KF|9,	KF|10,	Knum,	Kscroll,'7',
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
[0x40]	KF|6,	KF|7,	KF|8,	KF|9,	KF|10,	Knum,	Kscroll,'7',
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
[0x28]	0,	0,	0,	0,	0,	0,	0,	0,
[0x30]	0,	0,	0,	0,	0,	'/',	0,	Kprint,
[0x38]	Kaltgr,	0,	0,	0,	0,	0,	0,	0,
[0x40]	0,	0,	0,	0,	0,	0,	Kbreak,	Khome,
[0x48]	Kup,	Kpgup,	0,	Kleft,	0,	Kright,	0,	Kend,
[0x50]	Kdown,	Kpgdown,Kins,	Kdel,	0,	0,	0,	0,
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
[0x50]	Kdown,	Kpgdown,Kins,	Kdel,	0,	0,	0,	0,
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
	 *  e0's is the first of a 2 character sequence, e1 and e2 the first
	 *  of a 3 character sequence (on the safari)
	 */
	if(scan->esc2){
		scan->esc2--;
		return;
	} else if(c == 0xe1 || c == 0xe2){
		scan->esc2 = 2;
		return;
	} else if(c == 0xe0){
		scan->esc1 = 1;
		return;
	}

	key.down = (c & 0x80) == 0;
	c &= 0x7f;

	if(c >= Nscan)
		return;

	if(scan->esc1)
		key.r = kbtabesc1[c];
	else if(scan->shift)
		key.r = kbtabshift[c];
	else if(scan->altgr)
		key.r = kbtabaltgr[c];
	else if(scan->ctl)
		key.r = kbtabctl[c];
	else
		key.r = kbtab[c];

	if(scan->esc1 || kbtab[c] == 0)
		key.b = key.r;
	else
		key.b = kbtab[c];

	if(scan->caps && key.r<='z' && key.r>='a')
		key.r += 'A' - 'a';

	if(scan->ctl && scan->alt && key.r == Kdel)
		reboot();

	if(key.b)
		send(keychan, &key);

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
	scan->esc1 = 0;
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
 * rawchan and keystate in kbdchan.
 */
void
keyproc(void *)
{
	Rune rb[Nscan+1];
	Key key;
	int i, nb;
	char *s;

	threadsetname("keyproc");

	nb = 0;
	while(recv(keychan, &key) > 0){
		rb[0] = 0;
		for(i=0; i<nb && rb[i+1] != key.b; i++)
			;
		if(!key.down){
			while(i < nb && rb[i+1] == key.b){
				memmove(rb+i+1, rb+i+2, (nb-i+1) * sizeof(rb[0]));
				nb--;
				rb[0] = 'K';
			}
		} else if(i == nb && nb < nelem(rb)-1 && key.b){
			rb[++nb] = key.b;
			rb[0] = 'k';
		}
		if(rb[0]){
			if(kbdopen){
				s = utfconv(rb, nb+1);
				if(nbsendp(kbdchan, s) <= 0)
					free(s);
			}
			if(mctlfd >= 0){
				if(key.r == Kshift){
					if(key.down){
						fprint(mctlfd, "buttonmap 132");
					} else {
						fprint(mctlfd, "swap");
						fprint(mctlfd, "swap");
					}
				}
				fprint(mctlfd, "twitch");
			}
		}
		if(key.down && key.r)
			send(rawchan, &key.r);
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
				if(r == 021 || r == 023)	/* XON/XOFF */
					continue;
				if(r == '\n' && cr){
					cr = 0;
					continue;
				}
				if(cr = (r == '\r'))
					r = '\n';
				send(runechan, &r);
			}
		}
		n = x - p;
		memmove(buf, p, n);
		p = buf + n;
	}
}

static int
nextrune(Channel *ch, Rune *r)
{
	while(recv(ch, r) > 0){
		switch(*r){
		case 0:
		case Kcaps:
		case Knum:
		case Kshift:
		case Kaltgr:
			/* ignore modifiers */
			continue;

		case Kctl:
		case Kalt:
			/* composing escapes */
			return 1;
		}
		return 0;
	}
	return -1;
}

/*
 * Read runes from rawchan, possibly compose special characters
 * and output the new runes to runechan
 */
void
runeproc(void *)
{
	static struct {
		char	*ld;	/* must be seen before using this conversion */
		char	*si;	/* options for last input characters */
		Rune	*so;	/* the corresponding Rune for each si entry */
	} tab[] = {
#include "latin1.h"
	};
	Rune r, rr;
	int i, j;
	int ctl;

	threadsetname("runeproc");

	ctl = 0;
	while((i = nextrune(rawchan, &r)) >= 0){
		if(i == 0){
			ctl = 0;
Forward:
			send(runechan, &r);
			continue;
		}

		if(r == Kctl){
			ctl = 1;
			continue;
		}

		/*
		 * emulators like qemu and vmware use Ctrl+Alt to lock
		 * keyboard input so dont confuse them for a compose
		 * sequence.
		 */
		if(r != Kalt || ctl)
			continue;

		if(nextrune(rawchan, &r))
			continue;

		if(r == 'X'){
			r = 0;
			for(i = 0; i<4; i++){
				if(nextrune(rawchan, &rr))
					break;
				r <<= 4;
				if(rr >= '0' && rr <= '9')
					r |= (rr - '0');
				else if(rr >= 'a' && rr <= 'f')
					r |= 10 + (rr - 'a');
				else if(rr >= 'A' && rr <= 'F')
					r |= 10 + (rr - 'A');
				else
					break;
			}
			if(i == 4 && r)
				goto Forward;
		} else {
			if(nextrune(rawchan, &rr))
				continue;
			for(i = 0; i<nelem(tab); i++){
				if(tab[i].ld[0] != r)
					continue;
				if(tab[i].ld[1] == 0)
					break;	
				if(tab[i].ld[1] == rr){
					nextrune(rawchan, &rr);
					break;
				}
			}
			if(i == nelem(tab) || rr == 0)
				continue;
			for(j = 0; tab[i].si[j]; j++){
				if(tab[i].si[j] != rr)
					continue;
				r = tab[i].so[j];
				goto Forward;
			}
		}
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
	char *s;

	cook = aux;

	threadsetname("lineproc");

	for(;;){
		nr = 0;
		done = 0;
		do {
			recv(cook, &r);
			switch(r){
			case Kdel:
				if(notefd < 0)
					continue;
				write(notefd, "interrupt", 9);
				/* no break */
			case '\0':	/* flush */
				nr = 0;
				continue;
			case Kbs:	/* ^H: erase character */
			case Knack:	/* ^U: erase line */
			case Ketb:	/* ^W: erase word */
				while(nr > 0){
					nr--;
					fprint(1, "\b");
					if(r == Kbs)
						break;
					if(r == Ketb && utfrune(" \t", rb[nr]))
						break;
				}
				continue;
			case Keof:	/* ^D: eof */
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
		s = utfconv(rb, nr);
		if(nbsendp(conschan, s) <= 0)
			free(s);
	}
}

/*
 * Reads Tread and Tflush requests from reqchan and responds
 * to them with data received on the string channel.
 */
void
reqproc(void *aux)
{
	enum { AREQ, ASTR, AEND };
	Alt a[AEND+1];
	Channel **ac;
	Req *r, *q, **qq;
	char *s, *p, *e;
	int n;

	threadsetname("reqproc");

	s = nil;
	p = nil;

	q = nil;
	qq = &q;

	ac = aux;
	a[AREQ].op = CHANRCV;
	a[AREQ].c = ac[0];	/* chan(Req*) */
	a[AREQ].v = &r;

	a[ASTR].c = ac[1];	/* chan(char*) */
	a[ASTR].v = &s;

	a[AEND].op = CHANEND;

	for(;;){
		a[ASTR].op = s ? CHANNOP : CHANRCV;

		switch(alt(a)){
		case AREQ:
			if(r->ifcall.type == Tflush){
				Req **rr, **xx;

				for(rr = &q; *rr; rr=xx){
					xx = &((*rr)->aux);
					if(*rr == r->oldreq){
						if((*rr = *xx) == nil)
							qq = rr;
						respond(r->oldreq, Eintr);
						break;
					}
				}
				respond(r, nil);
				continue;
			} else if(r->ifcall.type != Tread){
				respond(r, Ephase);
				continue;
			}
			r->aux = nil;
			*qq = r;
			qq = &r->aux;

			if(0){
		case ASTR:
				p = s;
			}

			while(s && q){
				r = q;
				if((q = q->aux) == nil)
					qq = &q;

				e = s + strlen(s);
				if(p == s && r->fid->qid.path == Qkbd)
					e++; /* send terminating \0 if its kbd file */
				n = e - p;
				if(n > r->ifcall.count)
					n = r->ifcall.count;

				r->ofcall.count = n;
				memmove(r->ofcall.data, p, n);
				respond(r, nil);

				p += n;
				if(p >= e){
					free(s);
					s = nil;
				}
			}
		}
	}
}

/*
 * Keep track of rawing state and distribute the runes from
 * runechan to the right channels depending on the state.
 */
void
ctlproc(void *)
{
	Channel *cook, *aconsr[2], *akbdr[2];
	enum { ACTL, ARUNE, AEND };
	Alt a[AEND+1];
	Rune r;
	int c, raw;
	char *s;

	threadsetname("ctlproc");

	if(scanfd >= 0)
		proccreate(scanproc, nil, STACK);	/* scanfd -> keychan */
	if(consfd >= 0)
		proccreate(consproc, nil, STACK);	/* consfd -> runechan */

	threadcreate(keyproc, nil, STACK);		/* keychan -> rawchan, kbdchan */
	threadcreate(runeproc, nil, STACK);		/* rawchan -> runechan */

	aconsr[0] = consreqchan;
	aconsr[1] = conschan;
	threadcreate(reqproc, aconsr, STACK);		/* consreqchan,conschan -> respond */

	akbdr[0] = kbdreqchan;
	akbdr[1] = kbdchan;
	threadcreate(reqproc, akbdr, STACK);		/* kbdreqchan,kbdchan -> respond */

	cook = chancreate(sizeof(Rune), 0);
	threadcreate(lineproc, cook, STACK);		/* cook -> conschan */

	raw = 0;

	a[ACTL].c = ctlchan;
	a[ACTL].v = &c;
	a[ACTL].op = CHANRCV;

	a[ARUNE].c = runechan;
	a[ARUNE].v = &r;
	a[ARUNE].op = CHANRCV;

	a[AEND].op = CHANEND;

	for(;;){
		switch(alt(a)){
		case ACTL:
			switch(c){
			case Rawoff:
			case Rawon:
				if(raw = (c == Rawon)){
					r = 0;
					nbsend(cook, &r);
				}
			}
			break;
		case ARUNE:
			if(kbdopen){
				s = emalloc9p(UTFmax+2);
				s[0] = 'c';
				s[1+runetochar(s+1, &r)] = 0;
				if(nbsendp(kbdchan, s) <= 0)
					free(s);
				break;
			}
			if(raw){
				s = emalloc9p(UTFmax+1);
				s[runetochar(s, &r)] = 0;
				if(nbsendp(conschan, s) <= 0)
					free(s);
				break;
			}
			nbsend(cook, &r);
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

Rune
kbcompat(Rune r)
{
	static Rune o = Spec|0x60, tab[] = {
		Kshift, Kbreak, Kctl, Kalt,
		Kcaps, Knum, Kmiddle, Kaltgr,
	};
	if(r >= o && r < o+nelem(tab))
		return tab[r - o];
	return r;
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
			*rp = kbcompat(r);
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
		respond(r, Ephase);
		return;
	case Qroot:
		r->ofcall.count = readtopdir(f, (void*)r->ofcall.data, r->ifcall.offset,
			r->ifcall.count, r->ifcall.count);
		break;
	case Qkbd:
		sendp(kbdreqchan, r);
		return;
	case Qcons:
		sendp(consreqchan, r);
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
	Scan *a;
	char *p, *s;
	int n, i;
	Key k;

	f = r->fid;
	switch((ulong)f->qid.path){
	default:
		respond(r, Ephase);
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

	case Qkbdin:
		p = r->ifcall.data;
		n = r->ifcall.count;
		r->ofcall.count = n;
		if(n == 0)
			break;
		if(p[n-1] != 0){
			/*
			 * old format as used by bitsy keyboard:
			 * just a string of characters, no keyup
			 * information.
			 */
			s = emalloc9p(n+1);
			memmove(s, p, n);
			s[n] = 0;
			p = s;
			while(*p){
				p += chartorune(&k.r, p);
				if(k.r)
					send(rawchan, &k.r);
			}
			free(s);
			break;
		}
		switch(p[0]){
		case 'R':
		case 'r':
			/* rune up/down */
			chartorune(&k.r, p+1);
			if(k.r == 0)
				break;
			k.b = k.r;
			k.down = (p[0] == 'r');
			if(f->aux == nil){
				f->aux = emalloc9p(sizeof(Scan));
				memset(f->aux, 0, sizeof(Scan));
			}
			a = f->aux;
			/*
			 * handle ^X forms according to keymap and
			 * assign button.
			 */
			for(i=0; i<Nscan; i++){
				if((a->shift && kbtabshift[i] == k.r) || (kbtab[i] == k.r)){
					if(kbtab[i])
						k.b = kbtab[i];
					if(a->shift)
						k.r = kbtabshift[i];
					else if(a->altgr)
						k.r = kbtabaltgr[i];
					else if(a->ctl)
						k.r = kbtabctl[i];
					break;
				}
			}
			if(k.b)
				send(keychan, &k);
			if(k.r == Kshift)
				a->shift = k.down;
			else if(k.r == Kaltgr)
				a->altgr = k.down;
			else if(k.r == Kctl)
				a->ctl = k.down;
			break;
		default:
			if(!kbdopen)
				break;
			s = emalloc9p(n);
			memmove(s, p, n);
			if(nbsendp(kbdchan, s) <= 0)
				free(s);
		}
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
		sendp(kbdreqchan, r);
		return;
	case Qcons:
		sendp(consreqchan, r);
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
		case Qkbdin:
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

int
eopen(char *name, int mode)
{
	int fd;

	fd = open(name, mode);
	if(fd < 0 && !quiet)
		fprint(2, "%s: warning: can't open %s: %r\n", argv0, name);
	return fd;
}

void
reboot(void)
{
	int fd;

	if(debug)
		return;

	if((fd = eopen("/dev/reboot", OWRITE)) < 0)
		return;
	fprint(fd, "reboot\n");
	close(fd);
}

int
procopen(int pid, char *name, int mode)
{
	char buf[128];

	snprint(buf, sizeof(buf), "/proc/%d/%s", pid, name);
	return eopen(buf, mode);
}

void
elevate(void)
{
	Dir *d, nd;
	int fd;

	if(debug)
		return;

	if((fd = procopen(getpid(), "ctl", OWRITE)) < 0)
		return;

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
	fprint(2, "usage: %s [ -qdD ] [ -s srv ] [ -m mntpnt ] [ file ]\n", argv0);
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
	case 'q':
		quiet++;
		break;
	default:
		usage();
	}ARGEND

	notefd = procopen(getpid(), "notepg", OWRITE);

	scanfd = eopen("/dev/scancode", OREAD);
	ledsfd = eopen("/dev/leds", OWRITE);
	mctlfd = eopen("/dev/mousectl", OWRITE);

	if(*argv)
		consfd = eopen(*argv, OREAD);

	consreqchan = chancreate(sizeof(Req*), 0);
	kbdreqchan = chancreate(sizeof(Req*), 0);

	keychan = chancreate(sizeof(Key), 8);
	ctlchan = chancreate(sizeof(int), 0);
	rawchan = chancreate(sizeof(Rune), 0);
	runechan = chancreate(sizeof(Rune), 32);
	conschan = chancreate(sizeof(char*), 16);
	kbdchan = chancreate(sizeof(char*), 16);

	elevate();
	procrfork(ctlproc, nil, STACK, RFNAMEG|RFNOTEG);
	threadpostmountsrv(&fs, srv, mtpt, MBEFORE);
}
