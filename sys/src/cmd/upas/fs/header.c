#include "common.h"
#include <ctype.h>
#include <libsec.h>
#include "dat.h"

int
hdrlen(char *p, char *e)
{
	char *ep;

	ep = p;
	do {
		ep = strchr(ep, '\n');
		if(ep == nil){
			ep = e;
			break;
		}
		if(ep == p)
			break;
		if(ep - p == 1 && ep[-1] == '\r')
			break;
		ep++;
		if(ep >= e){
			ep = e;
			break;
		}
	} while(*ep == ' ' || *ep == '\t');
	return ep - p;
}

/* rfc2047 non-ascii: =?charset?q?encoded-text?= */
static int
tok(char **sp, char *se, char *token, int len)
{
	char charset[100], *s, *e, *x;
	int l;

	if(len == 0)
		return -1;
	s = *sp;
	e = token + len - 2;
	token += 2;

	x = memchr(token, '?', e - token);
	if(x == nil || (l = x - token) >= sizeof charset)
		return -1;
	memmove(charset, token, l);
	charset[l] = 0;

	/* bail if it doesn't fit */
	token = x + 1;
	if(e - token > se - s - 1)
		return -1;

	if(cistrncmp(token, "b?", 2) == 0){
		token += 2;
		len = dec64((uchar*)s, se - s - 1, token, e - token);
		if(len == -1)
			return -1;
		s[len] = 0;
	}else if(cistrncmp(token, "q?", 2) == 0){
		token += 2;
		len = decquoted(s, token, e, 1);
		if(len > 0 && s[len - 1] == '\n')
			len--;
		s[len] = 0;
	}else
		return -1;

	if(xtoutf(charset, &x, s, s + len) <= 0)
		s += len;
	else {
		s = seprint(s, se, "%s", x);
		free(x);
	}
	*sp = s;
	return 0;
}

char*
tokbegin(char *start, char *end)
{
	int quests;

	if(*--end != '=')
		return nil;
	if(*--end != '?')
		return nil;

	quests = 0;
	for(end--; end >= start; end--){
		switch(*end){
		case '=':
			if(quests == 3 && *(end + 1) == '?')
				return end;
			break;
		case '?':
			++quests;
			break;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			/* can't have white space in a token */
			return nil;
		}
	}
	return nil;
}

static char*
seappend822f(char *s, char *e, char *a, int n)
{
	int skip, c;

	skip = 0;
	for(; n--; a++){
		c = *a;
		if(skip && isspace(c))
			continue;
		if(c == '\n'){
			c = ' ';
			skip = 1;
		}else{
			if(c < 0x20)
				continue;
			skip = 0;
		}
		s = sputc(s, e, c);
	}
	return s;
}

static char*
seappend822(char *s, char *e, char *a, int n)
{
	int c;

	for(; n--; a++){
		c = *a;
		if(c < 0x20 && c != '\n' && c != '\t')
			continue;
		s = sputc(s, e, c);
	}
	return s;
}

/* convert a header line */
char*
rfc2047(char *s, char *se, char *uneaten, int len, int fold)
{
	char *sp, *token, *p, *e;
	char *(*f)(char*, char*, char*, int);

	f = seappend822;
	if(fold)
		f = seappend822f;
	sp = s;
	p = uneaten;
	for(e = p + len; p < e; ){
		while(*p++ == '=' && (token = tokbegin(uneaten, p))){
			sp = f(sp, se, uneaten, token - uneaten);
			if(tok(&sp, se, token, p - token) < 0)
				sp = f(sp, se, token, p - token);
			uneaten = p;
			for(; p < e && isspace(*p);)
				p++;
			if(p + 2 < e && p[0] == '=' && p[1] == '?')
				uneaten = p;	/* paste */
		}
	}
	if(p > uneaten)
		sp = f(sp, se, uneaten, e - uneaten);
	*sp = 0;
	return sp;
}
