#include <u.h>
#include <libc.h>
#include <bio.h>
#include "plist.h"
#include "icy.h"

int
icyfill(Meta *m)
{
	char *s, *s0, *e, *p, *path, *d;
	int f, n;

	path = strdup(m->path);
	s = strchr(path, ':')+3;
	if((e = strchr(s, '/')) != nil)
		*e++ = 0;
	if((p = strchr(s, ':')) != nil)
		*p = '!';
	p = smprint("tcp!%s", s);
	free(path);
	f = -1;
	if((d = netmkaddr(p, "tcp", "80")) != nil)
		f = dial(d, nil, nil, nil);
	free(p);
	if(f < 0)
		return -1;
	fprint(f, "GET /%s HTTP/0.9\r\nIcy-MetaData: 1\r\n\r\n", e ? e : "");
	s0 = malloc(4096);
	if((n = readn(f, s0, 4095)) > 0){
		s0[n] = 0;
		for(s = s0; s = strchr(s, '\n');){
			s++;
			if(strncmp(s, "icy-name:", 9) == 0 && (e = strchr(s, '\r')) != nil){
				*e = 0;
				m->artist[0] = strdup(s+9);
				m->numartist = 1;
				s = e+1;
			}else if(strncmp(s, "icy-url:", 8) == 0 && (e = strchr(s, '\r')) != nil){
				*e = 0;
				m->title = strdup(s+8);
				s = e+1;
			}
		}
	}
	free(s0);
	close(f);

	return n > 0 ? 0 : -1;
}
