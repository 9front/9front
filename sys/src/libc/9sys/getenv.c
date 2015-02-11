#include <u.h>
#include <libc.h>

char*
getenv(char *name)
{
	enum { HUNK = 100, };
	char *s, *p;
	int f, r, n;

	if(name[0]=='\0' || strcmp(name, ".")==0 || strcmp(name, "..")==0 || strchr(name, '/')!=nil
	|| strlen(name) >= HUNK-5){
		werrstr("bad env name: %s", name);
		return nil;
	}
	if((s = malloc(HUNK)) == nil)
		return nil;
	snprint(s, HUNK, "/env/%s", name);
	n = 0;
	r = -1;
	if((f = open(s, OREAD)) >= 0){
		while((r = read(f, s+n, HUNK)) > 0){
			n += r;
			r = -1;
			if((p = realloc(s, n+HUNK)) == nil)
				break;
			s = p;
		}
		close(f);
	}
	if(r < 0 || (p = realloc(s, n+1)) == nil){
		free(s);
		return nil;
	}
	s = p;
	setmalloctag(s, getcallerpc(&name));
	while(n > 0 && s[n-1] == '\0')
		n--;
	s[n] = '\0';
	while(--n >= 0){
		if(s[n] == '\0')
			s[n] = ' ';
	}
	return s;
}
