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
	char	font;
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
	text->font = tag->restore;
	text->pos += Bprint(&out, "\\f%c", text->font);
}

void
onfont(Text *text, Tag *tag)
{
	if(text->font == 0)
		text->font = 'R';
	tag->restore = text->font;
	tag->close = restorefont;
	if(cistrcmp(tag->tag, "i") == 0)
		text->font = 'I';
	else if(cistrcmp(tag->tag, "b") == 0)
		text->font = 'B';
	text->pos += Bprint(&out, "\\f%c", text->font);
}

struct {
	char	*tag;
	void	(*open)(Text *, Tag *);
} ontag[] = {
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

struct {
	char	*entity;
	Rune	rune;
} entities[] = {
	"AElig", 198,	"Aacute", 193,	"Acirc", 194,	"Agrave", 192,	
	"Alpha", 913,	"Aring", 197,	"Atilde", 195,	"Auml", 196,	
	"Beta", 914,	"Ccedil", 199,	"Chi", 935,	"Dagger", 8225,	
	"Delta", 916,	"ETH", 208,	"Eacute", 201,	"Ecirc", 202,	
	"Egrave", 200,	"Epsilon", 917,	"Eta", 919,	"Euml", 203,	
	"Gamma", 915,	"Iacute", 205,	"Icirc", 206,	"Igrave", 204,	
	"Iota", 921,	"Iuml", 207,	"Kappa", 922,	"Lambda", 923,	
	"Mu", 924,	"Ntilde", 209,	"Nu", 925,	"OElig", 338,	
	"Oacute", 211,	"Ocirc", 212,	"Ograve", 210,	"Omega", 937,	
	"Omicron", 927,	"Oslash", 216,	"Otilde", 213,	"Ouml", 214,	
	"Phi", 934,	"Pi", 928,	"Prime", 8243,	"Psi", 936,	
	"Rho", 929,	"Scaron", 352,	"Sigma", 931,	"THORN", 222,	
	"Tau", 932,	"Theta", 920,	"Uacute", 218,	"Ucirc", 219,	
	"Ugrave", 217,	"Upsilon", 933,	"Uuml", 220,	"Xi", 926,	
	"Yacute", 221,	"Yuml", 376,	"Zeta", 918,	"aacute", 225,	
	"acirc", 226,	"acute", 180,	"aelig", 230,	"agrave", 224,	
	"alefsym", 8501,"alpha", 945,	"amp", 38,	"and", 8743,	
	"ang", 8736,	"aring", 229,	"asymp", 8776,	"atilde", 227,	
	"auml", 228,	"bdquo", 8222,	"beta", 946,	"brvbar", 166,	
	"bull", 8226,	"cap", 8745,	"ccedil", 231,	"cdots", 8943,	
	"cedil", 184,	"cent", 162,	"chi", 967,	"circ", 710,	
	"clubs", 9827,	"cong", 8773,	"copy", 169,	"crarr", 8629,	
	"cup", 8746,	"curren", 164,	"dArr", 8659,	"dagger", 8224,	
	"darr", 8595,	"ddots", 8945,	"deg", 176,	"delta", 948,	
	"diams", 9830,	"divide", 247,	"eacute", 233,	"ecirc", 234,	
	"egrave", 232,	"emdash", 8212,	"empty", 8709,	"emsp", 8195,	
	"endash", 8211,	"ensp", 8194,	"epsilon", 949,	"equiv", 8801,	
	"eta", 951,	"eth", 240,	"euml", 235,	"euro", 8364,	
	"exist", 8707,	"fnof", 402,	"forall", 8704,	"frac12", 189,	
	"frac14", 188,	"frac34", 190,	"frasl", 8260,	"gamma", 947,	
	"ge", 8805,	"gt", 62,	"hArr", 8660,	"harr", 8596,	
	"hearts", 9829,	"hellip", 8230,	"iacute", 237,	"icirc", 238,	
	"iexcl", 161,	"igrave", 236,	"image", 8465,	"infin", 8734,	
	"int", 8747,	"iota", 953,	"iquest", 191,	"isin", 8712,	
	"iuml", 239,	"kappa", 954,	"lArr", 8656,	"lambda", 955,	
	"lang", 9001,	"laquo", 171,	"larr", 8592,	"lceil", 8968,	
	"ldots", 8230,	"ldquo", 8220,	"le", 8804,	"lfloor", 8970,	
	"lowast", 8727,	"loz", 9674,	"lrm", 8206,	"lsaquo", 8249,	
	"lsquo", 8216,	"lt", 60,	"macr", 175,	"mdash", 8212,	
	"micro", 181,	"middot", 183,	"minus", 8722,	"mu", 956,	
	"nabla", 8711,	"nbsp", 160,	"ndash", 8211,	"ne", 8800,	
	"ni", 8715,	"not", 172,	"notin", 8713,	"nsub", 8836,	
	"ntilde", 241,	"nu", 957,	"oacute", 243,	"ocirc", 244,	
	"oelig", 339,	"ograve", 242,	"oline", 8254,	"omega", 969,	
	"omicron", 959,	"oplus", 8853,	"or", 8744,	"ordf", 170,	
	"ordm", 186,	"oslash", 248,	"otilde", 245,	"otimes", 8855,	
	"ouml", 246,	"para", 182,	"part", 8706,	"permil", 8240,	
	"perp", 8869,	"phi", 966,	"pi", 960,	"piv", 982,	
	"plusmn", 177,	"pound", 163,	"prime", 8242,	"prod", 8719,	
	"prop", 8733,	"psi", 968,	"quad", 8193,	"quot", 34,	
	"rArr", 8658,	"radic", 8730,	"rang", 9002,	"raquo", 187,	
	"rarr", 8594,	"rceil", 8969,	"rdquo", 8221,	"real", 8476,	
	"reg", 174,	"rfloor", 8971,	"rho", 961,	"rlm", 8207,	
	"rsaquo", 8250,	"rsquo", 8217,	"sbquo", 8218,	"scaron", 353,	
	"sdot", 8901,	"sect", 167,	"shy", 173,	"sigma", 963,	
	"sigmaf", 962,	"sim", 8764,	"sp", 8194,	"spades", 9824,	
	"sub", 8834,	"sube", 8838,	"sum", 8721,	"sup", 8835,	
	"sup1", 185,	"sup2", 178,	"sup3", 179,	"supe", 8839,	
	"szlig", 223,	"tau", 964,	"there4", 8756,	"theta", 952,	
	"thetasym", 977,"thinsp", 8201,	"thorn", 254,	"tilde", 732,	
	"times", 215,	"trade", 8482,	"uArr", 8657,	"uacute", 250,	
	"uarr", 8593,	"ucirc", 251,	"ugrave", 249,	"uml", 168,	
	"upsih", 978,	"upsilon", 965,	"uuml", 252,	"varepsilon", 8712,	
	"varphi", 981,	"varpi", 982,	"varrho", 1009,	"vdots", 8942,	
	"vsigma", 962,	"vtheta", 977,	"weierp", 8472,	"xi", 958,	
	"yacute", 253,	"yen", 165,	"yuml", 255,	"zeta", 950,	
	"zwj", 8205,	"zwnj", 8204,
};

Rune
parserune(int c)
{
	char buf[10];
	int i, n;
	Rune r;

	n = 0;
	if(c == '&'){
		while((c = Bgetc(&in)) > 0){
			if(strchr("\n\r\t ;</>", c)){
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
		if(buf[0] == '#')
			return atoi(buf+1);
		for(i=0; i<nelem(entities); i++){
			n = strcmp(buf, entities[i].entity);
			if(n == 0)
				return entities[i].rune;
			if(n < 0)
				break;
		}
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
					if(text->pos >= 70){
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
