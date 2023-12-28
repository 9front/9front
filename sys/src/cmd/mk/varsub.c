#include	"mk.h"

static	Word		*subsub(Word*, char*, char*);
static	Word		*expandvar(char**);
static	Bufblock	*varname(char**);
static	Word		*extractpat(char*, char**, char*, char*);
static	int		submatch(char*, Word*, Word*, int*, char**);

Word *
varsub(char **s)
{
	Bufblock *b;
	Word *w;

	if(**s == '{')		/* either ${name} or ${name: A%B==C%D}*/
		return expandvar(s);
	b = varname(s);
	if(b == 0)
		return 0;
	w = getvar(b->start);
	freebuf(b);
	return wdup(w);
}

/*
 *	extract a variable name
 */
static Bufblock*
varname(char **s)
{
	Bufblock *b;
	char *cp;
	Rune r;
	int n;

	b = newbuf();
	cp = *s;
	for(;;){
		n = chartorune(&r, cp);
		if (!WORDCHR(r))
			break;
		rinsert(b, r);
		cp += n;
	}
	if(b->current == b->start){
		SYNERR(-1);
		fprint(2, "missing variable name <%s>\n", *s);
		freebuf(b);
		return 0;
	}
	*s = cp;
	insert(b, 0);
	return b;
}

static Word*
expandvar(char **s)
{
	Word *w;
	Bufblock *buf;
	char *cp, *begin, *end;

	begin = *s;
	(*s)++;						/* skip the '{' */
	buf = varname(s);
	if(buf == 0)
		return 0;
	cp = *s;
	if(*cp == '}') {				/* ${name} variant*/
		(*s)++;					/* skip the '}' */
		w = getvar(buf->start);
		freebuf(buf);
		return wdup(w);
	}
	if(*cp != ':') {
		SYNERR(-1);
		fprint(2, "bad variable name <%s>\n", buf->start);
		freebuf(buf);
		return 0;
	}
	cp++;
	end = charin(cp, "}");
	if(end == 0){
		SYNERR(-1);
		fprint(2, "missing '}': %s\n", begin);
		Exit();
	}
	*end = 0;
	*s = end+1;
	w = getvar(buf->start);
	freebuf(buf);
	if(w)
		w = subsub(w, cp, end);
	return w;
}

static Word*
extractpat(char *s, char **r, char *term, char *end)
{
	char save, *cp;
	Word *w;

	cp = charin(s, term);
	if(cp){
		*r = cp;
		if(cp == s)
			return 0;
		save = *cp;
		*cp = 0;
		w = stow(s);
		*cp = save;
	} else {
		*r = end;
		w = stow(s);
	}
	return w;
}

static Word*
subsub(Word *v, char *s, char *end)
{
	int nmid;
	Word *head, *tail, *w, *h, **l;
	Word *a, *b, *c, *d;
	Bufblock *buf;
	char *cp, *enda;

	a = extractpat(s, &cp, "=%&", end);
	b = c = d = 0;
	if(PERCENT(*cp))
		b = extractpat(cp+1, &cp, "=", end);
	if(*cp == '=')
		c = extractpat(cp+1, &cp, "&%", end);
	if(PERCENT(*cp))
		d = stow(cp+1);
	else if(*cp)
		d = stow(cp);

	head = tail = 0;
	buf = newbuf();
	for(; v; v = v->next){
		h = w = 0;
		l = &h;
		if(submatch(v->s, a, b, &nmid, &enda)){
			/* enda points to end of A match in source;
			 * nmid = number of chars between end of A and start of B
			 */
			if(c){
				*l = w = wdup(c);
				while(w->next){
					l = &w->next;
					w = w->next;
				}
			}
			if(PERCENT(*cp) && nmid > 0){	
				if(w){
					bufcpy(buf, w->s);
					bufncpy(buf, enda, nmid);
					insert(buf, 0);
					delword(w);
					*l = w = newword(buf->start);
				} else {
					bufncpy(buf, enda, nmid);
					insert(buf, 0);
					*l = w = newword(buf->start);
				}
				buf->current = buf->start;
			}
			if(!empty(d)){
				if(w){
					bufcpy(buf, w->s);
					bufcpy(buf, d->s);
					insert(buf, 0);
					delword(w);
					*l = w = newword(buf->start);
					w->next = wdup(d->next);
					buf->current = buf->start;
				} else {
					*l = w = wdup(d);
				}
				while(w->next){
					l = &w->next;
					w = w->next;
				}
			}
		}
		if(w == 0)
			*l = w = newword(v->s);
	
		if(head == 0)
			head = h;
		else
			tail->next = h;
		tail = w;
	}
	freebuf(buf);
	delword(a);
	delword(b);
	delword(c);
	delword(d);
	return head;
}

static int
submatch(char *s, Word *a, Word *b, int *nmid, char **enda)
{
	char *end;
	Word *w;
	int n;

	n = 0;
	for(w = a; w; w = w->next){
		n = strlen(w->s);
		if(strncmp(s, w->s, n) == 0)
			break;
	}
	if(a && w == 0)		/*  a == NULL matches everything*/
		return 0;

	*enda = s+n;		/* pointer to end a A part match */
	*nmid = strlen(s)-n;	/* size of remainder of source */
	end = *enda+*nmid;
	for(w = b; w; w = w->next){
		n = strlen(w->s);
		if(strcmp(w->s, end-n) == 0){
			*nmid -= n;
			break;
		}
	}
	if(b && w == 0)		/* b == NULL matches everything */
		return 0;
	return 1;
}
