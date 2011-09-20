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
};

void eatwhite(void);
Tag *parsetext(Text *, Tag *);
int parsetag(Tag *);
int parseattr(Attr *);

Biobuf in, out;

void
emitrune(Text *text, Rune r)
{
	if(r == '\r' || r =='\n')
		text->pos = 0;
	else
		text->pos++;
	Bputrune(&out, r);
}

void
emit(Text *text, char *fmt, ...)
{
	Rune buf[64];
	va_list a;
	int i;

	if(fmt[0] == '.' && text->pos){
		emitrune(text, '\n');
		text->space = 0;
	}
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

void
ontt(Text *text, Tag *tag)
{
	tag->aux = text->fontsize;
	tag->close = restorefontsize;
	fontsize(text, "CW");
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
	"kbd",		ontt,
	"li",		onli,
	"p",		onp,
	"pre",		onpre,
	"q",		onquote,
	"samp",		ontt,
	"script",	ongarbage,
	"small",	onsmall,
	"strong",	onb,
	"style",	ongarbage,
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
	return 0xFFFD;
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
	if(1) return;

	if(tag == nil)
		return;
	debugtag(tag->up, nil);
	fprint(2, "%s %s%s", tag->tag, dbg ? dbg : " > ", dbg ? "\n" : "");
}


Tag*
parsetext(Text *text, Tag *tag)
{
	Tag *rtag;
	Rune r;
	int c;

	rtag = tag;
	debugtag(tag, "open");
	if(tag == nil || tag->closing == 0){
		while((c = Bgetc(&in)) > 0){
			if(c == '<'){
				Tag t;

				memset(&t, 0, sizeof(t));
				if(parsetag(&t)){
					if(t.opening){
						t.up = tag;
						for(c = 0; c < nelem(ontag); c++){
							if(cistrcmp(t.tag, ontag[c].tag) == 0){
								ontag[c].open(text, &t);
								break;
							}
						}
						rtag = parsetext(text, &t);
						if(rtag == &t)
							rtag = tag;
						else
							break;
					} else if(t.closing){
						while(rtag && cistrcmp(rtag->tag, t.tag))
							rtag = rtag->up;
						if(rtag == nil)
							rtag = tag;
						else
							break;
					}
				}
				continue;
			}
			if(!text->output)
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
					text->space = 0;
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
			}
		}
	}
	debugtag(tag, "close");
	if(tag && tag->close)
		tag->close(text, tag);
	return rtag;
}

void
main(void)
{
	Text text;

	Binit(&in, 0, OREAD);
	Binit(&out, 1, OWRITE);

	memset(&text, 0, sizeof(text));

	text.fontstyle = "R";
	text.fontsize = "NL";
	text.output = 1;

	parsetext(&text, nil);
	emit(&text, "\n");
}
