#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "mothra.h"
/*
 * fd is the result of a successful open(name, OREAD),
 * where name is the name of a directory.  We convert
 * this into an html page containing links to the files
 * in the directory.
 */
int dir2html(char *name, int fd){
	int p[2], first;
	Dir *dir;
	int i, n;
	if(pipe(p)==-1){
		close(fd);
		return -1;
	}
	switch(rfork(RFFDG|RFPROC|RFNOWAIT)){
	case -1:
		close(fd);
		return -1;
	case 0:
		close(p[1]);
		fprint(p[0], "<head>\n");
		fprint(p[0], "<title>Directory %s</title>\n", name);
		fprint(p[0], "</head>\n");
		fprint(p[0], "<body>\n");
		fprint(p[0], "<h1>%s</h1>\n", name);
		fprint(p[0], "<ul>\n");
		first=1;
		while((n = dirread(fd, &dir)) > 0) {
		  for (i = 0; i < n; i++)
			fprint(p[0], "<li><a href=\"%s/%s\">%s%s</a>\n", name, dir[i].name, dir[i].name,
				dir[i].mode&DMDIR?"/":"");
		  free(dir);
		}
		fprint(p[0], "</ul>\n");
		fprint(p[0], "</body>\n");
		_exits(0);
	default:
		close(fd);
		close(p[0]);
		return p[1];
	}
}
