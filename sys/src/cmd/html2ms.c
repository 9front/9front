#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>

typedef struct Tag Tag;
typedef struct Attr Attr;
typedef struct Text Text;

struct Attr {
	char	attr[64];
	char	val[256-64];
};

struct Tag {
	Tag	*up;
	char	tag[32];
	Attr	attr[16];
	int	nattr;
	int	opening;
	int	closing;

	void	(*close)(Text *, Tag *);
	union {
		void	*aux;
	};
};

struct Text {
	char*	fontstyle;
	char*	fontsize;
	int	pre;
	int	pos;
	int	space;
	int	output;

	char	*bp;
	char	*wp;
	int	nb;
};

void eatwhite(void);
void parsetext(Text *, Tag *);
int parsetag(Tag *);
int parseattr(Attr *);
void flushtext(Text *);
char* getattr(Tag *, char *);
int gotattr(Tag *, char *, char *);
int gotstyle(Tag *, char *, char *);
void reparent(Text *, Tag *, Tag *);
void debugtag(Tag *, char *);

Biobuf in;

void
emitbuf(Text *text, char *buf, int nbuf)
{
	int nw;

	nw = text->wp - text->bp;
	if((text->nb - nw) < nbuf){
		if(nbuf < 4096)
			text->nb = nw + 4096;
		else
			text->nb = nw + nbuf;
		text->bp = realloc(text->bp, text->nb);
		text->wp = text->bp + nw;
	}
	memmove(text->wp, buf, nbuf);
	text->wp += nbuf;
}

void
emitrune(Text *text, Rune r)
{
	char buf[UTFmax+1];

	if(r == '\r' || r =='\n'){
		text->pos = 0;
		text->space = 0;
	}else
		text->pos++;
	emitbuf(text, buf, runetochar(buf, &r));
}

void
emit(Text *text, char *fmt, ...)
{
	Rune buf[64];
	va_list a;
	int i;

	if(fmt[0] == '.' && text->pos)
		emitrune(text, '\n');
	va_start(a, fmt);
	runevsnprint(buf, nelem(buf), fmt, a);
	va_end(a);
	for(i=0; buf[i]; i++)
		emitrune(text, buf[i]);
}

void
restoreoutput(Text *text, Tag *)
{
	text->output = 1;
}

void
ongarbage(Text *text, Tag *tag)
{
	if(text->output == 0)
		return;
	tag->close = restoreoutput;
	text->output = 0;
}

void
onmeta(Text *, Tag *tag)
{
	tag->closing = 1;
}

void
onp(Text *text, Tag *)
{
	emit(text, ".LP\n");
}

void
restorepre(Text *text, Tag *)
{
	text->pre = 0;
	emit(text, ".DE\n");
}

void
onpre(Text *text, Tag *tag)
{
	if(text->pre)
		return;
	tag->close = restorepre;
	text->pre = 1;
	emit(text, ".DS L\n");
}

void
onli(Text *text, Tag *tag)
{
	if(tag->up && cistrcmp(tag->up->tag, "ol") == 0)
		emit(text, ".IP\n");
	else
		emit(text, ".IP \\(bu\n");
	if(tag->up)
		tag->up->close = onp;
}

void
onh(Text *text, Tag *tag)
{
	emit(text, ".SH\n");
	tag->close = onp;
}

void
onbr(Text *text, Tag *tag)
{
	tag->closing = 1;
	emit(text, ".br\n");
	if(cistrcmp(tag->tag, "hr") == 0)
		emit(text, "\\l'5i'\n.br\n");
}

void
fontstyle(Text *text, char *style)
{
	if(strcmp(text->fontstyle, style) == 0)
		return;
	text->fontstyle = style;
	emit(text, "\\f%s", style);
}

void
fontsize(Text *text, char *size)
{
	if(strcmp(text->fontsize, size) == 0)
		return;
	text->fontsize = size;
	emit(text, ".%s\n", size);
}

void
restorefontstyle(Text *text, Tag *tag)
{
	fontstyle(text, tag->aux);
}

void
restorefontsize(Text *text, Tag *tag)
{
	fontsize(text, tag->aux);
}

void
oni(Text *text, Tag *tag)
{
	tag->aux = text->fontstyle;
	tag->close = restorefontstyle;
	fontstyle(text, "I");
}

void
onb(Text *text, Tag *tag)
{
	tag->aux = text->fontstyle;
	tag->close = restorefontstyle;
	fontstyle(text, "B");
}

void onsmall(Text *text, Tag *tag);
void onsup(Text *text, Tag *tag);

void
onsub(Text *text, Tag *tag)
{
	emit(text, "\\v\'0.5\'");
	if(cistrcmp(tag->tag, "sub") == 0){
		emit(text, "\\x\'0.5\'");
		onsmall(text, tag);
	} else
		restorefontsize(text, tag);
	tag->close = onsup;
}

void
onsup(Text *text, Tag *tag)
{
	emit(text, "\\v\'-0.5\'");
	if(cistrcmp(tag->tag, "sup") == 0){
		emit(text, "\\x\'-0.5\'");
		onsmall(text, tag);
	}else
		restorefontsize(text, tag);
	tag->close = onsub;
}

/*
 * this is poor mans CSS handler.
 */
void
onspan(Text *text, Tag *tag)
{
	Attr *a;

	if(!tag->opening)
		return;

	for(a=tag->attr; a < tag->attr+tag->nattr; a++){
		if(cistrcmp(a->attr, "class") != 0)
			continue;

		if(cistrcmp(a->val, "bold") == 0){
			onb(text, tag);
			return;
		}
		if(cistrcmp(a->val, "italic") == 0){
			oni(text, tag);
			return;
		}
		if(cistrcmp(a->val, "subscript") == 0){
			strcpy(tag->tag, "sub");
			onsub(text, tag);
			strcpy(tag->tag, "span");
			return;
		}
		if(cistrcmp(a->val, "superscript") == 0){
			strcpy(tag->tag, "sup");
			onsup(text, tag);
			strcpy(tag->tag, "span");
			return;
		}
	}
}

void
ontt(Text *text, Tag *tag)
{
	tag->aux = text->fontstyle;
	tag->close = restorefontstyle;
	fontstyle(text, "C");
}

void
onsmall(Text *text, Tag *tag)
{
	tag->aux = text->fontsize;
	tag->close = restorefontsize;
	fontsize(text, "SM");
}

void
onbig(Text *text, Tag *tag)
{
	tag->aux = text->fontsize;
	tag->close = restorefontsize;
	fontsize(text, "LG");
}

void
endquote(Text *text, Tag *tag)
{
	if(cistrcmp(tag->tag, "q") == 0)
		emitrune(text, '"');
	emit(text, ".QE\n");
}

void
onquote(Text *text, Tag *tag)
{
	tag->close = endquote;
	if(cistrcmp(tag->tag, "q") == 0)
		emit(text, ".QS\n\"");
	else
		emit(text, ".QP\n");
}

typedef struct Table Table;
struct Table
{
	char	*bp;
	int	nb;

	Table	*next;
	Table	*prev;
	int	enclose;
	int	brk;

	char	fmt[4];

	Text	save;
};

Tag*
tabletag(Tag *tag)
{
	if(tag == nil)
		return nil;
	if(cistrcmp(tag->tag, "table") == 0)
		return tag;
	return tabletag(tag->up);
}

void
dumprows(Text *text, Table *s, Table *e)
{
	
	for(; s != e; s = s->next){
		if(s->enclose)
			emit(text, "T{\n");
		if(s->nb <= 0)
			emit(text, "\\ ");
		else
			emitbuf(text, s->bp, s->nb);
		if(s->enclose)
			emit(text, "\nT}");
		emitrune(text, s->brk ? '\n' : '\t');
	}
}

void
endtable(Text *text, Tag *tag)
{
	int i, cols, rows;
	Table *t, *h, *s;
	Tag *tt;

	/* reverse list */
	h = nil;
	t = tag->aux;
 	for(; t; t = t->prev){
		t->next = h;
		h = t;
	}

	/*
	 * nested table case, add our cells to the next table up.
	 * this is the best we can do, tbl doesnt support nesting
	 */
	if(tt = tabletag(tag->up)){
		while(t = h){
			h = h->next;
			t->next = nil;
			t->prev = tt->aux;
			tt->aux = t;
		}
		return;
	}

	cols = 0;
	rows = 0;
	for(i = 0, t = h; t; t = t->next){
		i++;
		if(t->brk){
			rows++;
			if(i > cols)
				cols = i;
			i = 0;
		}
	}

	i = 0;
 	for(t = h; t; t = t->next){
		i++;
		if(t->brk){
			while(i < cols){
				s = mallocz(sizeof(Table), 1);
				strcpy(s->fmt, "L");
				s->brk = t->brk;
				t->brk = 0;
				s->next = t->next;
				t->next = s;
				i++;
			}
			break;
		}
	}

	s = h;
	while(s){
		emit(text, ".TS\n");
		if(gotattr(tag, "align", "center"))
			emit(text, "center ;\n");
		i = 0;
		for(t = s; t; t = t->next){
			emit(text, "%s", t->fmt);
			if(t->brk){
				emitrune(text, '\n');
				if(++i > 30){
					t = t->next;
					break;
				}
			}else
				emitrune(text, ' ');
		}
		emit(text, ".\n");
		dumprows(text, s, t);
		emit(text, ".TE\n");
		s = t;
	}

	while(t = h){
		h = t->next;
		free(t->bp);
		free(t);
	}
}

void
ontable(Text *, Tag *tag)
{
	tag->aux = nil;
	tag->close = endtable;
}

void
endcell(Text *text, Tag *tag)
{
	Table *t;
	Tag *tt;
	int i;

	if((tt = tabletag(tag)) == nil)
		return;
	if(cistrcmp(tag->tag, "tr") == 0){
		if(t = tt->aux)
			t->brk = 1;
	} else {
		t = tag->aux;
		t->bp = text->bp;
		t->nb = text->wp - text->bp;

		for(i=0; i<t->nb; i++)
			if(strchr(" \t\r\n", t->bp[i]) == nil)
				break;
		if(i > 0){
			memmove(t->bp, t->bp+i, t->nb - i);
			t->nb -= i;
		}
		while(t->nb > 0 && strchr(" \t\r\n", t->bp[t->nb-1]))
			t->nb--;
		if(t->nb < 32){
			for(i=0; i<t->nb; i++)
				if(strchr("\t\r\n", t->bp[i]))
					break;
			t->enclose = i < t->nb;
		} else {
			t->enclose = 1;
		}
		if(gotstyle(tag, "text-align", "center") || gotstyle(tt, "text-align", "center"))
			strcpy(t->fmt, "C");
		else
			strcpy(t->fmt, "L");
		if(strcmp(tag->tag, "th") == 0)
			strcpy(t->fmt+1, "B");
		t->prev = tt->aux;
		tt->aux = t;
		*text = t->save;
	}
}

void
oncell(Text *text, Tag *tag)
{
	Tag *tt;

	if((tt = tabletag(tag)) == nil)
		return;
	if(cistrcmp(tag->tag, "tr")){
		Table *t;

		tt = tag->up;
		while(tt && cistrcmp(tt->tag, "tr"))
			tt = tt->up;
		if(tt == nil)
			return;
		reparent(text, tag, tt);

		t = mallocz(sizeof(*t), 1);
		t->save = *text;
		tag->aux = t;

		text->bp = nil;
		text->wp = nil;
		text->nb = 0;
		text->pos = 0;
		text->space = 0;
	} else
		reparent(text, tag, tt);
	tag->close = endcell;
}

struct {
	char	*tag;
	void	(*open)(Text *, Tag *);
} ontag[] = {
	"b",		onb,
	"big",		onbig,
	"blockquote",	onquote,
	"br",		onbr,
	"cite",		oni,
	"code",		ontt,
	"dfn",		oni,
	"em",		oni,
	"h1",		onh,
	"h2",		onh,
	"h3",		onh,
	"h4",		onh,
	"h5",		onh,
	"h6",		onh,
	"head",		ongarbage,
	"hr",		onbr,
	"i",		oni,
	"img",		onmeta,
	"kbd",		ontt,
	"li",		onli,
	"link",		onmeta,
	"meta",		onmeta,
	"p",		onp,
	"pre",		onpre,
	"q",		onquote,
	"samp",		ontt,
	"script",	ongarbage,
	"small",	onsmall,
	"strong",	onb,
	"style",	ongarbage,
	"table",	ontable,
	"td",		oncell,
	"th",		oncell,
	"tr",		oncell,
	"sub",		onsub,
	"sup",		onsup,
	"span",		onspan,
	"tt",		ontt,
	"var",		oni,
};

void
eatwhite(void)
{
	int c;

	while((c = Bgetc(&in)) > 0){
		if(strchr("\n\r\t ", c) == nil){
			Bungetc(&in);
			return;
		}
	}
}

void
parsecomment(void)
{
	char buf[64];
	int n, c;

	n = 0;
	eatwhite();
	while((c = Bgetc(&in)) > 0){
		if(c == '>')
			return;
		if(n == 0 && c == '-'){
			while((c = Bgetc(&in)) > 0){
				if(c == '-')
					if(Bgetc(&in) == '-')
						if(Bgetc(&in) == '>')
							return;
			}
		}
		if(n+1 < sizeof(buf)){
			buf[n++] = c;
			if(n != 7 || cistrncmp(buf, "[CDATA[", 7))
				continue;
			while((c = Bgetc(&in)) > 0){
				if(c == ']'){
					if(Bgetc(&in) == ']'){
						if(Bgetc(&in) != '>')
							Bungetc(&in);
						return;
					}
				}
			}
		}
	}
}

int
parseattr(Attr *a)
{
	int q, c, n;

	n = 0;
	eatwhite();
	while((c = Bgetc(&in)) > 0){
		if(strchr("</>=?!", c)){
			Bungetc(&in);
			break;
		}
		if(strchr("\n\r\t ", c))
			break;
		if(n < sizeof(a->attr)-1)
			a->attr[n++] = c;
	}
	if(n == 0)
		return 0;
	a->attr[n] = 0;
	n = 0;
	eatwhite();
	if(Bgetc(&in) == '='){
		eatwhite();
		c = Bgetc(&in);
		if(strchr("'\"", c)){
			q = c;
			while((c = Bgetc(&in)) > 0){
				if(c == q)
					break;
				if(n < sizeof(a->val)-1)
					a->val[n++] = c;
			}
		} else {
			Bungetc(&in);
			while((c = Bgetc(&in)) > 0){
				if(strchr("\n\r\t </>?!", c)){
					Bungetc(&in);
					break;
				}
				if(n < sizeof(a->val)-1)
					a->val[n++] = c;
			}
		}
	} else
		Bungetc(&in);
	a->val[n] = 0;
	return 1;
}

int
parsetag(Tag *t)
{
	int n, c;

	t->nattr = 0;
	t->opening = 1;
	t->closing = 0;

	n = 0;
	eatwhite();
	while((c = Bgetc(&in)) > 0){
		if(c == '>')
			break;
		if(strchr("\n\r\t ", c)){
			if(parseattr(t->attr + t->nattr))
				if(t->nattr < nelem(t->attr)-1)
					t->nattr++;
			continue;
		}
		if(n == 0 && strchr("?!", c)){
			parsecomment();
			return 0;
		}
		if(c == '/'){
			if(n == 0){
				t->opening = 0;
				t->closing = 1;
			} else
				t->closing = 1;
			continue;
		}
		if(n < sizeof(t->tag)-1)
			t->tag[n++] = c;
	}
	t->tag[n] = 0;
	return n > 0;
}

Rune
parserune(int c)
{
	char buf[10];
	int n;
	Rune r;

	n = 0;
	if(c == '&'){
		while((c = Bgetc(&in)) > 0){
			if(strchr(";&</>\n\r\t ", c)){
				if(c != ';')
					Bungetc(&in);
				if(n == 0)
					return '&';
				break;
			}
			if(n == sizeof(buf)-1)
				break;
			buf[n++] = c;
		}
		buf[n] = 0;
		if(strcmp(buf, "lt") == 0)
			return '<';
		if(strcmp(buf, "gt") == 0)
			return '>';
		if(strcmp(buf, "quot") == 0)
			return '"';
		if(strcmp(buf, "apos") == 0)
			return '\'';
		if(strcmp(buf, "amp") == 0)
			return '&';
		/* use tcs -f html to handle the rest. */
	} else {
		do {
			buf[n++] = c;
			if(fullrune(buf, n)){
				chartorune(&r, buf);
				return r;
			}
			if(n >= UTFmax)
				break;
		} while((c = Bgetc(&in)) > 0);
	}
	return Runeerror;
}

Rune
substrune(Rune r)
{
	switch(r){
	case 0x2019:
	case 0x2018:
		return '\'';
	case 0x201c:
	case 0x201d:
		return '"';
	default:
		return r;
	}
}

void
debugtag(Tag *tag, char *dbg)
{
	if(1){
		USED(tag);
		USED(dbg);
		return;
	}

	if(tag == nil)
		return;
	debugtag(tag->up, nil);
	fprint(2, "%s %s%s", tag->tag, dbg ? dbg : " > ", dbg ? "\n" : "");
}

char*
getattr(Tag *tag, char *attr)
{
	int i;

	for(i=0; i<tag->nattr; i++)
		if(cistrcmp(tag->attr[i].attr, attr) == 0)
			return tag->attr[i].val;
	return nil;
}

int
gotattr(Tag *tag, char *attr, char *val)
{
	char *v;

	if((v = getattr(tag, attr)) == nil)
		return 0;
	return cistrstr(v, val) != 0;
}

int
gotstyle(Tag *tag, char *style, char *val)
{
	char *v;

	if((v = getattr(tag, "style")) == nil)
		return 0;
	if((v = cistrstr(v, style)) == nil)
		return 0;
	v += strlen(style);
	while(*v && *v != ':')
		v++;
	if(*v != ':')
		return 0;
	v++;
	while(*v && strchr("\t ", *v))
		v++;
	if(cistrncmp(v, val, strlen(val)))
		return 0;
	return 1;
}

void
reparent(Text *text, Tag *tag, Tag *up)
{
	Tag *old;

	old = tag->up;
	while(old != up){
		debugtag(old, "reparent");
		if(old->close){
			old->close(text, old);
			old->close = nil;
		}
		old = old->up;
	}
	tag->up = up;
}


void
parsetext(Text *text, Tag *tag)
{
	int hidden, c;
	Tag t, *up;
	Rune r;

	if(tag){
		up = tag->up;
		debugtag(tag, "open");
		for(c = 0; c < nelem(ontag); c++){
			if(cistrcmp(tag->tag, ontag[c].tag) == 0){
				ontag[c].open(text, tag);
				break;
			}
		}
		hidden = getattr(tag, "hidden") || gotstyle(tag, "display", "none");
	} else {
		up = nil;
		hidden = 0;
	}
	if(tag == nil || tag->closing == 0){
		while((c = Bgetc(&in)) > 0){
			if(c == '<'){
				memset(&t, 0, sizeof(t));
				if(parsetag(&t)){
					if(t.opening){
						t.up = tag;
						parsetext(text, &t);
						if(t.up != tag){
							debugtag(tag, "skip");
							up = t.up;
							break;
						}
						debugtag(tag, "back");
					} else if(t.closing){
						up = tag;
						while(up && cistrcmp(up->tag, t.tag))
							up = up->up;
						if(up){
							up = up->up;
							break;
						}
					}
				}
				continue;
			}
			if(hidden || !text->output)
				continue;
			r = substrune(parserune(c));
			switch(r){
			case '\n':
			case '\r':
			case ' ':
			case '\t':
				if(text->pre == 0){
					text->space = 1;
					break;
				}
			default:
				if(text->space){
					if(text->pos >= 70)
						emitrune(text, '\n');
					else if(text->pos > 0)
						emitrune(text, ' ');
				}
				if((text->pos == 0 && r == '.') || r == '\\')
					emit(text, "\\&");
				if(r == '\\' || r == 0xA0)
					emitrune(text, '\\');
				if(r == 0xA0)
					r = ' ';
				emitrune(text, r);
				text->space = 0;
			}
		}
	}
	if(tag){
		debugtag(tag, "close");
		if(tag->close){
			tag->close(text, tag);
			tag->close = nil;
		}
		if(up)
			tag->up = up;
	}
}

void
inittext(Text *text)
{
	memset(text, 0, sizeof(Text));
	text->fontstyle = "R";
	text->fontsize = "NL";
	text->output = 1;
}

void
main(void)
{
	Text text;
	Binit(&in, 0, OREAD);
	inittext(&text);
	parsetext(&text, nil);
	emit(&text, "\n");
	write(1, text.bp, text.wp - text.bp);
}
