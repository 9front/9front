/*
 *   Mostly based on the original source codes of Plan 9 release 2
 *   distribution.
 *             by Kenji Okamoto, August 4 2000
 *                   Osaka Prefecture Univ.
 *                   okamoto@granite.cias.osakafu-u.ac.jp
 */

/*
 * A glossary on some of the Japanese vocabulary used:
 * kana: syllabic letting, either hiragana(ひらがな) or katakana(カタカナ)
 * kanji(漢字): borrowed characters, 楽 in 楽しい
 * Okurigana(送り仮名): kana tail to kanji, しい in 楽しい
 * Joshi(助詞): particle, は in 私は
 * Jisho(辞書): dictionary
 * kouho(候補): candidate
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include "hash.h"
#include "ktrans.h"

#define	LSIZE	256

Rune	lbuf[LSIZE];		/* hiragana buffer for key input written by send() */
Hmap	*table;
uchar	okurigana[LSIZE];	/* buffer for okurigana */
char	okuri = 0;		/* buffer/flag for capital input char */
int	in, out;
int	llen, olen, joshi = 0;
int	natural = 1;		/* not Japanese but English mode */

int	changelang(int);
int	dotrans(Hmap*);
int	nrune(char *);
void	send(uchar *, int);
Hmap*	opendict(Hmap *, char *);

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

Map signalmore = {
	"_", nil, 1,
};

Hmap*
initmap(Map *m, int n)
{
	int i, j;
	char buf[16];
	char *s;
	Map prev;
	Hmap *h;

	h = hmapalloc(n, sizeof(Map));
	for(i = 0; i < n; i++){
		if(m[i].roma == nil || m[i].roma[0] == '\0')
			continue;

		//We mark all partial strings so we know when
		//we have partial match when ingesting.
		j = 2;
		for(s = m[i].roma; *s && j <= sizeof buf; s++){
			snprint(buf, j, "%s", m[i].roma);
			prev = m[i];
			if(hmapget(h, buf, &prev) == 0){
				if(prev.leadstomore == 1 && s[1] == '\0'){
					//confict; partial & valid input
					prev = m[i];
					prev.leadstomore = 1;
				}
			}

			if(s[1] == '\0'){
				hmaprepl(&h, strdup(buf), &prev, nil, 1);
			} else {
				hmaprepl(&h, strdup(buf), &signalmore, nil, 1);
			}
			j++;
		}
	}
	return h;
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
	Map lkup, last;
	int wantmore;
	int n, c;
	char *jishoname, *zidianname;
	Hmap *jisho, *zidian;

	ARGBEGIN{
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	if((jishoname = getenv("jisho")) == nil)
		jishoname = "/lib/kanji.jisho";
	jisho = opendict(nil, jishoname);

	if((zidianname = getenv("zidian")) == nil)
		zidianname = "/lib/hanzi.zidian";
	zidian = opendict(nil, zidianname);

	hira 	= table = initmap(mhira, nelem(mhira));
	kata 	= initmap(mkata, nelem(mkata));
	greek 	= initmap(mgreek, nelem(mgreek));
	cyril 	= initmap(mcyril, nelem(mcyril));
	hangul 	= initmap(mhangul, nelem(mhangul));
	last = (Map){nil, nil, -1};

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
				jisho = opendict(jisho, jishoname);
				zidian = opendict(zidian, zidianname);
				llen = 0;
				olen = okuri = joshi = 0;
				wantmore=0;
				bp=ep=buf;
				continue;
			}
			if (*bp=='') { /* ^\ (start translation command) */
				if (table == hanzi)
					c = dotrans(zidian);
				else
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
				last.kana = nil;
				continue;
			}
			if (changelang(*bp)) { /* change language mode OK */
				bp++;
				olen = okuri = joshi = 0;
				last.kana = nil;
				continue;
			}
			if (natural || *bp<=' ' || *bp>='{') { /* English mode but not ascii */
				Rune r;
				int rlen = chartorune(&r, (char *)bp);
				send(bp, rlen); /* write bp to /dev/cons */
				bp += rlen;
				last.kana = nil;
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
			if(hmapget(table, (char*)bp, &lkup) < 0){
				if(last.kana != nil){
					send((uchar*)last.kana, strlen(last.kana));
					bp += strlen(last.roma);
				} else
					send(bp++, 1);
				last.kana = nil;
				break;
			}
			/* concatinations; only advance a single character */
			if(lkup.kana != nil && strstr("ッっ", lkup.kana))
				lkup.roma = "_";
			/* partial match */
			if(lkup.kana == nil || lkup.leadstomore == 1){
				if(lkup.kana != nil)
					last = lkup;

				wantmore = 1;
				break;
			}
			last.kana = nil;
			send((uchar*)lkup.kana, strlen(lkup.kana));
			bp += strlen(lkup.roma);
		}
	}
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

	if(table != hira && table != hanzi)
		return;
	if(natural && table != hanzi)
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

int
changelang(int c)
{
	switch(c){
	case '': /* ^t (English mode) */
		natural = 1;
		table = hira;
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

	case '': /* ^c (Chinese mode) */
		natural = 1;
		table = hanzi;
		llen = 0;
		return 1;
		break;
	}
	return 0;
}

Hmap*
opendict(Hmap *h, char *name)
{
	Biobuf *b;
	char *p;
	char *dot, *rest;
	char *kouho[16];
	int i;

	b = Bopen(name, OREAD);
	if(b == nil)
		return nil;

	if(h == nil)
		h = hmapalloc(8192, sizeof(kouho));
	else
		hmapreset(h, 1);
	while(p = Brdstr(b, '\n', 1)){
		if(p[0] == '\0' || p[0] == ';'){
		Err:
			free(p);
			continue;
		}
		dot = utfrune(p, '\t');
		if(dot == nil)
			goto Err;

		*dot = '\0';
		rest = dot+1;
		if(*rest == '\0')
			goto Err;

		memset(kouho, 0, sizeof kouho);
		i = 0;
		while(i < nelem(kouho)-1 && (dot = utfrune(rest, ' '))){
			*dot = '\0';
			kouho[i++] = rest;
			rest = dot+1;
		}
		if(i < nelem(kouho)-1)
			kouho[i] = rest;

		/* key is the base pointer; overwrites clean up for us */
		hmaprepl(&h, p, kouho, nil, 1);
	}
	Bterm(b);
	return h;
}

/*
 * write translated kanji runes to stdout and return last character
 * if it's not ctl-\. if the last is ctl-\, proceed with
 * translation of the next kouho
 */
int
dotrans(Hmap *dic)
{
	Rune *res, r[1];
	char v[1024], *p, tbuf[64], hirabuf[64];
	int j, lastlen, nokouho = 0;
	char ch;
	int i;
	char *kouho[16];

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

	if(hmapget(dic, v, kouho) < 0){
		llen = olen = okuri = joshi = 0;
		okurigana[0] = 0;
		return 0;
	}
	for(i = 0; i < nelem(kouho) && kouho[i] != nil; i++) {
		p = kouho[i];
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
			if(i+1 < nelem(kouho) && kouho[i+1] != nil) { /* have next kouho */
				nokouho = 0;
				strcpy(tbuf, p);

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
			if(!nokouho && i != 0){  /* learn the previous use of the kouho */
				p = kouho[0];
				kouho[0] = kouho[i];
				kouho[i] = p;
				hmapupd(&dic, v, kouho);
			}

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
