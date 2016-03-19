#include <u.h>
#include <libc.h>
#include <draw.h>

static char*
readfile(char *name)
{
	enum { HUNK = 8*1024, };
	int f, n, r;
	char *s, *p;

	n = 0;
	r = -1;
	if((s = malloc(HUNK)) != nil){
		if((f = open(name, OREAD)) >= 0){
			while((r = read(f, s+n, HUNK)) > 0){
				n += r;
				r = -1;
				if((p = realloc(s, n+HUNK)) == nil)
					break;
				s = p;
			}
			close(f);
		}
	}
	if(r < 0 || (p = realloc(s, n+1)) == nil){
		free(s);
		return nil;
	}
	p[n] = 0;
	return p;
}

Font*
openfont(Display *d, char *name)
{
	Font *fnt;
	char *buf;

	if((buf = readfile(name)) == nil){
		werrstr("openfont: %r");
		return nil;
	}
	fnt = buildfont(d, buf, name);
	free(buf);
	return fnt;
}
