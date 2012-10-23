#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>

/*
 * block up paragraphs, possibly with indentation
 */

int extraindent = 0;		/* how many spaces to indent all lines */
int indent = 0;			/* current value of indent, before extra indent */
int length = 70;		/* how many columns per output line */
int join = 1;			/* can lines be joined? */
int maxtab = 8;

Biobuf bin;
Biobuf bout;

typedef struct Word Word;
struct Word
{
	Word	*next;

	int	indent;
	int	length;
	char	bol;
	char	text[];
};

void	fmt(void);

void
usage(void)
{
	fprint(2, "usage: %s [-j] [-i indent] [-l length] [file...]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, f;
	char *s, *err;

	ARGBEGIN{
	case 'i':
		extraindent = atoi(EARGF(usage()));
		break;
	case 'j':
		join = 0;
		break;
	case 'w':
	case 'l':
		length = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND

	if(length <= indent){
		fprint(2, "%s: line length<=indentation\n", argv0);
		exits("length");
	}

	s=getenv("tabstop");
	if(s!=nil && atoi(s)>0)
		maxtab=atoi(s);
	err = nil;
	Binit(&bout, 1, OWRITE);
	if(argc <= 0){
		Binit(&bin, 0, OREAD);
		fmt();
	}else{
		for(i=0; i<argc; i++){
			f = open(argv[i], OREAD);
			if(f < 0){
				fprint(2, "%s: can't open %s: %r\n", argv0, argv[i]);
				err = "open";
			}else{
				Binit(&bin, f, OREAD);
				fmt();
				Bterm(&bin);
				if(i != argc-1)
					Bputc(&bout, '\n');
			}
		}
	}
	exits(err);
}

int
indentof(char *s)
{
	int ind;

	ind = 0;
	for(; *s != '\0'; s++)
		switch(*s){
		default:
			return ind;
		case ' ':
			ind++;
			break;
		case '\t':
			ind += maxtab;
			ind -= ind%maxtab;
			break;
		}

	/* plain white space doesn't change the indent */
	return indent;
}

Word*
newword(char *s, int n, int ind, int bol)
{
	Word *w;

	w = malloc(sizeof(Word) + n+1);
	w->next = nil;
	w->indent = ind;
	w->bol = bol;
	memmove(w->text, s, n);
	w->text[n] = 0;
	w->length = utflen(w->text);
	return w;
}

Word*
getword(void)
{
	static Word *head, *tail;
	char *line, *s;
	Word *w;
	
	w = head;
	if(w != nil){
		head = w->next;
		return w;
	}
	line = Brdstr(&bin, '\n', 1);
	if(line == nil)
		return nil;
	tail = nil;
	indent = indentof(line);
	for(;;){
		while(*line == ' ' || *line == '\t')
			line++;
		if(*line == '\0'){
			if(head == nil)
				return newword("", 0, -1, 1);
			break;
		}
		/* how long is this word? */
		for(s=line++; *line != '\0'; line++)
			if(*line==' ' || *line=='\t')
				break;
		w = newword(s, line-s, indent, head==nil);
		if(head == nil)
			head = w;
		else
			tail->next = w;
		tail = w;
	}
	w = head;
	head = w->next;
	return w;
}

void
printindent(int w)
{
	while(w >= maxtab){
		Bputc(&bout, '\t');
		w -= maxtab;
	}
	while(w > 0){
		Bputc(&bout, ' ');
		w--;
	}
}

/* give extra space if word ends with period, etc. */
int
nspaceafter(char *s)
{
	int n;

	n = strlen(s);
	if(n < 2)
		return 1;
	if(isupper(s[0]) && n < 4)
		return 1;
	if(strchr(".!?", s[n-1]) != nil)
		return 2;
	return 1;
}

void
fmt(void)
{
	Word *w, *o;
	int col, nsp;

	w = getword();
	while(w != nil){
		if(w->indent == -1){
			Bputc(&bout, '\n');
			free(w);
			w = getword();
			if(w == nil)
				break;
		}
		col = w->indent;
		printindent(extraindent+col);
		/* emit words until overflow; always emit at least one word */
		for(;;){
			Bprint(&bout, "%s", w->text);
			col += w->length;
			o = w;
			w = getword();
			if(w == nil)
				break;
			if(w->indent != o->indent)
				break;	/* indent change */
			nsp = nspaceafter(o->text);
			if(col+nsp+w->length > length)
				break;	/* fold line */
			if(!join && w->bol)
				break;
			while(--nsp >= 0){
				Bputc(&bout, ' ');	/* emit space; another word will follow */
				col++;
			}
			free(o);
		}
		free(o);
		Bputc(&bout, '\n');
	}
}
