/*
 *   Mostly based on the original source codes of Plan 9 release 2
 *   distribution.
 *             by Kenji Okamoto, August 4 2000
 *                   Osaka Prefecture Univ.
 *                   okamoto@granite.cias.osakafu-u.ac.jp
 */

#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "hash.h"
#include "ktrans.h"

static Hmap  *jisho, *zidian;
static int   deflang;
static char  backspace[64];

mainstacksize = 8192*2;

char*
pushutf(char *dst, char *e, char *u, int nrune)
{
	Rune r;
	char *p;
	char *d;

	if(dst >= e)
		return dst;

	d = dst;
	p = u;
	while(d < e-1){
		if(isascii(*p)){
			if((*d = *p) == '\0')
				return d;
			p++;
			d++;
		} else {
			p += chartorune(&r, p);
			if(r == Runeerror){
				*d = '\0';
				return d;
			}
			d += runetochar(d, &r);
		}
		if(nrune > 0 && --nrune == 0)
			break;
	}
	if(d > e-1)
		d = e-1;

	*d = '\0';
	return d;
}

typedef struct Str Str;
struct Str {
	char b[128];
	char *p;
};

#define strend(s) ((s)->b + sizeof (s)->b)

void
resetstr(Str *s, ...)
{
	va_list args;
	va_start(args, s);
	do {
		s->p = s->b;
		s->p[0] = '\0';
		s = va_arg(args, Str*);
	} while(s != nil);
	va_end(args);
}

void
popstr(Str *s)
{
	while(s->p > s->b && (*--s->p & 0xC0)==0x80)
		;

	s->p[0] = '\0';
}

Hmap*
openmap(char *file)
{
	Biobuf *b;
	char *s;
	Map map;
	Hmap *h;
	char *key, *val;
	Str partial;
	Rune r;

	h = hmapalloc(64, sizeof(Map));
	b = Bopen(file, OREAD);
	if(b == nil)
		return nil;

	while(key = Brdstr(b, '\n', 1)){
		if(key[0] == '\0'){
		Err:
			free(key);
			continue;
		}

		val = strchr(key, '\t');
		if(val == nil || val[1] == '\0')
			goto Err;

		*val = '\0';
		val++;
		resetstr(&partial, nil);
		for(s = key; *s; s += chartorune(&r, s)){
			partial.p = pushutf(partial.p, strend(&partial), s, 1);
			map.leadstomore = 0;
			if(hmapget(h, partial.b, &map) == 0){
				if(map.leadstomore == 1 && s[1] == '\0')
					map.leadstomore = 1;
			}
			if(s[1] == '\0'){
				map.roma = key;
				map.kana = val;
				hmaprepl(&h, strdup(map.roma), &map, nil, 1);
			} else {
				map.roma = strdup(partial.b);
				map.leadstomore = 1;
				map.kana = nil;
				hmaprepl(&h, strdup(partial.b), &map, nil, 1);
			}
		}
	}
	Bterm(b);
	return h;
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
		dot = strchr(p, '\t');
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

enum{
	LangEN 	= '',	// ^t
	LangJP	= '', 	// ^n
	LangJPK = '',	// ^k
	LangRU 	= '',	// ^r
	LangEL	= '',	// ^o
	LangKO	= '',	// ^s
	LangZH	= '',	// ^c
};

Hmap *natural;
Hmap *hira, *kata;
Hmap *cyril;
Hmap *greek;
Hmap *hangul;
Hmap *hanzi;

Hmap **langtab[] = {
	[LangEN]  &natural,
	[LangJP]  &hira,
	[LangJPK] &kata,
	[LangRU]  &cyril,
	[LangEL]  &greek,
	[LangKO]  &hangul,
	[LangZH]  &hanzi,
};

char *langcodetab[] = {
	[LangEN]  "en",
	[LangJP]  "jp",
	[LangJPK] "jpk",
	[LangRU]  "ru",
	[LangEL]  "el",
	[LangKO]  "ko",
	[LangZH]  "zh",
};

int
parselang(char *s)
{
	int i;

	for(i = 0; i < nelem(langcodetab); i++){
		if(langcodetab[i] == nil)
			continue;
		if(strcmp(langcodetab[i], s) == 0)
			return i;
	}

	return -1; 
}

int
checklang(int *dst, int c)
{
	Hmap **p;

	if(c >= nelem(langtab))
		return 0;

	p = langtab[c];
	if(p == nil)
		return 0;

	*dst = c;
	return c;
}

int
maplkup(int lang, char *s, Map *m)
{
	Hmap **h;

	if(lang >= nelem(langtab))
		return -1;

	h = langtab[lang];
	if(h == nil || *h == nil)
		return -1;

	return hmapget(*h, s, m);
}

static int
emitutf(Channel *out, char *u, int nrune)
{
	Msg m;
	char *e;

	m.code = 'c';
	e = pushutf(m.buf, m.buf + sizeof m.buf, u, nrune);
	send(out, &m);
	return e - m.buf;
}

static void
dictthread(void *a)
{
	Trans *t;
	Msg m;
	Rune r;
	int n;
	char *p;
	Hmap *dict;
	char *kouho[16];
	Str line;
	Str last;
	Str okuri;
	int selected;

	enum{
		Kanji,
		Okuri,
		Joshi,
	};
	int mode;

	t = a;
	dict = jisho;
	selected = -1;
	kouho[0] = nil;
	mode = Kanji;
	resetstr(&last, &line, &okuri, nil);

	threadsetname("dict");
	while(recv(t->dict, &m) != -1){
		for(p = m.buf; *p; p += n){
			n = chartorune(&r, p);
			if(r != ''){
				if(selected >= 0){
					resetstr(&okuri, nil);
					mode = Kanji;
				}
				resetstr(&last, nil);
				selected = -1;
				kouho[0] = nil;
			}
			switch(r){
			case LangJP:
				dict = jisho;
				break;
			case LangZH:
				dict = zidian;
				break;
			case '':
				if(line.b == line.p){
					emitutf(t->output, "", 1);
					break;
				}
				emitutf(t->output, backspace, utflen(line.b));
				/* fallthrough */
			case ' ': case ',': case '.':
			case '':
				mode = Kanji;
				resetstr(&line, &okuri, nil);
				break;
			case '\b':
				if(mode != Kanji){
					if(okuri.p == okuri.b){
						mode = Kanji;
						popstr(&line);
					}else
						popstr(&okuri);
					break;
				}
				popstr(&line);
				break;
			case '\n':
				if(line.b == line.p){
					emitutf(t->output, "\n", 1);
					break;
				}
				/* fallthrough */
			case '':
				selected++;
				if(selected == 0){
					if(hmapget(dict, line.b, kouho) < 0){
						resetstr(&line, &last, nil);
						selected = -1;
						break;
					}
					if(dict == jisho && line.p > line.b && isascii(line.p[-1]))
						line.p[-1] = '\0';
				}
				if(kouho[selected] == nil){
					/* cycled through all matches; bail */
					emitutf(t->output, backspace, utflen(last.b));
					emitutf(t->output, line.b, 0);
					resetstr(&line, &last, &okuri, nil);
					selected = -1;
					break;
				}

				if(okuri.p != okuri.b)
					emitutf(t->output, backspace, utflen(okuri.b));
				if(selected == 0)
					emitutf(t->output, backspace, utflen(line.b));
				else
					emitutf(t->output, backspace, utflen(last.b));

				emitutf(t->output, kouho[selected], 0);
				last.p = pushutf(last.b, strend(&last), kouho[selected], 0);
				emitutf(t->output, okuri.b, 0);

				resetstr(&line, nil);
				mode = Kanji;
				break;
			default:
				if(dict == zidian){
					line.p = pushutf(line.p, strend(&line), p, 1);
					break;
				}

				if(mode == Joshi){
					okuri.p = pushutf(okuri.p, strend(&okuri), p, 1);
					break;
				}
	
				if(isupper(*p)){
					if(mode == Okuri){
						popstr(&line);
						mode = Joshi;
						okuri.p = pushutf(okuri.p, strend(&okuri), p, 1);
						break;
					}
					mode = Okuri;
					*p = tolower(*p);
					line.p = pushutf(line.p, strend(&line), p, 1);
					okuri.p = pushutf(okuri.b, strend(&okuri), p, 1);
					break;	
				}
				if(mode == Kanji)
					line.p = pushutf(line.p, strend(&line), p, 1);
				else
					okuri.p = pushutf(okuri.p, strend(&okuri), p, 1);
				break;
			}
		}
	}

	send(t->done, nil);
}

void
keyproc(void *a)
{
	Trans *t;
	int lang;
	Msg m;
	Map lkup;
	char *p;
	int n;
	Rune r;
	char peek[UTFmax+1];
	Str line;
	int mode;

	t = a;
	mode = 0;
	peek[0] = lang = deflang;
	threadcreate(dictthread, a, mainstacksize);
	resetstr(&line, nil);
	if(lang == LangJP || lang == LangZH)
		emitutf(t->dict, peek, 1);

	threadsetname("key");
	while(recv(t->input, &m) != -1){
		if(m.code != 'c'){
			if(m.code == 'q')
				send(t->lang, &langcodetab[lang]);
			else
				send(t->output, &m);
			continue;
		}

		for(p = m.buf; *p; p += n){
			n = chartorune(&r, p);
			if(checklang(&lang, r)){
				emitutf(t->dict, "", 1);
				if(lang == LangJP || lang == LangZH)
					emitutf(t->dict, p, 1);
				resetstr(&line, nil);
				continue;
			}
			if(lang == LangZH || lang == LangJP){
				emitutf(t->dict, p, 1);
				if(utfrune("\n", r) != nil){
					resetstr(&line, nil);
					continue;
				}
				if(lang == LangJP && isupper(*p)){
					*p = tolower(*p);
					mode++;
				} else {
					mode = 0;
				}
			}

			emitutf(t->output, p, 1);
			if(lang == LangEN || lang == LangZH)
				continue;
			if(r == '\b'){
				popstr(&line);
				continue;
			}

			line.p = pushutf(line.p, strend(&line), p, 1);
			if(maplkup(lang, line.b, &lkup) < 0){
				resetstr(&line, nil);
				pushutf(peek, peek + sizeof peek, p, 1);
				if(maplkup(lang, peek, &lkup) == 0)
					line.p = pushutf(line.p, strend(&line), p, 1);
				continue;
			}
			if(lkup.kana == nil)
				continue;

			if(!lkup.leadstomore)
				resetstr(&line, nil);

			if(lang == LangJP){
				emitutf(t->dict, backspace, utflen(lkup.roma));
				emitutf(t->dict, lkup.kana, 0);
			}
			emitutf(t->output, backspace, utflen(lkup.roma));
			emitutf(t->output, lkup.kana, 0);
		}
	}
	send(t->done, nil);
}

void
usage(void)
{
	fprint(2, "usage: %s [ -K ] [ -l lang ]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{

	char *jishoname, *zidianname;
	char *kbd, *srv, *mntpt;

	kbd = "/dev/kbd";
	srv = nil;
	mntpt = "/mnt/ktrans";
	deflang = LangEN;
	ARGBEGIN{
	case 'K':
		kbd = nil;
		break;
	case 'k':
		kbd = EARGF(usage());
		break;
	case 'l':
		deflang = parselang(EARGF(usage()));
		if(deflang < 0)
			usage();
		break;
	case 's':
		srv = EARGF(usage());
		break;
	case 'm':
		mntpt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 0)
		usage();

	memset(backspace, '\b', sizeof backspace-1);
	backspace[sizeof backspace-1] = '\0';

	if((jishoname = getenv("jisho")) == nil)
		jishoname = "/lib/ktrans/kanji.dict";
	jisho = opendict(nil, jishoname);

	if((zidianname = getenv("zidian")) == nil)
		zidianname = "/lib/ktrans/wubi.dict";
	zidian = opendict(nil, zidianname);

	natural = hanzi = nil;
	hira 	= openmap("/lib/ktrans/hira.map");
	kata 	= openmap("/lib/ktrans/kata.map");
	greek 	= openmap("/lib/ktrans/greek.map");
	cyril 	= openmap("/lib/ktrans/cyril.map");
	hangul 	= openmap("/lib/ktrans/hangul.map");

	launchfs(srv, mntpt, kbd);
}
