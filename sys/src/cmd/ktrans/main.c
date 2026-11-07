#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <plumb.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "hash.h"

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

char*
peekstr(char *s, char *b)
{
	while(s > b && (*--s & 0xC0)==Runesync)
		;
	return s;
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
	s->p = peekstr(s->p, s->b);
	s->p[0] = '\0';
}

typedef	struct Map Map;
struct Map {
	char	*roma;
	char	*kana;
	char	leadstomore;
};

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

enum{
	Maxkouho=32,
};

Hmap*
opendict(Hmap *h, char *name)
{
	Biobuf *b;
	char *p;
	char *dot, *rest;
	char *kouho[Maxkouho];
	int i;

	b = Bopen(name, OREAD);
	if(b == nil)
		return nil;

	if(h == nil)
		h = hmapalloc(8192, sizeof(kouho));
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
	LangKO	= '',	// ^s
	LangZH	= '',	// ^c
	LangVN	= '',	// ^v
};

int deflang;

Hmap *natural;
Hmap *hira, *kata, *jisho;
Hmap *judou, *zidian;
Hmap *telex;

Hmap **langtab[] = {
	[LangEN]  &natural,
	[LangJP]  &hira,
	[LangJPK] &kata,
	[LangKO]  &natural,
	[LangZH]  &judou,
	[LangVN]  &telex,
};

char *langcodetab[] = {
	[LangEN]  "en",
	[LangJP]  "jp",
	[LangJPK] "jpk",
	[LangKO]  "ko",
	[LangZH]  "zh",
	[LangVN]  "vn",
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

enum   { Msgsize = 256 };
static Channel	*dictch;
static Channel	*output;
static Channel	*input;
static char	backspace[Msgsize];

static Channel	*displaych;
static Channel	*selectch;

static void
displaythread(void*)
{
	Mousectl *mctl;
	Mouse m;
	Keyboardctl *kctl;
	Rune key;
	char *kouho[Maxkouho], **s, **e;
	int i, page, total, height, round;
	Image *back, *text, *board, *high, *scroll;
	Font *f;
	Point p;
	Rectangle r, exitr, selr, scrlr;
	int selected;
	char *mp, move[Msgsize];
	enum { Adisp, Aresize, Amouse, Asel, Akbd, Amove, Aend };
	Alt a[] = {
		[Adisp] { nil, kouho, CHANRCV },
		[Aresize] { nil, nil, CHANRCV },
		[Amouse] { nil, &m, CHANRCV },
		[Asel] { nil, &selected, CHANRCV },
		[Akbd] { nil, &key, CHANRCV },
		[Amove] { nil, move, CHANNOP },
		[Aend] { nil, nil, CHANEND },
	};

	if(initdraw(nil, nil, "ktrans") < 0)
		sysfatal("failed to initdraw: %r");

	mctl = initmouse(nil, screen);
	if(mctl == nil)
		sysfatal("failed to get mouse: %r");

	/*
	 * For keys coming in to our specific window.
	 * We've already transliterated these, but should
	 * consume keys and exit on del to avoid artifacts.
	 */
	kctl = initkeyboard(nil);
	if(kctl == nil)
		sysfatal("failed to get keyboard: %r");

	memset(kouho, 0, sizeof kouho);
	selected = -1;
	f = display->defaultfont;
	high = allocimagemix(display, DYellowgreen, DWhite);
	text = display->black;
	back = allocimagemix(display, DPaleyellow, DWhite);
	board = allocimagemix(display, DBlack, DWhite);
	scroll = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DYellowgreen);

	a[Adisp].c = displaych;
	a[Aresize].c = mctl->resizec;
	a[Amouse].c = mctl->c;
	a[Asel].c = selectch;
	a[Akbd].c = kctl->c;
	a[Amove].c = input;

	threadsetname("display");
	goto Redraw;
	for(;;)
		switch(alt(a)){
		case Amove:
			a[Amove].op = CHANNOP;
			break;
		case Akbd:
			if(key != Kdel)
				break;
			closedisplay(display);
			threadexitsall(nil);
		case Amouse:
			if(!m.buttons)
				break;
			if(ptinrect(m.xy, exitr)){
				closedisplay(display);
				threadexitsall(nil);
			}
			if(kouho[0] == nil)
				break;
			if(m.xy.x > scrlr.min.x && m.xy.x < scrlr.max.x){
				if(m.xy.y > scrlr.min.y && m.xy.y < scrlr.max.y)
					break;
				if(m.xy.y < scrlr.min.y)
					goto Up;
				else
					goto Down;
			}

			if(m.buttons & 7){
				m.xy.y -= screen->r.min.y;
				m.xy.y -= f->height;
				if(m.xy.y < 0)
					break;
				i = round + m.xy.y/f->height + 1;
				if(selected != -1)
					i = i - selected - 1;
			} else if(m.buttons == 8){
			Up:
				i = -1 * (selected % height + height);
				if(selected + i < 0)
					i = -(selected + (total % height));
			} else if(m.buttons == 16){
			Down:
				i = height - (selected % height);
				if(selected + i > total)
					i = total - selected;
			} else
				break;

			memset(move, 0, sizeof move);
			move[0] = 'c';
			if(i == 0)
				break;
			else if(i > 0)
				memset(move+1, '', i);
			else for(mp = move+1; i < 0; i++)
				mp = seprint(mp, move + sizeof move, "%C", Kup);
			a[Amove].op = CHANSND;
			break;
		case Aresize:
			getwindow(display, Refnone);
		case Adisp:
		Redraw:
			for(s = kouho, total = 0; *s != nil; s++, total++)
				;

			r = screen->r;
			height = Dy(r)/f->height - 2;
			draw(screen, r, back, nil, ZP);
			r.max.y = r.min.y + f->height;
			draw(screen, r, board, nil, ZP);
			round = selected - (selected % height);

			if(selected >= 0 && kouho[selected] != nil){
				selr = screen->r;
				selr.min.y += f->height*(selected-round+1);
				selr.max.y = selr.min.y + f->height;
				draw(screen, selr, high, nil, ZP);
			}

			scrlr = screen->r;
			scrlr.min.y += f->height;
			scrlr.max.y -= f->height;
			scrlr.max.x = scrlr.min.x + 10;
			draw(screen, scrlr, scroll, nil, ZP);

			if(kouho[0] != nil){
				scrlr.max.x--;
				page = Dy(scrlr) / (total / height + 1);
				scrlr.min.y = scrlr.min.y + page*(round / height);
				scrlr.max.y = scrlr.min.y + page;
				/* fill to the bottom on last page */
				if((screen->r.max.y - f->height) - scrlr.max.y < page)
					scrlr.max.y = screen->r.max.y - f->height;
				draw(screen, scrlr, back, nil, ZP);
			}

			r.min.x += Dx(r)/2;
			p.y = r.min.y;

			p.x = r.min.x - stringwidth(f, "候補")/2;
			string(screen, p, text, ZP, f, "候補");
			p.y += f->height;

			for(s = kouho+round, e = kouho+round+height; *s != nil && s < e; s++){
				p.x = r.min.x - stringwidth(f, *s)/2;
				string(screen, p, text, ZP, f, *s);
				p.y += f->height;
			}

			p.x = r.min.x - stringwidth(f, "終了")/2;
			p.y = screen->r.max.y - f->height;
			exitr = Rpt(Pt(0, p.y), screen->r.max);
			draw(screen, exitr, board, nil, ZP);
			string(screen, p, text, ZP, f, "終了");
			flushimage(display, 1);
			break;
		}
}

static int
emitutf(Channel *out, char *u, int nrune)
{
	char b[Msgsize];
	char *e;

	b[0] = 'c';
	e = pushutf(b+1, b + Msgsize - 1, u, nrune);
	send(out, b);
	return e - b;
}

static int compacting = 0;

static void
dictthread(void*)
{
	char m[Msgsize];
	Rune r;
	int n;
	char *p;
	Hmap *dict;
	char *kouho[Maxkouho];
	Str line;
	Str last;
	Str okuri;
	int selected, loop;

	enum{
		Kanji,
		Okuri,
		Joshi,
	};
	int mode;

	dict = jisho;
	selected = -1;
	loop = 0;
	mode = Kanji;
	memset(kouho, 0, sizeof kouho);
	resetstr(&last, &line, &okuri, nil);

	threadsetname("dict");
	while(recv(dictch, m) != -1){
		compacting = 1;
		for(p = m+1; *p; p += n){
			n = chartorune(&r, p);
			switch(r){
			case LangJP:
				dict = jisho;
				break;
			case LangZH:
				dict = zidian;
				break;
			case '':
				if(line.b == line.p){
					emitutf(output, "", 1);
					break;
				}
				emitutf(output, backspace, utflen(line.b));
				/* fallthrough */
			case '': case ' ': case '\n':
				mode = Kanji;
				resetstr(&line, &okuri, &last, nil);
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
			case Kup:
				if(selected == -1){
					emitutf(output, p, 1);
					break;
				}
				if(--selected < 0){
					//wrap
					while(kouho[++selected] != nil)
						;
					selected--;
				}
				loop = 1;
				goto Select;
			case Kdown:
				if(selected == -1){
					emitutf(output, p, 1);
					break;
				}
			case '':
				selected++;
				if(selected == 0){
					if(hmapget(dict, line.b, kouho) < 0)
						break;
					if(dict == jisho && line.p > line.b && isascii(line.p[-1]))
						line.p[-1] = '\0';
				}
				if(kouho[selected] == nil){
					selected = 0;
					loop = 1;
				}
			Select:
				send(selectch, &selected);
				send(displaych, kouho);

				if(okuri.p != okuri.b)
					emitutf(output, backspace, utflen(okuri.b));
				if(selected == 0 && !loop)
					emitutf(output, backspace, utflen(line.b));
				else
					emitutf(output, backspace, utflen(last.b));

				emitutf(output, kouho[selected], 0);
				last.p = pushutf(last.b, strend(&last), kouho[selected], 0);
				emitutf(output, okuri.b, 0);
				mode = Kanji;
				loop = 0;
				continue;
			case ',': case '.':
			case L'。': case L'、':
				if(dict == zidian || line.p == line.b){
					selected = 0; //hit cleanup below
					break;
				}
				mode = Joshi;
				okuri.p = pushutf(okuri.p, strend(&okuri), p, 1);
				break;
			default:
				if(dict == zidian)
					goto Line;
				if(mode == Joshi)
					goto Okuri;

				if(isupper(*p)){
					if(mode == Okuri){
						popstr(&line);
						mode = Joshi;
						goto Okuri;
					}
					mode = Okuri;
					*p = tolower(*p);
					okuri.p = pushutf(okuri.b, strend(&okuri), p, 1);
					goto Line;
				}

				switch(mode){
				case Kanji:
				Line:
					line.p = pushutf(line.p, strend(&line), p, 1);
					break;
				default:
				Okuri:
					okuri.p = pushutf(okuri.p, strend(&okuri), p, 1);
					break;
				}
			}

			if(selected >= 0){
				resetstr(&okuri, &last, &line, nil);
				if(dict == zidian)
					line.p = pushutf(line.p, strend(&line), p, 1);
				selected = -1;
				send(selectch, &selected);
			}
			memset(kouho, 0, sizeof kouho);
			hmapget(dict, line.b, kouho);
			send(displaych, kouho);
		}
		compacting = 0;
	}
}

static void
telexlkup(Str *line)
{
	Map lkup;
	char buf[UTFmax*3], *p, *e, *dot;
	Str out;
	int n, ln;

	for(dot = line->b; (ln = utflen(dot)) >= 2; dot += n){
		p = pushutf(buf, buf+sizeof buf, dot, 1);
		n = p-buf;
	
		if(hmapget(telex, buf, &lkup) < 0)
			continue;
	
		e = peekstr(line->p, line->b);
		pushutf(p, buf+sizeof buf, e, 1);
		if(hmapget(telex, buf, &lkup) < 0)
			continue;
	
		out.p = pushutf(out.b, strend(&out), lkup.kana, 0);
		out.p = pushutf(out.p, strend(&out), dot+n, 0);
		popstr(&out);

		emitutf(output, backspace, ln);
		emitutf(output, out.b, 0);
		line->p = pushutf(line->b, strend(line), out.b, 0);
		return;
	}
}

static char chotab[] = "rRseEfaqQtTdwWczxvg";

static char *jungtab[] = {
	"k", "o", "i", "O", "j", "p", "u", "P", "h", "hk", "ho",
	"hl", "y", "n", "nj", "np", "nl", "b", "m", "ml", "l",
};

static char *jongtab[] = {
	"", "r", "R", "rt", "s", "sw", "sg", "e", "f", "fr", "fa", "fq",
	"ft", "fx", "fv", "fg", "a", "q", "qt", "t", "T", "d", "w", "c",
	"z", "x", "v", "g",
};

static char *jamotab[] = {
	"r", "R", "", "s", "", "", "e", "E", "f", "",
	"", "", "", "", "", "", "a", "q", "Q", "",
	"t", "T", "d", "w", "W", "c", "z", "x", "v", "g",
};

static Rune
hangullkup(char *s)
{
	char *p;
	int i, j, l, cho;

	if(s[0] == '\0')
		return 0;
	for(i = 0; i < nelem(jamotab); i++)
		if(strcmp(s, jamotab[i]) == 0)
			return 0x3131 + i;
	for(i = 0; i < nelem(jungtab); i++)
		if(strcmp(s, jungtab[i]) == 0)
			return 0x314f + i;

	if((p = strchr(chotab, s[0])) == nil)
		return 0;
	cho = p - chotab;
	s++;
	for(i = 0; i < nelem(jungtab); i++){
		l = strlen(jungtab[i]);
		if(strncmp(s, jungtab[i], l) != 0)
			continue;
		for(j = 0; j < nelem(jongtab); j++)
			if(strcmp(s + l, jongtab[j]) == 0)
				return 0xac00 + (cho*21 + i)*28 + j;
	}
	return 0;
}

static void
emitrune(Rune r)
{
	char s[UTFmax+1];

	s[runetochar(s, &r)] = '\0';
	emitutf(output, s, 1);
}

static void
dubeolbksp(Str *line)
{
	Rune r;

	popstr(line);
	/* lookup and emit remaining Jamo to output */
	if((r = hangullkup(line->b)) == 0)
		return;
	emitrune(r);
}

static void
dubeollkup(Str *line)
{
	Rune r, r2;
	char buf[UTFmax*2];
	char *e2, *e1;

	e1 = peekstr(line->p, line->b);
	if((r = hangullkup(line->b)) == 0){
		if(e1 != line->b){
			e2 = peekstr(e1, line->b);
			pushutf(buf, buf+sizeof buf, e2, utflen(e2));
		}else
			pushutf(buf, buf+sizeof buf, e1, utflen(e1));

		if((r2 = hangullkup(buf)) == 0){
			pushutf(buf, buf+sizeof buf, e1, 1);
			resetstr(line, nil);
			if((r = hangullkup(buf)) == 0)
				return;
			line->p = pushutf(line->p, strend(line), buf, 1);
		}else{
			/* treat Jongseong as Choseong when it matches with new Jamo */
			popstr(line);
			popstr(line);
			r = hangullkup(line->b);
			emitutf(output, backspace, 2);
			emitrune(r);
			emitrune(r2);
			line->p = pushutf(line->b, strend(line), buf, utflen(buf));
			return;
		}
	}
	if(utflen(line->b) == 1)
		emitutf(output, backspace, 1);
	else
		emitutf(output, backspace, 2);
	emitrune(r);
}

static void
keythread(void*)
{
	int lang, litmode;
	char m[Msgsize];
	char *todict;
	Map lkup;
	char *p;
	int n;
	Rune r;
	char peek[UTFmax+1];
	Str line;

	peek[0] = lang = deflang;
	litmode = 0;
	resetstr(&line, nil);
	if(lang == LangJP || lang == LangZH)
		emitutf(dictch, peek, 1);
	todict = smprint("%C%C", Kup, Kdown);

	threadsetname("keytrans");
	while(recv(input, m) != -1){
		if(m[0] == 'z'){
			emitutf(dictch, "", 1);
			resetstr(&line, nil);
			continue;
		}
		if(m[0] != 'c'){
			send(output, m);
			continue;
		}

		for(p = m+1; *p; p += n){
			while(compacting)
				yield();
			n = chartorune(&r, p);
			if(litmode){
				emitutf(output, p, 1);
				litmode = 0;
				continue;
			}
			if(checklang(&lang, r)){
				emitutf(dictch, "", 1);
				if(lang == LangJP || lang == LangZH)
					emitutf(dictch, p, 1);
				resetstr(&line, nil);
				continue;
			}
			if(r == ''){ // ^'
				litmode = 1;
				continue;
			}
			if(lang == LangEN){
				emitutf(output, p, 1);
				continue;
			}
			if(utfrune(todict, r) != nil){
				resetstr(&line, nil);
				emitutf(dictch, p, 1);
				continue;
			}
			emitutf(output, p, 1);

			switch(lang){
			case LangZH:
				emitutf(dictch, p, 1);
				break;
			case LangJP:
				emitutf(dictch, p, 1);
				if(isupper(*p))
					*p = tolower(*p);
				break;
			case LangKO:
				if(isupper(*p) && strchr("QWERTOP", *p) == nil)
					*p = tolower(*p);
				break;
			}
			if(utfrune("\n\t ", r) != nil){
				resetstr(&line, nil);
				continue;
			} else if(r == '\b'){
				if(lang == LangKO){
					dubeolbksp(&line);
					continue;
				}
				popstr(&line);
				continue;
			}

			line.p = pushutf(line.p, strend(&line), p, 1);
			if(lang == LangVN){
				telexlkup(&line);
				continue;
			}else if(lang == LangKO){
				dubeollkup(&line);
				continue;
			}
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
				emitutf(dictch, backspace, utflen(lkup.roma));
				emitutf(dictch, lkup.kana, 0);
			}
			emitutf(output, backspace, utflen(lkup.roma));
			emitutf(output, lkup.kana, 0);
		}
	}
}

static int kbdin;
static int kbdout;

static void
kbdtap(void*)
{
	char m[Msgsize];
	char buf[Msgsize];
	char *p;
	int n;

	threadsetname("kbdtap");
	for(;;){
	Drop:
		n = read(kbdin, buf, sizeof buf);
		if(n < 0)
			break;
		for(p = buf; p < buf+n; p += strlen(p) + 1){
			switch(*p){
			case 'c': case 'k': case 'K':
			case 'z':
				break;
			default:
				goto Drop;
			}
			strcpy(m, p);
			if(send(input, m) == -1)
				return;
		}
	}
	threadexitsall(nil);
}

static void
kbdsink(void*)
{
	char in[Msgsize];
	char out[Msgsize];
	char *p;
	int n;
	Rune rn;

	out[0] = 'c';
	threadsetname("kbdsink");
	while(recv(output, in) != -1){
		if(in[0] != 'c'){
			if(write(kbdout, in, strlen(in)+1) < 0)
				break;
			continue;
		}

		for(p = in+1; *p; p += n){
			n = chartorune(&rn, p);
			if(rn == Runeerror || rn == '\0')
				break;
			memmove(out+1, p, n);
			out[1+n] = '\0';
			if(write(kbdout, out, 1+n+1) < 0)
				break;
		}
	}
}

static int plumbfd;

static void
plumbproc(void*)
{
	char m[Msgsize];
	Plumbmsg *p;

	threadsetname("plumbproc");
	for(; p = plumbrecv(plumbfd); plumbfree(p)){
		if(p->ndata > sizeof m - 1)
			continue;
		memmove(m, p->data, p->ndata);
		m[p->ndata] = '\0';

		m[1] = parselang(m);
		if(m[1] == -1)
			continue;
		m[0] = 'c';
		m[2] = '\0';

		if(send(input, m) == -1)
			break;
	}
	plumbfree(p);
}

void
usage(void)
{
	fprint(2, "usage: %s [ -G ] [ -l lang ] [ kbdtap ]\n", argv0);
	threadexits("usage");
}


static char *kdir = "/lib/ktrans";

struct {
	char *s;
	Hmap **m;
} initmaptab[] = {
	"judou", &judou,
	"hira", &hira,
	"kata", &kata,
	"telex", &telex,
};
struct {
	char *env;
	char *def;
	Hmap **m;
} initdicttab[] = {
	"jisho", "kanji.dict", &jisho,
	"zidian", "wubi.dict", &zidian,
};

mainstacksize = 8192*2;

void
threadmain(int argc, char *argv[])
{
	int nogui, i;
	char buf[128];
	char *e, *home;
	Hmap *m;

	deflang = LangEN;
	nogui = 0;
	ARGBEGIN{
	case 'l':
		deflang = parselang(EARGF(usage()));
		if(deflang < 0)
			usage();
		break;
	case 'G':
		nogui++;
		break;
	default:
		usage();
	}ARGEND;
	switch(argc){
	case 0:
		kbdin = 0;
		kbdout = 1;
		break;
	case 1:
		kbdin = kbdout = open(argv[0], ORDWR);
		if(kbdin < 0)
			sysfatal("failed to open kbdtap: %r");
		break;
	default:
		usage();
	}

	dictch 	= chancreate(Msgsize, 0);
	input 	= chancreate(Msgsize, 0);
	output 	= chancreate(Msgsize, 0);

	/* allow gui to warm up while we're busy reading maps */
	if(nogui || access("/dev/winid", AEXIST) < 0){
		displaych = nil;
		selectch = nil;
	} else {
		selectch = chancreate(sizeof(int), 0);
		displaych = chancreate(sizeof(char*)*Maxkouho, 0);
		proccreate(displaythread, nil, mainstacksize);
	}

	memset(backspace, '\b', sizeof backspace-1);
	backspace[sizeof backspace-1] = '\0';

	if((home = getenv("home")) == nil)
		sysfatal("$home undefined");
	for(i = 0; i < nelem(initdicttab); i++){
		e = getenv(initdicttab[i].env);
		if(e != nil){
			snprint(buf, sizeof buf, "%s", e);
			free(e);
		} else
			snprint(buf, sizeof buf, "%s/%s", kdir, initdicttab[i].def);
		if((*initdicttab[i].m = opendict(*initdicttab[i].m, buf)) == nil)
			sysfatal("failed to open dict: %r");
		snprint(buf, sizeof buf, "%s/%s/%s", home, kdir, initdicttab[i].def);
		m = opendict(*initdicttab[i].m, buf);
		if(m != nil)
			*initdicttab[i].m = m;
	}
	free(home);

	natural = nil;
	for(i = 0; i < nelem(initmaptab); i++){
		snprint(buf, sizeof buf, "%s/%s.map", kdir, initmaptab[i].s);
		if((*initmaptab[i].m = openmap(buf)) == nil)
			sysfatal("failed to open map: %r");
	}

	plumbfd = plumbopen("lang", OREAD);
	if(plumbfd >= 0)
		proccreate(plumbproc, nil, mainstacksize);

	proccreate(kbdtap, nil, mainstacksize);
	proccreate(kbdsink, nil, mainstacksize);
	threadcreate(dictthread, nil, mainstacksize);
	threadcreate(keythread, nil, mainstacksize);

	threadexits(nil);
}
