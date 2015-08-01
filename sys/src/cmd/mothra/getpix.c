#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "mothra.h"

typedef struct Pix Pix;
struct Pix{
	Pix *next;
	Image *b;
	int width;
	int height;
	char name[NNAME];
};

char *pixcmd[]={
[GIF]	"gif -9t",
[JPEG]	"jpg -9t",
[PNG]	"png -9t",
[BMP]	"bmp -9t",
[ICO]	"ico -c",
};

void getimage(Rtext *t, Www *w){
	Action *ap;
	Url *url;
	Image *b;
	int fd, typ;
	char err[512], buf[80], *s;
	Pix *p;

	ap=t->user;
	url=emalloc(sizeof(Url));
	seturl(url, ap->image, w->url->fullname);
	for(p=w->pix;p!=nil; p=p->next)
		if(strcmp(ap->image, p->name)==0 && ap->width==p->width && ap->height==p->height){
			t->b = p->b;
			w->changed=1;
			return;
		}
	fd=urlget(url, -1);
	if(fd==-1){
	Err:
		snprint(err, sizeof(err), "[img: %s: %r]", urlstr(url));
		free(t->text);
		t->text=strdup(err);
		w->changed=1;
		close(fd);
		goto Out;
	}
	typ = snooptype(fd);
	if(typ < 0 || typ >= nelem(pixcmd) || pixcmd[typ] == nil){
		werrstr("unknown image type");
		goto Err;
	}
	if((fd = pipeline(fd, "exec %s", pixcmd[typ])) < 0)
		goto Err;
	if(ap->width>0 || ap->height>0){
		s = buf;
		s += sprint(s, "exec resize");
		if(ap->width>0)
			s += sprint(s, " -x %d", ap->width);
		if(ap->height>0)
			s += sprint(s, " -y %d", ap->height);
		USED(s);
		if((fd = pipeline(fd, buf)) < 0)
			goto Err;
	}
	b=readimage(display, fd, 1);
	if(b==0){
		werrstr("can't read image");
		goto Err;
	}
	close(fd);
	p=emalloc(sizeof(Pix));
	nstrcpy(p->name, ap->image, sizeof(p->name));
	p->b=b;
	p->width=ap->width;
	p->height=ap->height;
	p->next=w->pix;
	w->pix=p;
	t->b=b;
	w->changed=1;
Out:
	freeurl(url);
}

void getpix(Rtext *t, Www *w){
	int i, pid, nworker, worker[NXPROC];
	Action *ap;

	nworker = 0;
	for(i=0; i<nelem(worker); i++)
		worker[i] = -1;

	for(;t!=0;t=t->next){
		ap=t->user;
		if(ap && ap->image){
			pid = rfork(RFFDG|RFPROC|RFMEM);
			switch(pid){
			case -1:
				fprint(2, "fork: %r\n");
				break;
			case 0:
				getimage(t, w);
				exits(0);
			default:
				for(i=0; i<nelem(worker); i++)
					if(worker[i] == -1){
						worker[i] = pid;
						nworker++;
						break;
					}

				while(nworker == nelem(worker)){
					if((pid = waitpid()) < 0)
						break;
					for(i=0; i<nelem(worker); i++)
						if(worker[i] == pid){
							worker[i] = -1;
							nworker--;
							break;
						}
				}
			}
			
		}
	}
	while(nworker > 0){
		if((pid = waitpid()) < 0)
			break;
		for(i=0; i<nelem(worker); i++)
			if(worker[i] == pid){
				worker[i] = -1;
				nworker--;
				break;
			}
	}
}

ulong countpix(void *p){
	ulong n=0;
	Pix *x;
	for(x = p; x; x = x->next)
		n += Dy(x->b->r)*bytesperline(x->b->r, x->b->depth);
	return n;
}

void freepix(void *p){
	Pix *x, *xx;
	xx = p;
	while(x = xx){
		xx = x->next;
		freeimage(x->b);
		free(x);
	}
}
