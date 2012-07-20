#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "mothra.h"
#include "html.h"
#include "rtext.h"

typedef struct Fontdata Fontdata;
struct Fontdata{
	char *name;
	Font *font;
	int space;
}fontlist[4][4]={
	"dejavusans/unicode.12", 0, 0,
	"dejavusans/unicode.12", 0, 0,
	"dejavusans/unicode.14", 0, 0,
	"dejavusans/unicode.16", 0, 0,

	"dejavusansit/unicode.12", 0, 0,
	"dejavusansit/unicode.12", 0, 0,
	"dejavusansit/unicode.14", 0, 0,
	"dejavusansit/unicode.16", 0, 0,

	"dejavusansbd/unicode.12", 0, 0,
	"dejavusansbd/unicode.12", 0, 0,
	"dejavusansbd/unicode.14", 0, 0,
	"dejavusansbd/unicode.16", 0, 0,

	"terminus/unicode.12", 0, 0,
	"terminus/unicode.14", 0, 0,
	"terminus/unicode.16", 0, 0,
	"terminus/unicode.18", 0, 0,
};
Fontdata *pl_whichfont(int f, int s){
	char name[NNAME];

	assert(f >= 0 && f < 4);
	assert(s >= 0 && s < 4);

	if(fontlist[f][s].font==0){
		snprint(name, sizeof(name), "/lib/font/bit/%s.font", fontlist[f][s].name);
		fontlist[f][s].font=openfont(display, name);
		if(fontlist[f][s].font==0) fontlist[f][s].font=font;
		fontlist[f][s].space=stringwidth(fontlist[f][s].font, "0");
	}
	return &fontlist[f][s];
	
}
void getfonts(void){
	int f, s;
	for(f=0;f!=4;f++)
		for(s=0;s!=4;s++)
			pl_whichfont(f, s);
}
void pl_pushstate(Hglob *g, int t){
	++g->state;
	if(g->state==&g->stack[NSTACK]){
		htmlerror(g->name, g->lineno, "stack overflow at <%s>", tag[t].name);
		--g->state;
	}
	g->state[0]=g->state[-1];
	g->state->tag=t;
}
void pl_linespace(Hglob *g){
	plrtbitmap(&g->dst->text, 1000000, 0, linespace, 0, 0);
	g->para=0;
	g->linebrk=0;
}
enum{
	HORIZ,
	VERT,
};

int strtolength(Hglob *g, int dir, char *str){
	double f;
	Point p;

	f = atof(str);
	if(cistrstr(str, "%"))
		return 0;
	if(cistrstr(str, "em")){
		p=stringsize(pl_whichfont(g->state->font, g->state->size)->font, "M");
		return floor(f*((dir==HORIZ) ? p.x : p.y));
	}
	return floor(f);
}

void pl_htmloutput(Hglob *g, int nsp, char *s, Field *field){
	Fontdata *f;
	int space, indent;
	Action *ap;
	if(g->state->tag==Tag_title
/*	|| g->state->tag==Tag_textarea */
	|| g->state->tag==Tag_select){
		if(s){
			if(g->tp!=g->text && g->tp!=g->etext && g->tp[-1]!=' ')
				*g->tp++=' ';
			while(g->tp!=g->etext && *s) *g->tp++=*s++;
			if(g->state->tag==Tag_title) g->dst->changed=1;
			*g->tp='\0';
		}
		return;
	}
	f=pl_whichfont(g->state->font, g->state->size);
	space=f->space;
	indent=g->state->margin;
	if(g->para){
		space=1000000;
		indent+=g->state->indent;
	}
	else if(g->linebrk)
		space=1000000;
	else if(nsp<=0)
		space=0;
	if(g->state->image[0]==0 && g->state->link[0]==0 && g->state->name[0]==0 && field==0)
		ap=0;
	else{
		ap=mallocz(sizeof(Action), 1);
		if(ap!=0){
			if(g->state->image[0])
				ap->image = strdup(g->state->image);
			if(g->state->link[0])
				ap->link = strdup(g->state->link);
			if(g->state->name[0])
				ap->name = strdup(g->state->name);
			ap->ismap=g->state->ismap;
			ap->width=g->state->width;
			ap->height=g->state->height;
			ap->field=field;
		}
	}
	if(space<0) space=0;
	if(indent<0) indent=0;
	if(g->state->pre && s[0]=='\t'){
		space=0;
		while(s[0]=='\t'){
			space++;
			s++;
		}
		space=PL_TAB|space;
		if(g->linebrk){
			indent=space;
			space=1000000;
		}
	}
	plrtstr(&g->dst->text, space, indent, f->font, strdup(s),
		g->state->link[0] || g->state->image[0], ap);
	g->para=0;
	g->linebrk=0;
	g->dst->changed=1;
}

/*
 * Buffered read, no translation
 * Save in cache.
 */
int pl_bread(Hglob *g){
	int n, c;
	char err[1024];
	if(g->hbufp==g->ehbuf){
		n=read(g->hfd, g->hbuf, NHBUF);
		if(n<=0){
			if(n<0){
				snprint(err, sizeof(err), "%r reading %s", g->name);
				pl_htmloutput(g, 1, err, 0);
			}
			g->heof=1;
			return EOF;
		}
		g->hbufp=g->hbuf;
		g->ehbuf=g->hbuf+n;
	}
	c=*g->hbufp++&255;
	if(c=='\n') g->lineno++;
	return c;
}
/*
 * Read a character, translating \r\n, \n\r, \r and \n into \n
 */
int pl_readc(Hglob *g){
	int c;
	static int peek=-1;
	if(peek!=-1){
		c=peek;
		peek=-1;
	}
	else
		c=pl_bread(g);
	if(c=='\r'){
		c=pl_bread(g);
		if(c!='\n') peek=c;
		return '\n';
	}
	if(c=='\n'){
		c=pl_bread(g);
		if(c!='\r') peek=c;
		return '\n';
	}
	return c;
}
void pl_putback(Hglob *g, int c){
	if(g->npeekc==NPEEKC) htmlerror(g->name, g->lineno, "too much putback!");
	else if(c!=EOF) g->peekc[g->npeekc++]=c;
}
int pl_nextc(Hglob *g){
	int c;
	int n;
	Rune r;
	char crune[UTFmax+1];
	if(g->heof) return EOF;
	if(g->npeekc!=0) return g->peekc[--g->npeekc];
	c=pl_readc(g);
	if(c=='<'){
		c=pl_readc(g);
		if(c=='/'){
			c=pl_readc(g);
			pl_putback(g, c);
			pl_putback(g, '/');
			if('a'<=c && c<='z' || 'A'<=c && c<='Z') return STAG;
			return '<';
		}
		pl_putback(g, c);
		if(c=='!' || 'a'<=c && c<='z' || 'A'<=c && c<='Z' || c=='?') return STAG;
		return '<';
	}
	if(c=='>') return ETAG;
	if(c==EOF) return c;
	for (n=1; n<=sizeof(crune); n++){
		crune[n-1]=c;
		if(fullrune(crune, n)){
			chartorune(&r, crune);
			return r;
		}
		c=pl_readc(g);
		if(c==EOF)
			return EOF;
	}
	return c;
}
char *unquot(char *dst, char *src, int len){
	char *e;

	e=0;
	while(*src && strchr(" \t\r\n", *src))
		src++;
	if(*src=='\'' || *src=='"'){
		e=strrchr(src+1, *src);
		src++;
	}
	if(e==0) e=strchr(src, 0);
	len--;
	if((e - src) < len)
		len=e-src;
	if(len>0) memmove(dst, src, len);
	dst[len]=0;
	return dst;
}
int alnumchar(int c){
	return 'a'<=c && c<='z' || 'A'<=c && c<='Z' || '0'<=c && c<='9';
}
int entchar(int c){
	return c=='#' || alnumchar(c);
}

/* return url if text token looks like a hyperlink */
char *linkify(char *s){
	if(!cistrncmp(s, "http://", 7))
		return strdup(s);
	if(!cistrncmp(s, "https://", 8))
		return strdup(s);
	if(!cistrncmp(s, "www.", 4)){
		int d, i;

		d = 1;
		for(i=4; s[i]; i++){
			if(s[i] == '.'){
				if(s[i-1] == '.')
					return 0;
				d++;
			} else if(!alnumchar(s[i]))
				break;
		}
		if(d >= 2)
			return smprint("http://%s", s);
	}
	return 0;
}

/*
 * remove entity references, in place.
 * Potential bug:
 *	This doesn't work if removing an entity reference can lengthen the string!
 *	Fortunately, this doesn't happen.
 */
void pl_rmentities(Hglob *g, char *s){
	char *t, *u, c, svc;
	Entity *ep;
	Rune r;
	t=s;
	do{
		c=*s++;
		if(c=='&'
		&& ((*s=='#' && strchr("0123456789Xx", s[1]))
		  || 'a'<=*s && *s<='z'
		  || 'A'<=*s && *s<='Z')){
			u=s;
			while(entchar(*s)) s++;
			svc=*s;
			*s = 0;
			if(svc==';') s++;
			if(strcmp(u, "lt") == 0)
				*t++='<';
			else if(strcmp(u, "gt") == 0)
				*t++='>';
			else if(strcmp(u, "quot") == 0)
				*t++='"';
			else if(strcmp(u, "apos") == 0)
				*t++='\'';
			else if(strcmp(u, "amp") == 0)
				*t++='&';
			else {
				if(svc==';') s--;
				*s=svc;
				*t++='&';
				while(u<s)
					*t++=*u++;
			}
		}	
		else *t++=c;
	}while(c);
}
/*
 * Skip over white space
 */
char *pl_white(char *s){
	while(*s==' ' || *s=='\t' || *s=='\n' || *s=='\r') s++;
	return s;
}
/*
 * Skip over HTML word
 */
char *pl_word(char *s){
	if ('a'<=*s && *s<='z' || 'A'<=*s && *s<='Z') {
		s++;
		while('a'<=*s && *s<='z' || 'A'<=*s && *s<='Z' || '0'<=*s && *s<='9' || 
			*s=='-' || *s=='.' || *s==':') s++;
	}
	return s;
}
/*
 * Skip to matching quote
 */
char *pl_quote(char *s){
	char q;
	q=*s++;
	while(*s!=q && *s!='\0') s++;
	return s;
}
void pl_dnl(char *s){
	char *t;
	for(t=s;*s;s++) if(*s!='\r' && *s!='\n') *t++=*s;
	*t='\0';
}
void pl_tagparse(Hglob *g, char *str){
	char *s, *t, *name, c;
	Pair *ap;
	Tag *tagp;
	g->tag=Tag_end;
	ap=g->attr;
	if(str[0]=='!'){	/* test should be strncmp(str, "!--", 3)==0 */
		g->tag=Tag_comment;
		ap->name=0;
		return;
	}
	if(str[0]=='/') str++;
	name=str;
	s=pl_word(str);
	if(*s!='/' && *s!=' ' && *s!='\n' && *s!='\t' && *s!='\0'){
		htmlerror(g->name, g->lineno, "bad tag name in %s", str);
		ap->name=0;
		return;
	}
	if(*s!='\0') *s++='\0';
	for(t=name;t!=s;t++) if('A'<=*t && *t<='Z') *t+='a'-'A';
	/*
	 * Binary search would be faster here
	 */
	for(tagp=tag;tagp->name;tagp++) if(strcmp(name, tagp->name)==0) break;
	g->tag=tagp-tag;
	if(g->tag==Tag_end) htmlerror(g->name, g->lineno, "no tag %s", name);
	for(;;){
		s=pl_white(s);
		if(*s=='\0'){
			ap->name=0;
			return;
		}
		ap->name=s;
		s=pl_word(s);
		t=pl_white(s);
		c=*t;
		*s='\0';
		for(s=ap->name;*s;s++) if('A'<=*s && *s<='Z') *s+='a'-'A';
		if(c=='='){
			s=pl_white(t+1);
			if(*s=='\'' || *s=='"'){
				ap->value=s+1;
				s=pl_quote(s);
				if(*s=='\0'){
					htmlerror(g->name, g->lineno,
						"No terminating quote in rhs of attribute %s",
						ap->name);
					ap->name=0;
					return;
				}
				*s++='\0';
				pl_dnl(ap->value);
			}
			else{
				/* read up to white space or > */
				ap->value=s;
				while(*s!=' ' && *s!='\t' && *s!='\n' && *s!='\0') s++;
				if(*s!='\0') *s++='\0';
			}
			pl_rmentities(g, ap->value);
		}
		else{
			if(c!='\0') s++;
			ap->value="";
		}
		if(ap==&g->attr[NATTR-1])
			htmlerror(g->name, g->lineno, "too many attributes!");
		else ap++;
	}
}
int pl_getcomment(Hglob *g){
	int c;
	if((c=pl_nextc(g))=='-' && (c=pl_nextc(g))=='-'){
		/* <!-- eats everything until --> or EOF */
		for(;;){
			while((c=pl_nextc(g))!='-' && c!=EOF)
				;
			if(c==EOF)
				break;
			if((c=pl_nextc(g))=='-'){
				while((c=pl_nextc(g))=='-')
					;
				if(c==ETAG || c==EOF)
					break;
			}
		}
	} else {
		/* <! eats everything until > or EOF */
		while(c!=ETAG && c!=EOF)
			c=pl_nextc(g);
	}
	if(c==EOF)
		htmlerror(g->name, g->lineno, "EOF in comment");
	g->tag=Tag_comment;
	g->attr->name=0;
	g->token[0]='\0';
	return TAG;
}
int lrunetochar(char *p, int v)
{
	Rune r;

	r=v;
	return runetochar(p, &r);
}

/*
 * Read a start or end tag -- the caller has read the initial <
 */
int pl_gettag(Hglob *g){
	char *tokp;
	int c, q;
	tokp=g->token;
	if((c=pl_nextc(g))=='!' || c=='?')
		return pl_getcomment(g);
	pl_putback(g, c);
	q = 0;
	while((c=pl_nextc(g))!=EOF){
		if(c == '=' && q == 0)
			q = '=';
		else if(c == '\'' || c == '"'){
			if(q == '=')
				q = c;
			else if(q == c)
				q = 0;
		} else if(c == ETAG && q != '\'' && q != '"')
			break;
		if(tokp < &g->token[NTOKEN-UTFmax-1])
			tokp += lrunetochar(tokp, c);
	}
	*tokp='\0';
	if(c==EOF) htmlerror(g->name, g->lineno, "EOF in tag");
	pl_tagparse(g, g->token);
	if(g->token[0]!='/') return TAG;
	if(g->attr[0].name!=0)
		htmlerror(g->name, g->lineno, "end tag should not have attributes");
	return ENDTAG;
}
/*
 * The next token is a tag, an end tag or a sequence of
 * non-white characters.
 * If inside <pre>, newlines are converted to <br> and spaces are preserved.
 * Otherwise, spaces and newlines are noted and discarded.
 */
int pl_gettoken(Hglob *g){
	char *tokp;
	int c;
	if(g->state->pre) switch(c=pl_nextc(g)){
	case STAG: return pl_gettag(g);
	case EOF: return EOF;
	case '\n':
		pl_tagparse(g, "br");
		return TAG;
	default:
		tokp=g->token;
		while(c=='\t'){
			if(tokp < &g->token[NTOKEN-UTFmax-1]) tokp += lrunetochar(tokp, c);
			c=pl_nextc(g);
		}
		while(c!='\t' && c!='\n' && c!=STAG && c!=EOF){
			if(c==ETAG) c='>';
			if(tokp < &g->token[NTOKEN-UTFmax-1]) tokp += lrunetochar(tokp, c);
			c=pl_nextc(g);
		}
		*tokp='\0';
		pl_rmentities(g, g->token);
		pl_putback(g, c);
		g->nsp=0;
		g->spacc=0;
		return TEXT;
	}
	while((c=pl_nextc(g))==' ' || c=='\t' || c=='\n')
		if(g->spacc!=-1)
			g->spacc++;
	switch(c){
	case STAG: return pl_gettag(g);
	case EOF: return EOF;
	default:
		tokp=g->token;
		do{
			if(c==ETAG) c='>';
			if(tokp < &g->token[NTOKEN-UTFmax-1]) tokp += lrunetochar(tokp, c);
			c=pl_nextc(g);
		}while(c!=' ' && c!='\t' && c!='\n' && c!=STAG && c!=EOF);
		*tokp='\0';
		pl_rmentities(g, g->token);
		pl_putback(g, c);
		g->nsp=g->spacc;
		g->spacc=0;
		return TEXT;
	}
}
char *pl_getattr(Pair *attr, char *name){
	for(;attr->name;attr++)
		if(strcmp(attr->name, name)==0)
			return attr->value;
	return 0;
}
int pl_hasattr(Pair *attr, char *name){
	for(;attr->name;attr++)
		if(strcmp(attr->name, name)==0)
			return 1;
	return 0;
}
void plaintext(Hglob *g){
	char line[NLINE];
	char *lp, *elp;
	int c;
	g->state->font=CWIDTH;
	g->state->size=NORMAL;
	elp=&line[NLINE-UTFmax-1];
	lp=line;
	for(;;){
		c=pl_readc(g);
		if(c==EOF) break;
		if(c=='\n' || lp>=elp){
			*lp='\0';
			g->linebrk=1;
			pl_htmloutput(g, 0, line, 0);
			lp=line;
		}
		if(c=='\t'){
			do *lp++=' '; while(lp<elp && utfnlen(line, lp-line)%8!=0);
		}
		else if(c!='\n')
			lp += lrunetochar(lp, c);
	}
	if(lp!=line){
		*lp='\0';
		g->linebrk=1;
		pl_htmloutput(g, 0, line, 0);
	}
}
void plrdplain(char *name, int fd, Www *dst){
	Hglob g;
	g.state=g.stack;
	g.state->tag=Tag_html;
	g.state->font=CWIDTH;
	g.state->size=NORMAL;
	g.state->pre=0;
	g.state->image[0]=0;
	g.state->link[0]=0;
	g.state->name[0]=0;
	g.state->margin=0;
	g.state->indent=20;
	g.state->ismap=0;
	g.dst=dst;
	g.hfd=fd;
	g.name=name;
	g.ehbuf=g.hbufp=g.hbuf;
	g.npeekc=0;
	g.heof=0;
	g.lineno=1;
	g.linebrk=1;
	g.para=0;
	g.text=dst->title;
	g.tp=g.text;
	g.etext=g.text+NTITLE-1;
	g.spacc=0;
	g.form=0;
	nstrcpy(g.text, name, NTITLE);
	plaintext(&g);
	finish(dst);
}
void plrdhtml(char *name, int fd, Www *dst){
	int t, tagerr;
	Stack *sp;
	char buf[20];
	char *str;
	Hglob g;

	g.state=g.stack;
	g.state->tag=Tag_html;
	g.state->font=ROMAN;
	g.state->size=NORMAL;
	g.state->pre=0;
	g.state->image[0]=0;
	g.state->link[0]=0;
	g.state->name[0]=0;
	g.state->margin=0;
	g.state->indent=25;
	g.state->ismap=0;
	g.state->width=0;
	g.state->height=0;
	g.dst=dst;
	g.hfd=fd;
	g.name=name;
	g.ehbuf=g.hbufp=g.hbuf;
	g.npeekc=0;
	g.heof=0;
	g.lineno=1;
	g.linebrk=1;
	g.para=0;
	g.text=dst->title;
	g.tp=g.text;
	g.etext=g.text+NTITLE-1;
	dst->title[0]='\0';
	g.spacc=0;
	g.form=0;

	for(;;) switch(pl_gettoken(&g)){
	case TAG:
		switch(tag[g.tag].action){
		case OPTEND:
			for(sp=g.state;sp!=g.stack && sp->tag!=g.tag;--sp);
			if(sp->tag!=g.tag)
				pl_pushstate(&g, g.tag);
			else
				for(;g.state!=sp;--g.state)
					if(tag[g.state->tag].action!=OPTEND)
						htmlerror(g.name, g.lineno,
							"end tag </%s> missing",
							tag[g.state->tag].name);
			break;
		case END:
			pl_pushstate(&g, g.tag);
			break;
		}
		if(str=pl_getattr(g.attr, "id")){
			char swap[NNAME];

			nstrcpy(swap, g.state->name, sizeof(swap));
			nstrcpy(g.state->name, str, sizeof(g.state->name));
			pl_htmloutput(&g, 0, "", 0);
			nstrcpy(g.state->name, swap, sizeof(g.state->name));
		}
		switch(g.tag){
		default:
			htmlerror(g.name, g.lineno,
				"unimplemented tag <%s>", tag[g.tag].name);
			break;
		case Tag_end:	/* unrecognized start tag */
			break;
		case Tag_img:
			if(str=pl_getattr(g.attr, "src"))
				nstrcpy(g.state->image, str, sizeof(g.state->image));
			g.state->ismap=pl_hasattr(g.attr, "ismap");
			if(str=pl_getattr(g.attr, "width"))
				g.state->width=strtolength(&g, HORIZ, str);
			if(str=pl_getattr(g.attr, "height"))
				g.state->height=strtolength(&g, VERT, str);
			str=pl_getattr(g.attr, "alt");
			if(str==0){
				if(g.state->image[0])
					str=g.state->image;
				else
					str="[[image]]";
			}
			pl_htmloutput(&g, 0, str, 0);
			g.state->image[0]=0;
			g.state->ismap=0;
			g.state->width=0;
			g.state->height=0;
			break;
		case Tag_plaintext:
			g.spacc=0;
			plaintext(&g);
			break;
		case Tag_comment:
		case Tag_html:
		case Tag_link:
		case Tag_nextid:
		case Tag_table:
			break;
		case Tag_tr:
			g.spacc=0;
			g.linebrk=1;
			break;
		case Tag_td:
			g.spacc++;
			break;
		case Tag_base:
			if(str=pl_getattr(g.attr, "href")){
				seturl(g.dst->url, str, g.dst->url->fullname);
				nstrcpy(g.dst->url->fullname, str, sizeof(g.dst->url->fullname));
				/* base should be a full url, but it often isnt so have to resolve */
				urlresolve(g.dst->url);
			}
			break;
		case Tag_a:
			if(str=pl_getattr(g.attr, "name"))
				nstrcpy(g.state->name, str, sizeof(g.state->name));
			pl_htmloutput(&g, 0, "", 0);
			if(str=pl_getattr(g.attr, "href"))
				nstrcpy(g.state->link, str, sizeof(g.state->link));
			break;
		case Tag_meta:
			if((str=pl_getattr(g.attr, "http-equiv"))==0)
				break;
			if(cistrcmp(str, "refresh"))
				break;
			if((str=pl_getattr(g.attr, "content"))==0)
				break;
			if((str=strchr(str, '='))==0)
				break;
			str++;
			pl_htmloutput(&g, 0, "[refresh: ", 0);
			str=unquot(g.state->link, str, sizeof(g.state->link));
			pl_htmloutput(&g, 0, str, 0);
			g.state->link[0]=0;
			pl_htmloutput(&g, 0, "]", 0);
			g.linebrk=1;
			g.spacc=0;
			break;
		case Tag_source:
		case Tag_video:
		case Tag_audio:
		case Tag_embed:
		case Tag_frame:
		case Tag_iframe:
			snprint(buf, sizeof(buf), "[%s: ", tag[g.tag].name);
			pl_htmloutput(&g, 0, buf, 0);
			if(str=pl_getattr(g.attr, "src"))
				nstrcpy(g.state->link, str, sizeof(g.state->link));
			if(str=pl_getattr(g.attr, "name"))
				nstrcpy(g.state->name, str, sizeof(g.state->name));
			else
				str = g.state->link;
			pl_htmloutput(&g, 0, str, 0);
			g.state->link[0]=0;
			g.state->name[0]=0;
			pl_htmloutput(&g, 0, "]", 0);
			g.linebrk=1;
			g.spacc=0;
			break;
		case Tag_address:
			g.spacc=0;
			g.linebrk=1;
			g.state->font=ROMAN;
			g.state->size=NORMAL;
			g.state->margin=300;
			g.state->indent=50;
			break;
		case Tag_b:
		case Tag_strong:
			g.state->font=BOLD;
			break;
		case Tag_blockquot:
			g.spacc=0;
			g.linebrk=1;
			g.state->margin+=50;
			g.state->indent=20;
			break;
		case Tag_body:
			break;
		case Tag_head:
			g.state->font=ROMAN;
			g.state->size=NORMAL;
			g.state->margin=0;
			g.state->indent=20;
			g.spacc=0;
			break;
		case Tag_div:
		case Tag_br:
			g.spacc=0;
			g.linebrk=1;
			break;
		case Tag_span:
		case Tag_center:
			/* more to come */
			break;
		case Tag_cite:
		case Tag_acronym:
			g.state->font=ITALIC;
			g.state->size=NORMAL;
			break;
		case Tag_code:
			g.state->font=CWIDTH;
			g.state->size=NORMAL;
			break;
		case Tag_dd:
			g.linebrk=1;
			g.state->indent=0;
			g.state->font=ROMAN;
			g.spacc=0;
			break;
		case Tag_dfn:
			htmlerror(g.name, g.lineno, "<dfn> deprecated");
		case Tag_abbr:
			g.state->font=BOLD;
			g.state->size=NORMAL;
			break;
		case Tag_dl:
			g.state->font=BOLD;
			g.state->size=NORMAL;
			g.state->margin+=40;
			g.spacc=0;
			break;
		case Tag_dt:
			g.para=1;
			g.state->indent=-40;
			g.state->font=BOLD;
			g.spacc=0;
			break;
		case Tag_font:
			/* more to come */
			break;
		case Tag_u:
			htmlerror(g.name, g.lineno, "<u> deprecated");
		case Tag_em:
		case Tag_i:
		case Tag_var:
			g.state->font=ITALIC;
			break;
		case Tag_h1:
			g.linebrk=1;
			g.state->font=BOLD;
			g.state->size=ENORMOUS;
			g.state->margin+=100;
			g.spacc=0;
			break;
		case Tag_h2:
			pl_linespace(&g);
			g.state->font=BOLD;
			g.state->size=ENORMOUS;
			g.spacc=0;
			break;
		case Tag_h3:
			g.linebrk=1;
			pl_linespace(&g);
			g.state->font=ITALIC;
			g.state->size=ENORMOUS;
			g.state->margin+=20;
			g.spacc=0;
			break;
		case Tag_h4:
			pl_linespace(&g);
			g.state->font=BOLD;
			g.state->size=LARGE;
			g.state->margin+=10;
			g.spacc=0;
			break;
		case Tag_h5:
			pl_linespace(&g);
			g.state->font=ITALIC;
			g.state->size=LARGE;
			g.state->margin+=10;
			g.spacc=0;
			break;
		case Tag_h6:
			pl_linespace(&g);
			g.state->font=BOLD;
			g.state->size=LARGE;
			g.spacc=0;
			break;
		case Tag_hr:
			g.spacc=0;
			plrtbitmap(&g.dst->text, 1000000, g.state->margin, hrule, 0, 0);
			break;
		case Tag_key:
			htmlerror(g.name, g.lineno, "<key> deprecated");
		case Tag_kbd:
			g.state->font=CWIDTH;
			break;
		case Tag_dir:
		case Tag_menu:
		case Tag_ol:
		case Tag_ul:
			g.state->number=0;
			g.linebrk=1;
			g.state->margin+=25;
			g.state->indent=-25;
			g.spacc=0;
			break;
		case Tag_li:
			g.spacc=0;
			switch(g.state->tag){
			default:
				htmlerror(g.name, g.lineno, "can't have <li> in <%s>",
					tag[g.state->tag].name);
			case Tag_dir:	/* supposed to be multi-columns, can't do! */
			case Tag_menu:
				g.linebrk=1;
				break;
			case Tag_ol:
				g.para=1;
				snprint(buf, sizeof(buf), "%2d  ", ++g.state->number);
				pl_htmloutput(&g, 0, buf, 0);
				break;
			case Tag_ul:
				g.para=0;
				g.linebrk=0;
				g.spacc=-1;
				plrtbitmap(&g.dst->text, 100000,
					g.state->margin+g.state->indent, bullet, 0, 0);
				break;
			}
			break;
		case Tag_p:
			pl_linespace(&g);
			g.linebrk=1;
			g.spacc=0;
			break;
		case Tag_listing:
		case Tag_xmp:
			htmlerror(g.name, g.lineno, "<%s> deprecated", tag[g.tag].name);
		case Tag_pre:
		case Tag_samp:
			g.state->indent=0;
			g.state->pre=1;
			g.state->font=CWIDTH;
			g.state->size=NORMAL;
			pl_linespace(&g);
			break;
		case Tag_tt:
			g.state->font=CWIDTH;
			g.state->size=NORMAL;
			break;
		case Tag_title:
			g.text=dst->title+strlen(dst->title);
			g.tp=g.text;
			g.etext=dst->title+NTITLE-1;
			break;
		case Tag_form:
		case Tag_input:
		case Tag_button:
		case Tag_select:
		case Tag_option:
		case Tag_textarea:
		case Tag_isindex:
			rdform(&g);
			break;
		case Tag_script:
		case Tag_object:
		case Tag_applet:
		case Tag_style:
			/*
			 * ignore the content of these tags, eat tokens until we
			 * reach a matching endtag.
			 */
			t = g.tag;
			for(;;){
				switch(pl_gettoken(&g)){
				default:
					continue;
				case ENDTAG:
					if(g.tag != t)
						continue;
				case EOF:
					break;
				}
				break;
			}
			break;
		}
		break;

	case ENDTAG:
		/*
		 * If the end tag doesn't match the top, we try to uncover a match
		 * on the stack.
		 */
		if(g.state->tag!=g.tag){
			tagerr=0;
			for(sp=g.state;sp!=g.stack;--sp){
				if(sp->tag==g.tag)
					break;
				if(tag[g.state->tag].action!=OPTEND) tagerr++;
			}
			if(sp==g.stack){
				if(tagerr)
					htmlerror(g.name, g.lineno,
						"end tag mismatch <%s>...</%s>, ignored",
						tag[g.state->tag].name, tag[g.tag].name);
			}
			else{
				if(tagerr)
					htmlerror(g.name, g.lineno,
						"end tag mismatch <%s>...</%s>, "
						"intervening tags popped",
						tag[g.state->tag].name, tag[g.tag].name);
				g.state=sp-1;
			}
		}
		else if(g.state==g.stack)
			htmlerror(g.name, g.lineno, "end tag </%s> at stack bottom",
				tag[g.tag].name);
		else
			--g.state;
		switch(g.tag){
		case Tag_select:
		case Tag_form:
		case Tag_textarea:
			endform(&g);
			break;
		case Tag_h1:
		case Tag_h2:
		case Tag_h3:
		case Tag_h4:
			pl_linespace(&g);
			break;
		case Tag_div:
		case Tag_address:
		case Tag_blockquot:
		case Tag_body:
		case Tag_dir:
		case Tag_dl:
		case Tag_dt:
		case Tag_h5:
		case Tag_h6:
		case Tag_listing:
		case Tag_menu:
		case Tag_ol:
		case Tag_samp:
		case Tag_title:
		case Tag_ul:
		case Tag_xmp:
		case Tag_table:
			g.linebrk=1;
			break;
		case Tag_pre:
			pl_linespace(&g);
			break;
		}
		break;
	case TEXT:
		if(g.state->link[0]==0 && (str = linkify(g.token))){
			nstrcpy(g.state->link, str, sizeof(g.state->link));
			pl_htmloutput(&g, g.nsp, g.token, 0);
			g.state->link[0] = 0;
			free(str);
		} else
			pl_htmloutput(&g, g.nsp, g.token, 0);
		break;
	case EOF:
		for(;g.state!=g.stack;--g.state)
			if(tag[g.state->tag].action!=OPTEND)
				htmlerror(g.name, g.lineno,
					"missing </%s> at EOF", tag[g.state->tag].name);
		*g.tp='\0';
		getpix(dst->text, dst);
		finish(dst);
		return;
	}
}
