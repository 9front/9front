#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "dat.h"
#include "fns.h"

char *whitespace = " \t\r\n";

void*
emalloc(int n)
{
	void *v;
	v = emalloc9p(n);
	setmalloctag(v, getcallerpc(&n));
	memset(v, 0, n);
	return v;
}

char*
estrdup(char *s)
{
	s = estrdup9p(s);
	setmalloctag(s, getcallerpc(&s));
	return s;
}

void
nstrcpy(char *to, char *from, int n)
{
	strncpy(to, from, n);
	to[n-1] = 0;
}

Key*
addkey(Key *h, char *key, char *val)
{
	Key *k;
	int n;

	if(val == nil)
		val = "";
	n = strlen(key)+1;
	k = emalloc(sizeof(*k) + n + strlen(val)+1);
	k->next = h;
	k->val = k->key + n;
	strcpy(k->key, key);
	strcpy(k->val, val);
	return k;
}

Key*
delkey(Key *h, char *key)
{
	Key *k, *p;

	for(p = nil, k = h; k; p = k, k = k->next){
		if(!cistrcmp(k->key, key)){
			if(p)
				p->next = k->next;
			else
				h = k->next;
			memset(k->val, 0, strlen(k->val));
			free(k);
			break;
		}
	}
	return h;
}

Key*
getkey(Key *k, char *key)
{
	while(k){
		if(!cistrcmp(k->key, key))
			break;
		k = k->next;
	}
	return k;
}

char*
lookkey(Key *k, char *key)
{
	if(k = getkey(k, key))
		return k->val;
	return nil;
}

Key*
parsehdr(char *s)
{
	char *v;

	if(*s == 0)
		return nil;
	v = strchr(s, 0)-1;
	while(v >= s && strchr(whitespace, *v))
		*v-- = 0;
	if(v = strchr(s, ':')){
		*v++ = 0;
		while(*v == ' ' || *v == '\t')
			v++;
		if(*s && *v)
			return addkey(0, s, v);
	}
	return nil;
}

char*
unquote(char *s, char **ps)
{
	char *p;

	if(*s != '"'){
		p = strpbrk(s, " \t\r\n");
		*p++ = 0;
		*ps = p;
		return s;
	}
	for(p=s+1; *p; p++){
		if(*p == '\"'){
			*p++ = 0;
			break;
		}
		if(*p == '\\' && *(p+1)){
			p++;
			continue;
		}
	}
	memmove(s, s+1, p-(s+1));
	s[p-(s+1)] = 0;
	*ps = p;
	return s;
}
