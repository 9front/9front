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
		int	restore;
	};
};

struct Text {
	char*	font;
	int	pre;
	int	pos;
	int	space;
	int	output;
	int	underline;
};

void eatwhite(void);
Tag *parsetext(Text *, Tag *);
int parsetag(Tag *);
int parseattr(Attr *);

Biobuf in, out;

void
emit(Text *text, char *fmt, ...)
{
	va_list a;

	if(text->pos > 0){
		text->pos = 0;
		Bputc(&out, '\n');
	}
	va_start(a, fmt);
	Bvprint(&out, fmt, a);
	va_end(a);
}

void
restoreoutput(Text *text, Tag *tag)
{
	text->output = tag->restore;
}

void
ongarbage(Text *text, Tag *tag)
{
	tag->restore = text->output;
	tag->close = restoreoutput;
	text->output = 0;
}

void
onp(Text *text, Tag *)
{
	emit(text, ".LP\n");
}

void
restorepre(Text *text, Tag *tag)
{
	text->pre = tag->restore;
	emit(text, ".DE\n");
}

void
onpre(Text *text, Tag *tag)
{
	tag->restore = text->pre;
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
	emit(text, ".SH %c\n", tag->tag[1]);
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
restorefont(Text *text, Tag *tag)
{
	text->font = tag->aux;
	text->pos += Bprint(&out, "\\f%s", text->font);
}

void
onfont(Text *text, Tag *tag)
{
	if(text->font == 0)
		text->font = "R";
	tag->aux = text->font;
	tag->close = restorefont;
	if(cistrcmp(tag->tag, "i") == 0)
		text->font = "I";
	else if(cistrcmp(tag->tag, "b") == 0)
		text->font = "B";
	text->pos += Bprint(&out, "\\f%s", text->font);
}

void
ona(Text *text, Tag *)
{
	text->underline = 1;
}

struct {
	char	*tag;
	void	(*open)(Text *, Tag *);
} ontag[] = {
	"a",		ona,
	"br",		onbr,
	"hr",		onbr,
	"b",		onfont,
	"i",		onfont,
	"p",		onp,
	"h1",		onh,
	"h2",		onh,
	"h3",		onh,
	"h4",		onh,
	"h5",		onh,
	"h6",		onh,
	"li",		onli,
	"pre",		onpre,
	"head",		ongarbage,
	"style",	ongarbage,
	"script",	ongarbage,
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
				if(c == ']')
					if(Bgetc(&in) == ']')
						if(Bgetc(&in) == '>')
							return;
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
					continue;
				}
			default:
				if(r == '\n' || r == '\r')
					text->pos = 0;
				if(text->space){
					text->space = 0;
					if(text->underline){
						emit(text, "");
						text->pos = Bprint(&out, ".UL ");
					} else if(text->pos >= 70){
						text->pos = 0;
						Bputc(&out, '\n');
					} else if(text->pos > 0){
						text->pos++;
						Bputc(&out, ' ');
					}
				}
				if(text->pos == 0 && r == '.'){
					text->pos++;
					Bputc(&out, ' ');
				}
				text->pos++;
				if(r == 0xA0){
					r = ' ';
					Bputc(&out, '\\');
				}
				Bprint(&out, "%C", r);
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
	text.output = 1;
	parsetext(&text, nil);
	emit(&text, "\n");
}
