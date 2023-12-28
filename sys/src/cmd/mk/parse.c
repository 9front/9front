#include	"mk.h"

char *mkinfile;
int mkinline;

static void ipush(char *file);
static void ipop(void);
static void doassign(Word *, Word *, int, int);
static int rhead(char *, Word **, Word **, int *, char **);
static char *rbody(Biobuf*);
extern Word *target1;

void
parse(char *f, int fd)
{
	int hline, attr, pid, newfd;
	Word *head, *tail;
	char *prog, *p;
	Bufblock *buf;
	Biobuf in;

	if(fd < 0){
		perror(f);
		Exit();
	}
	Binit(&in, fd, OREAD);
	ipush(f);
	buf = newbuf();
	while(assline(&in, buf)){
		hline = mkinline;
		switch(rhead(buf->start, &head, &tail, &attr, &prog))
		{
		case '<':
			p = wtos(tail);
			if(*p == 0){
				SYNERR(-1);
				fprint(2, "missing include file name\n");
				Exit();
			}
			newfd = open(p, OREAD|OCEXEC);
			if(newfd < 0){
				fprint(2, "warning: skipping missing include file: ");
				perror(p);
			} else
				parse(p, newfd);
			free(p);
			break;
		case '|':
			p = wtos(tail);
			if(*p == 0){
				SYNERR(-1);
				fprint(2, "missing include program name\n");
				Exit();
			}
			pid=pipecmd(p, 0, execinit(), &newfd);
			if(newfd < 0){
				fprint(2, "warning: skipping missing program file: ");
				perror(p);
			} else
				parse(p, newfd);
			while(waitup(-3, &pid) >= 0)
				;
			if(pid != 0){
				fprint(2, "bad include program status\n");
				Exit();
			}
			free(p);
			break;
		case ':':
			addrules(head, tail, rbody(&in), attr, hline, prog);
			continue;	/* don't free head and tail */
		case '=':
			doassign(head, tail, attr, 0);
			continue;
		default:
			SYNERR(hline);
			fprint(2, "expected one of :<=\n");
			Exit();
			break;
		}
		delword(head);
		delword(tail);
	}
	freebuf(buf);
	ipop();
	close(fd);
}

static void
doassign(Word *head, Word *tail, int attr, int override)
{
	int set;

	if(head->next){
		SYNERR(-1);
		fprint(2, "multiple vars on left side of assignment\n");
		Exit();
	}
	if(symlook(head->s, S_OVERRIDE, 0)){
		set = override;
	} else {
		set = 1;
		if(override)
			symlook(head->s, S_OVERRIDE, 1);
	}
	if(set){
		setvar(head->s, tail);
		symlook(head->s, S_WESET, 1);
		tail = 0;	/* don't free */
	}
	if(attr)
		symlook(head->s, S_NOEXPORT, 1);
	delword(head);
	delword(tail);
}

void
varoverride(char *line)
{
	Word *head, *tail;
	char *dummy;
	int attr;
	
	head = tail = 0;
	if(rhead(line, &head, &tail, &attr, &dummy) == '='){
		doassign(head, tail, attr, 1);
		return;
	}
	delword(head);
	delword(tail);
}

void
addrules(Word *head, Word *tail, char *body, int attr, int hline, char *prog)
{
	Word *w;

	assert(/*addrules args*/ head && body);
		/* tuck away first non-meta rule as default target*/
	if(target1 == 0 && !(attr&REGEXP)){
		for(w = head; w; w = w->next)
			if(charin(w->s, "%&"))
				break;
		if(w == 0)
			target1 = wdup(head);
	}
	for(w = head; w; w = w->next)
		addrule(w->s, tail, body, head, attr, hline, prog);
}

static int
rhead(char *line, Word **h, Word **t, int *attr, char **prog)
{
	char *p;
	char *pp;
	int sep;
	Rune r;
	int n;
	Word *w;

	*h = *t = 0;
	*attr = 0;
	*prog = 0;

	p = charin(line,":=<");
	if(p == 0)
		return('?');
	sep = *p;
	*p++ = 0;
	if(sep == '<' && *p == '|'){
		sep = '|';
		p++;
	}
	if(sep == '='){
		pp = charin(p, termchars);	/* termchars is shell-dependent */
		if (pp && *pp == '=') {
			while (p != pp) {
				n = chartorune(&r, p);
				switch(r)
				{
				default:
					SYNERR(-1);
					fprint(2, "unknown attribute '%c'\n",*p);
					Exit();
				case 'U':
					*attr = 1;
					break;
				}
				p += n;
			}
			p++;		/* skip trailing '=' */
		}
	}
	if((sep == ':') && *p && (*p != ' ') && (*p != '\t')){
		while (*p) {
			n = chartorune(&r, p);
			if (r == ':')
				break;
			p += n;
			switch(r)
			{
			default:
				SYNERR(-1);
				fprint(2, "unknown attribute '%c'\n", p[-1]);
				Exit();
			case 'D':
				*attr |= DEL;
				break;
			case 'E':
				*attr |= NOMINUSE;
				break;
			case 'n':
				*attr |= NOVIRT;
				break;
			case 'N':
				*attr |= NOREC;
				break;
			case 'P':
				pp = utfrune(p, ':');
				if (pp == 0 || *pp == 0)
					goto eos;
				*pp = 0;
				*prog = Strdup(p);
				*pp = ':';
				p = pp;
				break;
			case 'Q':
				*attr |= QUIET;
				break;
			case 'R':
				*attr |= REGEXP;
				break;
			case 'U':
				*attr |= UPD;
				break;
			case 'V':
				*attr |= VIR;
				break;
			}
		}
		if (*p++ != ':') {
	eos:
			SYNERR(-1);
			fprint(2, "missing trailing :\n");
			Exit();
		}
	}
	*h = w = stow(line);
	if(empty(w) && sep != '<' && sep != '|') {
		SYNERR(mkinline-1);
		fprint(2, "no var on left side of assignment/rule\n");
		Exit();
	}
	*t = stow(p);
	return(sep);
}

static char *
rbody(Biobuf *in)
{
	Bufblock *buf;
	int r, lastr;
	char *p;

	lastr = '\n';
	buf = newbuf();
	for(;;){
		r = Bgetrune(in);
		if (r < 0)
			break;
		if (lastr == '\n') {
			if (r == '#')
				rinsert(buf, r);
			else if (r != ' ' && r != '\t') {
				Bungetrune(in);
				break;
			}
		} else
			rinsert(buf, r);
		lastr = r;
		if (r == '\n')
			mkinline++;
	}
	insert(buf, 0);
	p = Strdup(buf->start);
	freebuf(buf);
	return p;
}

struct input
{
	char *file;
	int line;
	struct input *next;
};
static struct input *inputs = 0;

static void
ipush(char *file)
{
	struct input *i;

	i = (struct input *)Malloc(sizeof(*i));
	i->file = mkinfile;
	i->line = mkinline;
	i->next = inputs;
	inputs = i;

	mkinfile = Strdup(file);
	mkinline = 1;
}

static void
ipop(void)
{
	struct input *i;

	i = inputs;
	inputs = i->next;
	mkinfile = i->file;
	mkinline = i->line;
	free(i);
}
