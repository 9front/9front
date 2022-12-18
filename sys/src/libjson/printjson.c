#include <u.h>
#include <libc.h>
#include <json.h>

static int printjson(Fmt*, JSON*, int);
static int printarray(Fmt*, JSON*, int);
static int printobject(Fmt*, JSON*, int);

static int
printstring(Fmt *f, char *s)
{
	Rune r;
	int n;

	n = fmtstrcpy(f, "\"");
	while(*s){
		s += chartorune(&r, s);
		switch(r){
		case '\\':	n += fmtstrcpy(f, "\\\\");	break;
		case '\f':	n += fmtstrcpy(f, "\\f");	break;
		case '\b':	n += fmtstrcpy(f, "\\b");	break;
		case '\n':	n += fmtstrcpy(f, "\\n");	break;
		case '\r':	n += fmtstrcpy(f, "\\r");	break;
		case '\"':	n += fmtstrcpy(f, "\\\"");	break;
		default:
			if(r < 0x20)
				n += fmtprint(f, "\\u%04x", r);
			else
				n += fmtrune(f, r);
		}
	}
	n += fmtstrcpy(f, "\"");
	return n;
}

static int
printarray(Fmt *f, JSON *j, int indent)
{
	JSONEl *jl;
	int i, r;

	if(j->first == nil){
		return fmtprint(f, "[]");
	}
	r = fmtprint(f, "[\n");
	for(jl = j->first; jl != nil; jl = jl->next){
		for(i = 0; i < indent; i++)
			r += fmtprint(f, "\t");
		r += printjson(f, jl->val, indent);
		r += fmtprint(f, "%s\n", jl->next != nil ? "," : "");
	}
	for(i = 0; i < indent-1; i++)
		r += fmtprint(f, "\t");
	r += fmtprint(f, "]");
	return r;
}

static int
printobject(Fmt *f, JSON *j, int indent)
{
	JSONEl *jl;
	int i, r;

	if(j->first == nil){
		return fmtprint(f, "{}");
	}
	r = fmtprint(f, "{\n");
	for(jl = j->first; jl != nil; jl = jl->next){
		for(i = 0; i < indent; i++)
			fmtprint(f, "\t");
		r += fmtprint(f, "\"%s\": ", jl->name);
		r += printjson(f, jl->val, indent);
		r += fmtprint(f, "%s\n", jl->next != nil ? "," : "");
	}
	for(i = 0; i < indent-1; i++)
		r += fmtprint(f, "\t");
	r += fmtprint(f, "}");
	return r;
}

static int
printjson(Fmt *f, JSON *j, int indent)
{
	switch(j->t){
	case JSONNull:
		return fmtprint(f, "null");
		break;
	case JSONBool:
		return fmtprint(f, "%s", j->n ? "true" : "false");
		break;
	case JSONNumber:
		return fmtprint(f, "%f", j->n);
		break;
	case JSONString:
		return printstring(f, j->s);
		break;
	case JSONArray:
		return printarray(f, j, indent+1);
		break;
	case JSONObject:
		return printobject(f, j, indent+1);
		break;
	}
	return 0;
}

int
JSONfmt(Fmt *f)
{
	JSON *j;

	j = va_arg(f->args, JSON*);
	return printjson(f, j, 0);
}

void
JSONfmtinstall(void)
{
	fmtinstall('J', JSONfmt);
}
