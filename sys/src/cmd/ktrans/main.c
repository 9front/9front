/*
 *   Mostly based on the original source codes of Plan 9 release 2
 *   distribution.
 *             by Kenji Okamoto, August 4 2000
 *                   Osaka Prefecture Univ.
 *                   okamoto@granite.cias.osakafu-u.ac.jp
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include "ktrans.h"
#include "jisho.h"

#define	LSIZE	256

Rune	lbuf[LSIZE];		/* hiragana buffer for key input written by send() */
Map	*table = hira;		/* default language conversion table */
uchar	okurigana[LSIZE];	/* buffer for okurigana */
char	okuri = 0;		/* buffer/flag for capital input char */
int	in, out;
int	llen, olen, joshi = 0;
int	natural = 1;		/* not Japanese but English mode */

int	changelang(int);
int	dotrans(Dictionary*);
int	nrune(char *);
void	send(uchar *, int);
Map	*match(uchar *p, int *nc, Map *table);

extern Dictionary *openQDIC(char *);
extern KouhoList *getKouhoHash(Dictionary*, char *);
extern KouhoList *getKouhoFile(DicList*, char *);
extern void freeQDIC(Dictionary*);
extern void selectKouho(KouhoList **, KouhoList*);

void
kbdopen(void)
{
	int n, kinfd, koutfd, fd[2];
	char buf[128];
	int kbd;

	kbd = 1;
	if((kinfd = open("/dev/kbd", OREAD)) < 0){
		kbd = 0;
		if((kinfd = open("/dev/cons", OREAD)) < 0)
			sysfatal("open kbd: %r");
	}
	if(bind("#|", "/n/temp", MREPL) < 0)
		sysfatal("bind /n/temp: %r");
	if((koutfd = open("/n/temp/data1", OWRITE)) < 0)
		sysfatal("open kbd pipe: %r");
	if(bind("/n/temp/data", kbd? "/dev/kbd": "/dev/cons", MREPL) < 0)
		sysfatal("bind kbd pipe: %r");
	unmount(nil, "/n/temp");
	if(!kbd){
		in = kinfd;
		out = koutfd;
		return;
	}
	if(pipe(fd) < 0)
		sysfatal("pipe: %r");
	if(fork()){
		in = out = fd[0];
		close(fd[1]);
		close(kinfd);
		close(koutfd);
		return;
	}
	close(fd[0]);
	if(fork()){
		Biobuf b;
		long r;

		Binit(&b, fd[1], OREAD);
		while((r = Bgetrune(&b)) >= 0){
			n = snprint(buf, sizeof(buf), "c%C", (Rune)r)+1;
			write(koutfd, buf, n); /* pass on result */
		}
	} else
		while((n = read(kinfd, buf, sizeof(buf))) > 0){
			buf[n-1] = 0;
			if(n < 2 || buf[0] != 'c')
				write(koutfd, buf, n); /* pass on */
			else
				write(fd[1], buf+1, n-2); /* to translator */
		}
	exits(nil);
}

void
usage(void)
{
	fprint(2, "usage: %s\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{

	uchar *bp, *ep, buf[128];
	Map *mp;
	int nchar, wantmore;
	int n, c;
	char *dictname;
	Dictionary *jisho;

	ARGBEGIN{
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	if((dictname = getenv("jisho")) == nil)
		dictname = "/lib/kanji.jisho";
	jisho = openQDIC(dictname);

	kbdopen();
	if(fork())
		exits(nil); /* parent process will exit */

	bp = ep = buf;
	wantmore = 0;
	for (;;) { /* key board input loop */
	getmore:
		if (bp>=ep || wantmore) {
			if (wantmore==0)
				bp = ep = buf; /* clear all */
			n = read(in, ep, &buf[sizeof(buf)]-ep);
			if (n<=0)
				exits("");
			ep += n;
			*ep = '\0';
		}
		while (bp<ep) { /* there are input data */
			if (table == hira && natural != 1 && (*bp>'A' && *bp<='Z') && ep-bp<2
			   && !strchr("EIOU", *bp)) {
				wantmore = 1;
				goto getmore;
			}
			if (!fullrune((char *)bp, ep-bp)) { /* not enough length of input */
				wantmore = 1;
				goto getmore;
			}
			wantmore = 0;

			if (*bp=='') { /* ^x read ktrans-jisho once more */
				freeQDIC(jisho);
				jisho = openQDIC(dictname);
				llen = 0;
				olen = okuri = joshi = 0;
				wantmore=0;
				bp=ep=buf;
				continue;
			}
			if (*bp=='') { /* ^\ (start translation command) */
				c = dotrans(jisho);
				if (c)
					*bp = c; /* pointer to translated rune */
				else
					bp++;
				continue;
			}
			if (*bp=='') { /* ^l (no translate command) */
				bp++;
				llen = 0;
				olen = okuri = joshi = 0;
				continue;
			}
			if (changelang(*bp)) { /* change language mode OK */
				bp++;
				olen = okuri = joshi = 0;
				continue;
			}
			if (natural || *bp<=' ' || *bp>='{') { /* English mode but not ascii */
				Rune r;
				int rlen = chartorune(&r, (char *)bp);
				send(bp, rlen); /* write bp to /dev/cons */
				bp += rlen;
				continue;
			}
			if (table == hira && (*bp >= 'A' && *bp <= 'Z') && (*(bp+1) < 'A'
			   || *(bp+1) > 'Z')) {
				*bp = okuri = tolower(*bp);
				joshi = olen = 0;
			} else if (table == hira && (*bp >= 'A' && *bp <= 'Z') &&
			   (*(bp+1) >= 'A' && *(bp+1) <= 'Z')) {
				*bp = okuri = tolower(*bp);
				*(bp+1) = tolower(*(bp+1));
				joshi = 1;
				olen = 0;
			}
			mp = match(bp, &nchar, table);
			if (mp == 0) {
				if (nchar>0) { /* match, longer possible */
					wantmore++;
					break;
				}
				send(bp++, 1); /* alphabet in kana mode */
			} else {
				send((uchar*)mp->kana, strlen(mp->kana));
				bp += nchar;
			}
		}
	}
}

int
min(int a, int b)
{
	return a<b? a: b;
}

/*
 * send UTF string (p) with length (n) to stdout
 * and write rune (r) in global lbuf[] buffer
 * or okurigana[] buffer if okuri (verb or joshi) mode
 */
void
send(uchar *p, int n)
{
	Rune r;
	uchar *ep;

	if (write(out, (char*)p, n) != n)
		sysfatal("write: %r");

	if (llen>LSIZE-64) {
		memmove((char*)lbuf, (char*)lbuf+64, 64*sizeof(Rune));
		llen -= 64;
	}

	if (table!=hira || natural)
		return;

	ep = p+n;
	if(okuri)
		while (olen<LSIZE && p<ep)
			okurigana[olen++] = *p++;
	else
		while (llen<LSIZE && p<ep) {
			p += chartorune(&r, (char*)p);
			if (r=='\b') {
				if (llen>0)
				llen--;
				continue;
			}
			if (r==0x80) /* ignore view key */
				continue;
			lbuf[llen++] = r;
	   }
}

/*
 * Romaji to Hiragana/Katakana conversion
 *     romaji shoud be input as small letter
 *     returns the matched address in table, hira, kata, etc.
 *     nc: number of character (return value)
 */
Map *
match(uchar *p, int *nc, Map *table)
{
	register Map *longp = 0, *kp;
	static char last;
	int longest = 0;

	*nc = -1;
	for (kp=table; kp->roma; kp++) {
		if (*p == *kp->roma) {
			int lr = strlen(kp->roma);
			int len = min(lr, strlen((char *)p));
			if (strncmp(kp->roma, (char *)p, len)==0) {
				if (len<lr) {
					*nc = 1;
					return 0;
				}
				if (len>longest) {
					longest = len;
					longp = kp;
				}
			}
		}
	}
	if (longp) {
		last = longp->roma[longest-1];
		*nc = longp->advance;
	}
	return longp;
}

int
changelang(int c)
{
	switch(c){
	case '': /* ^t (English mode) */
		natural = 1;
		llen = 0;
		return 1;
		break;

	case '': /* ^n (Japanese hiragana mode ) */
		natural = 0;
		table = hira;
		llen = 0;
		return 1;
		break;

	case '': /* ^k (Japanese katakana mode) */
		natural = 0;
		table = kata;
		llen = 0;
		return 1;
		break;

	case '': /* ^r (Russian mode) */
		natural = 0;
		table = cyril;
		llen = 0;
		return 1;
		break;

	case '': /* ^o (Greek mode) */
		natural = 0;
		table = greek;
		llen = 0;
		return 1;
		break;

	case '': /* ^s (Korean mode) */
		natural = 0;
		table = hangul;
		llen = 0;
		return 1;
		break;
	}
	return 0;
}

/*
 * write translated kanji runes to stdout and return last character
 * if it's not ctl-\. if the last is ctl-\, proceed with
 * translation of the next kouho
 */
int
dotrans(Dictionary *dic)
{
	Rune *res, r[1];
	char v[1024], *p, tbuf[64], hirabuf[64];
	int j, lastlen, nokouho = 0;
	char ch;
	KouhoList *fstkouho, *currentkouho;

	if (llen==0)
		return 0; /* don't use kanji transform function */
	if (okuri && joshi != 1) {
		   lbuf[llen++] = (Rune)okuri;
		   lbuf[llen] = 0;
	}else
		lbuf[llen] = 0;
	okurigana[olen] = 0;

	/*
	 * search the matched index for the key word in the dict hash table, and
	 * return a pointer to the matched kouho, 0 otherwise.
	 */
	res = lbuf;
	for (j=0; *res != L'\0'; j += runetochar(v+j, res++))
		;
	v[j] = '\0';
	strcpy(tbuf, v);
	strcpy(hirabuf, v); /* to remember the initial hiragana input */

	if (okuri && joshi != 1) /* verb mode */
		hirabuf[strlen(hirabuf) - 1] = '\0';

	if(!(fstkouho = getKouhoHash(dic, v))) { /* not found */
		llen = olen = okuri = joshi = 0;
		okurigana[0] = 0;
		return 0;
	}

	currentkouho = fstkouho;
	for(;;) {
		p = currentkouho->kouhotop; /* p to the head of kanji kouho array */
		lastlen = nrune(tbuf); /* number of rune chars */

		if (okuri && joshi != 1) /* verb mode */
	   		for (j=0; j<lastlen-1; j++)
				write(out, "\b", 1); /* clear hiragana input */
		else
			for (j=0; j<lastlen; j++)
				write(out, "\b", 1); /* clear hiragana input */

		if (okuri) {
			lastlen = nrune((char *)okurigana);
			for (j=0; j<lastlen; j++)
				write(out, "\b", 1);
		}

		write(out, p, strlen(p)); /* write kanji to stdout */
		if (okuri)
			write(out, (char *)okurigana, olen);

		if (read(in, &ch, 1)<=0) /* read from stdin */
			exits(nil);

		if (ch == '') { /* if next input is ^\, once again */
			if(currentkouho->nextkouho != 0) { /* have next kouho */
				nokouho = 0;
				strcpy(tbuf, p);
				currentkouho = currentkouho->nextkouho;

				if (okuri && joshi != 1) /* verb mode */
					for (j=0; j<nrune(tbuf); j++)
						write(out, "\b", 1);
				continue;
			} else { /* the last kouho */
				if (okuri) {
					lastlen = nrune((char *)okurigana);
					for (j=0; j<lastlen; j++)
						write(out, "\b", 1);
				}

				for (lastlen=0; *p != 0; p += j) {
					j = chartorune(r, p);
					lastlen++;
				}

				for (j=0; j<lastlen; j++)
					write(out, "\b", 1);

				if(hirabuf[0])
					write(out, hirabuf, strlen(hirabuf));

				if(okurigana[0])
					write(out, (char *)okurigana, olen);

				olen = okuri = joshi = 0;
				okurigana[0] = 0;
				break;
			}
		} else {
			if(!nokouho) /* learn the previous use of the kouho */
				selectKouho(&(fstkouho->dicitem->kouho), currentkouho);

			olen = okuri = joshi = 0;
			okurigana[0] = 0;
			break;
		}
	}
	llen = 0;
	return ch;
}

/*
 * returns the number of characters in the pointed Rune
 */
int
nrune(char *p)
{
	int n = 0;
	Rune r;

	while (*p) {
		p += chartorune(&r, p);
		n++;
	}
	return n;
}
