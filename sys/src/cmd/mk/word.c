#include	"mk.h"

static	Word	*nextword(char**);

Word*
newword(char *s)
{
	int n = strlen(s)+1;
	Word *w = (Word *)Malloc(sizeof(Word) + n);
	memcpy(w->s, s, n);
	w->next = 0;
	return w;
}

Word*
popword(Word *w)
{
	Word *x = w->next;
	free(w);
	return x;
}

Word *
stow(char *s)
{
	Word *w, *h, **l;

	h = 0;
	l = &h;
	while(*s && (*l = w = nextword(&s))){
		while(w->next)
			w = w->next;
		l = &w->next;
	}
	return h;
}

char *
wtos(Word *w)
{
	Bufblock *buf;
	char *s;

	buf = newbuf();
	bufcpyw(buf, w);
	insert(buf, 0);
	s = Strdup(buf->start);
	freebuf(buf);
	return s;
}

void
bufcpyw(Bufblock *buf, Word *w)
{
	for(; w; w = w->next){
		bufcpyq(buf, w->s);
		if(w->next)
			insert(buf, ' ');
	}
}

int
empty(Word *w)
{
	return w == 0 || w->s[0] == 0;
}

int
wadd(Word **l, char *s)
{
	Word *w;

	while(w = *l){
		if(strcmp(w->s, s) == 0)
			return 1;
		l = &w->next;
	}
	*l = newword(s);
	return 0;
}

int
wcmp(Word *a, Word *b)
{
	for(; a && b; a = a->next, b = b->next)
		if(strcmp(a->s, b->s))
			return 1;
	return(a || b);
}

Word*
wdup(Word *w)
{
	Word *h, **l;

	h = 0;
	l = &h;
	while(w){
		*l = newword(w->s);
		l = &(*l)->next;
		w = w->next;
	}
	return h;
}

void
delword(Word *w)
{
	while(w)
		w = popword(w);
}

/*
 *	break out a word from a string handling quotes, executions,
 *	and variable expansions.
 */
static Word*
nextword(char **s)
{
	Word *head, *tail, **link, *w, *t;
	Bufblock *b;
	int empty;
	char *cp;
	Rune r;

	cp = *s;
	b = newbuf();
	empty = 1;
	head = tail = 0;
	link = &head;
restart:
	while(*cp == ' ' || *cp == '\t')		/* leading white space */
		cp++;
	while(*cp){
		cp += chartorune(&r, cp);
		switch(r)
		{
		case ' ':
		case '\t':
		case '\n':
			goto out;
		case '\\':
		case '\'':
		case '"':
			cp = expandquote(cp, r, b);
			if(cp == 0){
				fprint(2, "missing closing quote: %s\n", *s);
				Exit();
			}
			empty = 0;
			break;
		case '$':
			w = varsub(&cp);
			if(w == 0){
				if(empty)
					goto restart;
				break;
			}
			empty = 0;
			if(b->current != b->start){
				bufcpy(b, w->s);
				insert(b, 0);
				t = popword(w);
				w = newword(b->start);
				w->next = t;
				b->current = b->start;
			}
			if(tail){
				bufcpy(b, tail->s);
				bufcpy(b, w->s);
				insert(b, 0);
				delword(tail);
				*link = tail = newword(b->start);
				tail->next = popword(w);
				b->current = b->start;
			} else
				*link = tail = w;
			while(tail->next){
				link = &tail->next;
				tail = tail->next;
			}
			break;
		default:
			rinsert(b, r);
			break;
		}
	}
out:
	*s = cp;
	if(b->current != b->start || !empty){
		insert(b, 0);
		if(tail){
			cp = Strdup(b->start);
			b->current = b->start;
			bufcpy(b, tail->s);
			bufcpy(b, cp);
			free(cp);
			insert(b, 0);
			delword(tail);
			*link = newword(b->start);
		} else
			*link = newword(b->start);
	}
	freebuf(b);
	return head;
}

void
dumpw(char *s, Word *w)
{
	Bprint(&bout, "%s", s);
	for(; w; w = w->next)
		Bprint(&bout, " '%s'", w->s);
	Bputc(&bout, '\n');
}
