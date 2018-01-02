#include <u.h>
#include <libc.h>
#include <auth.h>

int
_attrfmt(Fmt *fmt)
{
	Attr *a;
	int first = 1;

	for(a=va_arg(fmt->args, Attr*); a != nil; a=a->next){
		if(a->name == nil)
			continue;
		switch(a->type){
		default:
			continue;
		case AttrQuery:
			fmtprint(fmt, first+" %q?", a->name);
			break;
		case AttrNameval:
		case AttrDefault:
			fmtprint(fmt, first+" %q=%q", a->name, a->val);
			break;
		}
		first = 0;
	}
	return 0;
}

Attr*
_copyattr(Attr *a)
{
	Attr **la, *na;

	na = nil;
	la = &na;
	for(; a; a=a->next){
		*la = _mkattr(a->type, a->name, a->val, nil);
		setmalloctag(*la, getcallerpc(&a));
		la = &(*la)->next;
	}
	*la = nil;
	return na;
}

Attr*
_delattr(Attr *a, char *name)
{
	Attr *fa;
	Attr **la;

	for(la=&a; *la; ){
		if(strcmp((*la)->name, name) == 0){
			fa = *la;
			*la = (*la)->next;
			fa->next = nil;
			_freeattr(fa);
		}else
			la=&(*la)->next;
	}
	return a;
}

Attr*
_findattr(Attr *a, char *n)
{
	for(; a; a=a->next)
		if(strcmp(a->name, n) == 0 && a->type != AttrQuery)
			return a;
	return nil;
}

void
_freeattr(Attr *a)
{
	Attr *anext;

	for(; a; a=anext){
		anext = a->next;
		free(a->name);
		free(a->val);
		a->name = (void*)~0;
		a->val = (void*)~0;
		a->next = (void*)~0;
		free(a);
	}
}

Attr*
_mkattr(int type, char *name, char *val, Attr *next)
{
	Attr *a;

	a = malloc(sizeof(*a));
	if(a==nil)
		sysfatal("_mkattr malloc: %r");
	a->type = type;
	a->name = strdup(name);
	a->val = strdup(val);
	if(a->name==nil || a->val==nil)
		sysfatal("_mkattr malloc: %r");
	a->next = next;
	setmalloctag(a, getcallerpc(&type));
	return a;
}

static Attr*
cleanattr(Attr *a)
{
	Attr *fa;
	Attr **la;

	for(la=&a; *la; ){
		if((*la)->type==AttrQuery && _findattr(a, (*la)->name)){
			fa = *la;
			*la = (*la)->next;
			fa->next = nil;
			_freeattr(fa);
		}else
			la=&(*la)->next;
	}
	return a;
}

Attr*
_parseattr(char *s)
{
	char *p, *t, *tok[256];
	int i, ntok;
	Attr *a;

	s = strdup(s);
	if(s == nil)
		sysfatal("_parseattr strdup: %r");

	ntok = tokenize(s, tok, nelem(tok));
	a = nil;
	for(i=ntok-1; i>=0; i--){
		t = tok[i];
		if((p = strchr(t, '=')) != nil){
			*p++ = '\0';
			a = _mkattr(AttrNameval, t, p, a);
		}else if((p = strchr(t, '\0')-1) >= t && *p == '?'){
			*p = '\0';
			a = _mkattr(AttrQuery, t, "", a);
		}else{
			/* really a syntax error, but better to provide some indication */
			a = _mkattr(AttrNameval, t, "", a);
		}
		setmalloctag(a, getcallerpc(&s));
	}
	free(s);
	return cleanattr(a);
}

char*
_strfindattr(Attr *a, char *n)
{
	a = _findattr(a, n);
	if(a == nil)
		return nil;
	return a->val;
}

