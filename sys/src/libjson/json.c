#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <json.h>

typedef struct Lex Lex;

enum {
	TEOF,
	TSTRING = Runemax+1,
	TNUM,
	TNULL,
	TFALSE,
	TTRUE,
};

struct Lex
{
	char *s;
	ulong slen;
	int t;
	double n;
	char *buf;
	Rune peeked;
};

static Rune
getch(Lex *l)
{
	Rune r;

	if(l->peeked){
		r = l->peeked;
		l->peeked = 0;
		return r;
	}
	if(l->s[0] == '\0')
		return 0;
	l->s += chartorune(&r, l->s);
	return r;
}

static Rune
peekch(Lex *l)
{
	if(!l->peeked)
		l->peeked = getch(l);
	return l->peeked;
}

static Rune
peeknonspace(Lex *l)
{
	Rune r;

	for(;;){
		r = peekch(l);
		if(r != 0x20 && r != 0x09 && r != 0x0A && r != 0x0D)
			break;
		getch(l);
	}
	return r;
}

static int
fixsurrogate(Rune *rp, Rune r2)
{
	Rune r1;

	r1 = *rp;
	if(r1 >= 0xD800 && r1 <= 0xDBFF){
		if(r2 >= 0xDC00 && r2 <= 0xDFFF){
			*rp = 0x10000 + (((r1 - 0xD800)<<10) | (r2 - 0xDC00));
			return 0;
		}
		return 1;
	} else
	if(r1 >= 0xDC00 && r1 <= 0xDFFF){
		if(r2 >= 0xD800 && r2 <= 0xDBFF){
			*rp = 0x10000 + (((r2 - 0xD800)<<10) | (r1 - 0xDC00));
			return 0;
		}
		return 1;
	}
	return 0;
}

static int
lex(Lex *l)
{
	Rune r, r2;
	char *t;
	int i;
	char c;

	peeknonspace(l);
	r = getch(l);
	if(r == 0 || r == '{' || r == '[' || r == ']' || r == '}' || r == ':' || r == ','){
		l->t = r;
		return 0;
	}
	if(r >= 0x80 || isalpha(r)){
		t = l->buf;
		for(;;){
			t += runetochar(t, &r);
			if(t >= l->buf + l->slen){
				werrstr("json: literal too long");
				return -1;
			}
			r = peekch(l);
			if(r < 0x80 && !isalpha(r))
				break;
			getch(l);
		}
		*t = 0;
		if(strcmp(l->buf, "true") == 0)
			l->t = TTRUE;
		else if(strcmp(l->buf, "false") == 0)
			l->t = TFALSE;
		else if(strcmp(l->buf, "null") == 0)
			l->t = TNULL;
		else{
			werrstr("json: invalid literal");
			return -1;
		}
		return 0;
	}
	if(isdigit(r) || r == '-'){
		l->n = strtod(l->s-1, &l->s);
		l->t = TNUM;
		return 0;
	}
	if(r == '"'){
		r2 = 0;
		t = l->buf;
		for(;;){
			r = getch(l);
			if(r == '"')
				break;
			if(r < ' '){
				werrstr("json: invalid char in string %x", r);
				return -1;
			}
			if(r == '\\'){
				r = getch(l);
				switch(r){
				case 'n':
					r = '\n';
					break;
				case 'r':
					r = '\r';
					break;
				case 'u':
					r = 0;
					for(i = 0; i < 4; i++){
						if(!isxdigit(peekch(l)))
							break;

						c = getch(l);
						r *= 16;
						if(c >= '0' && c <= '9')
							r += c - '0';
						else if(c >= 'a' && c <= 'f')
							r += c - 'a' + 10;
						else if(c >= 'A' && c <= 'F')
							r += c - 'A' + 10;
					}
					if(fixsurrogate(&r, r2)){
						r2 = r;
						continue;
					}
					break;
				case 't':
					r = '\t';
					break;
				case 'f':
					r = '\f';
					break;
				case 'b':
					r = '\b';
					break;
				case '"': case '/': case '\\':
					break;
				default:
					werrstr("json: invalid escape sequence \\%C", r);
					return -1;
				}
			}
			r2 = 0;
			t += runetochar(t, &r);
			if(t >= l->buf + l->slen){
				werrstr("json: string too long");
				return -1;
			}
		}
		*t = 0;
		l->t = TSTRING;
		return 0;
	}
	werrstr("json: invalid char %C", peekch(l));
	return -1;
}

static JSON*
jsonobj(Lex *l)
{
	JSON *j;
	JSONEl *e;
	JSONEl **ln;
	int obj;

	if((j = mallocz(sizeof(*j), 1)) == nil)
		return nil;

	if(lex(l) < 0){
error:
		free(j);
		return nil;
	}
	switch(l->t){
	case TEOF:
		werrstr("json: unexpected eof");
		goto error;
	case TNULL:
		j->t = JSONNull;
		break;
	case TTRUE:
		j->t = JSONBool;
		j->n = 1;
		break;
	case TFALSE:
		j->t = JSONBool;
		j->n = 0;
		break;
	case TSTRING:
		j->t = JSONString;
		if((j->s = strdup(l->buf)) == nil)
			goto error;
		break;
	case TNUM:
		j->t = JSONNumber;
		j->n = l->n;
		break;
	case '{':
	case '[':
		obj = l->t == '{';
		ln = &j->first;
		if(obj){
			j->t = JSONObject;
			if(lex(l) < 0)
				goto abort;
			if(l->t == '}')
				return j;
			goto firstobj;
		}else{
			j->t = JSONArray;
			if(peeknonspace(l) == ']'){
				getch(l);
				return j;
			}
		}
		for(;;){
			if(obj){
				if(lex(l) < 0)
					goto abort;
			firstobj:
				if(l->t != TSTRING){
					werrstr("json: syntax error, not string");
					goto abort;
				}
				if((e = mallocz(sizeof(*e), 1)) == nil)
					goto abort;
				e->name = strdup(l->buf);
				if(e->name == nil || lex(l) < 0){
					free(e);
					goto abort;
				}
				if(l->t != ':'){
					werrstr("json: syntax error, not colon");
					free(e);
					goto abort;
				}
			}else{
				if((e = mallocz(sizeof(*e), 1)) == nil)
					goto abort;
			}
			e->val = jsonobj(l);
			if(e->val == nil){
				free(e);
				goto abort;
			}
			*ln = e;
			ln = &e->next;
			if(lex(l) < 0)
				goto abort;
			if(l->t == (obj ? '}' : ']'))
				break;
			if(l->t != ','){
				werrstr("json: syntax error, neither comma nor ending paren");
				goto abort;
			}
		}
		break;
	abort:
		jsonfree(j);
		return nil;
	case ']': case '}': case ',': case ':':
		werrstr("json: unexpected %C", l->t);
		goto error;
	default:
		werrstr("json: the front fell off");
		goto error;
	}
	return j;
}

JSON*
jsonparse(char *s)
{
	JSON *j;
	Lex l;

	memset(&l, 0, sizeof(l));
	l.s = s;
	l.slen = strlen(s);
	if((l.buf = mallocz(l.slen+UTFmax+1, 1)) == nil)
		return nil;

	j = jsonobj(&l);
	free(l.buf);

	if(peeknonspace(&l) != 0){
		jsonfree(j);
		werrstr("json: unexpected trailing data");
		return nil;
	}

	return j;
}

void
jsonfree(JSON *j)
{
	JSONEl *e, *f;

	if(j == nil)
		return;
	switch(j->t){
	case JSONString:
		if(j->s)
			free(j->s);
		break;
	case JSONArray: case JSONObject:
		for(e = j->first; e != nil; e = f){
			if(e->name)
				free(e->name);
			jsonfree(e->val);
			f = e->next;
			free(e);
		}
	}
	free(j);
}

JSON *
jsonbyname(JSON *j, char *n)
{
	JSONEl *e;
	
	if(j->t != JSONObject){
		werrstr("not an object");
		return nil;
	}
	for(e = j->first; e != nil; e = e->next)
		if(strcmp(e->name, n) == 0)
			return e->val;
	werrstr("key '%s' not found", n);
	return nil;
}

char *
jsonstr(JSON *j)
{
	if(j == nil)
		return nil;
	if(j->t != JSONString){
		werrstr("not a string");
		return nil;
	}
	return j->s;
}
