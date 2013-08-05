/*
 * google code wiki to html converter.
 * https://code.google.com/p/support/wiki/WikiSyntax
 */
#include <u.h>
#include <libc.h>

enum {
	HUNK = 8*1024,
};

char	*buf;
char	*pos;
char	*epos;
char	*opos;

int	inquote = 0;
int	intable = 0;
int	inlist = 0;
int	indent = -1;

void	body(void);

int
match(char *s)
{
	int n;

	n = strlen(s);
	if(pos+n > epos)
		return 0;
	return cistrncmp(pos, s, n) == 0;
}

int
got(char *s)
{
	if(!match(s))
		return 0;
	pos += strlen(s);
	return 1;
}

char*
look(char *s, char *e)
{
	char *p;
	int n;

	if(e == nil)
		e = epos;
	n = strlen(s);
	e -= n;
	for(p = pos; p <= e; p++)
		if(cistrncmp(p, s, n) == 0)
			return p;
	return nil;
}

void
eatspace(void)
{
	while(pos < epos && (*pos == ' ' || *pos == '\t'))
		pos++;
}

char*
trimback(char *s)
{
	while(s > pos && strchr("\t ", s[-1]) != nil)
		s--;
	return s;
}

void
flush(void)
{
	int n;

	n = opos - buf;
	if(n <= 0)
		return;
	if(write(1, buf, n) != n)
		sysfatal("write: %r");
	opos = buf;
}

void
output(char *s, int n)
{
	int r;

	if(n <= 0)
		return;
	r = HUNK - (opos - buf);
	if(n > r){
		output(s, r);
		output(s+r, n-r);
	} else {
		memmove(opos, s, n);
		opos += n;
		if(r == n)
			flush();
	}
}

void
string(char *s)
{
	output(s, strlen(s));
}

void
escape(char *e)
{
	char *p;

	for(p = pos; p < e; p++)
		if(*p == '<'){
			output(pos, p - pos);
			pos = p+1;
			string("&lt;");
		} else if(*p == '>'){
			output(pos, p - pos);
			pos = p+1;
			string("&gt;");
		} else if(*p == '&'){
			output(pos, p - pos);
			pos = p+1;
			string("&amp;");
		}
	output(pos, p - pos);
	pos = p;
}

void
ebody(char *e)
{
	char *t;

	t = epos;
	epos = trimback(e);
	body();
	pos = e;
	epos = t;
}

int
tag(char *term, char *tag)
{
	char *e;

	if(!got(term))
		return 0;
	if(e = look(term, nil)){
		eatspace();
		string("<"); string(tag); string(">");
		ebody(e);
		string("</"); string(tag); string(">");
		pos += strlen(term);
	} else
		string(term);
	return 1;
}

int
heading(void)
{
	char *o, *s, *e;
	int n;

	for(s = "======"; *s; s++)
		if(got(s))
			break;
	if(*s == 0)
		return 0;
	n = strlen(s);
	e = look("=", look("\n", nil));
	if(e == nil)
		e = look("\n", nil);
	if(e == nil)
		e = epos;
	eatspace();
	string("<h");
	output("0123456"+n, 1);
	string("><a name=\"");
	o = pos;
	s = trimback(e);
	while(pos < s){
		if((*pos >= 'a' && *pos <= 'z')
		|| (*pos >= 'A' && *pos <= 'Z')
		|| (*pos >= '0' && *pos <= '9')
		|| (strchr("!#$%()_+,-./{|}~:;=?@[\\]^_`", *pos) != 0))
			output(pos, 1);
		else if(*pos == ' ' || *pos == '\t')
			output("_", 1);
		else if(*pos == '<')
			string("&lt;");
		else if(*pos == '>')
			string("&gt;");
		else if(*pos == '&')
			string("&amp;");
		else if(*pos == '"')
			string("&quot;");
		else if(*pos == '\'')
			string("&#39;");
		pos++;
	}
	string("\"></a>");
	pos = o;
	ebody(e);
	while(got("="))
		;
	string("</h");
	output("0123456"+n, 1);
	string(">");
	return 1;
}

void
link(char *e)
{
	char *s, *o;

	s = o = pos;
	while(s < epos){
		if(e != nil && s >= e)
			break;
		if(*s == 0 || strchr("<>[] \t\r\n", *s) != nil)
			break;
		s++;
	}
	if(s-4 >= o)
	if(cistrncmp(s-4, ".png", 4)
	&& cistrncmp(s-4, ".jpg", 4)
	&& cistrncmp(s-4, ".gif", 4)){
		string("<a href=\"");
		escape(s);
		string("\">");
		eatspace();
		if(e != nil && pos < e)
			ebody(e);
		else {
			pos = o;
			escape(s);
		}
		string("</a>");
	} else {
		string("<img src=\"");
		escape(s);
		string("\">");
	}
}

void
body(void)
{
	char *s;
	int t;

Next:
	if(pos >= epos)
		return;

	if(got("\n") || got("\r\n"))
		indent = -1;
	if(got("\n") || got("\r\n")){
		string("<br>");
		while(got("\n") || got("\r\n"))
			;
	}

	if(indent == -1){
		indent = 0;
		for(;;){
			if(got(" "))
				indent++;
			else if(got("\t")){
				indent += 8;
				indent %= 8;
			}
			else break;
		}

		if(intable && look("||", look("\n", nil)) == nil){
			string("</table>");
			intable = 0;
		}

		string("\n");
		if((indent < inlist) || (indent < inquote))
			return;

		while(indent > 0){
			if(pos >= epos)
				return;
			if(got("*") || got("#")){
				s = pos-1;
				eatspace();
				if(indent > inlist){
					if(*s == '*')
						string("<ul><li>");
					else
						string("<ol><li>");
					t = inlist;
					inlist = indent;
					body();
					inlist = t;
					if(*s == '*')
						string("</li></ul>");
					else
						string("</li></ol>");
				} else {
					string("</li><li>");
					break;
				}
			} else if(indent > inquote){
				string("<blockquote>");
				t = inquote;
				inquote = indent;
				body();
				inquote = t;
				string("</blockquote>");
			} else
				break;
		}

		if(indent == 0){
			if(got("#")){
				if((pos = look("\n", nil)) == nil)
					pos = epos;
				goto Next;
			}
			if(heading())
				goto Next;
			if(got("----")){
				while(got("-"))
					;
				string("<hr>");
				goto Next;
			}
		}
	}

	if(got("`")){
		if(s = look("`", nil)){
			escape(s);
			pos = s+1;
		} else
			string("`");
	}
	else if(got("<")){
		string("<");
		if(s = look(">", nil)){
			s++;
			output(pos, s - pos);
			pos = s;
		}
	}
	else if(got("[")){
		if(s = look("]", nil)){
			link(s);
			pos = s+1;
		} else
			string("[");
	}
	else if(tag("*", "b") ||
		tag("_", "i") ||
		tag("^", "sup") ||
		tag(",,", "sub") ||
		tag("~~", "strike")){
	}
	else if(got("{{{")){
		if(s = look("}}}", nil)){
			if(look("\n", s)){
				string("<pre>");
				escape(s);
				string("</pre>");
			} else {
				string("<tt>");
				escape(s);
				string("</tt>");
			}
			pos = s+3;
		} else
			string("{{{");
	}
	else if(got("||")){
		if(s = look("||", look("\n", nil))){
			eatspace();
			switch(intable){
			case 0:	string("<table>");
				intable++;
			case 1:	string("<tr>");
				intable++;
			}
			string("<td>");
			ebody(s);
			string("</td>");
		} else if(intable){
			string("</tr>");
			intable = 1;
		}
	}
	else if(match("http://"))
		link(nil);
	else if(match("https://"))
		link(nil);
	else if(match("ftp://"))
		link(nil);
	else{
		output(pos, 1);
		pos++;
	}
	goto Next;
}

void
usage(void)
{
	fprint(2, "usage: %s [ file ]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int n;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	if(argc != 0 && argc != 1)
		usage();

	if(*argv){
		if((n = open(*argv, OREAD)) < 0)
			sysfatal("open %s: %r", *argv);
		if(dup(n, 0) < 0)
			sysfatal("dup: %r");
	}

	buf = opos = sbrk(HUNK);
	pos = epos = buf + HUNK;
	for(;;){
		if(brk(epos + HUNK + 8) < 0)
			sysfatal("brk: %r");
		if((n = read(0, epos, HUNK)) < 0)
			sysfatal("read: %r");
		if(n == 0)
			break;
		epos += n;
	}
	if(epos > pos && epos[-1] != '\n')
		*epos++ = '\n';

	body();
	flush();
	exits(0);
}
