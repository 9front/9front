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
[PIC]	"fb/3to1 /lib/fb/cmap/rgbv",
[TIFF]	"/sys/lib/mothra/tiffcvt",
[XBM]	"fb/xbm2pic",
};

void storebitmap(Rtext *t, Image *b){
	t->b=b;
	free(t->text);
	t->text=0;
}

void getimage(Rtext *t, Www *w){
	int pfd[2];
	Action *ap;
	Url url;
	Image *b;
	int fd;
	char err[512];
	Pix *p;

	ap=t->user;
	crackurl(&url, ap->image, w->base);
	for(p=w->pix;p!=nil; p=p->next)
		if(strcmp(ap->image, p->name)==0 && ap->width==p->width && ap->height==p->height){
			storebitmap(t, p->b);
			free(ap->image);
			ap->image=0;
			w->changed=1;
			return;
		}
	fd=urlopen(&url, GET, 0);
	if(fd==-1){
	Err:
		snprint(err, sizeof(err), "[%s: %r]", url.fullname);
		free(t->text);
		t->text=strdup(err);
		free(ap->image);
		ap->image=0;
		w->changed=1;
		close(fd);
		return;
	}
	if(url.type!=GIF
	&& url.type!=JPEG
	&& url.type!=PNG
	&& url.type!=PIC
	&& url.type!=TIFF
	&& url.type!=XBM){
		werrstr("unknown image type");
		goto Err;
	}

	if((fd = pipeline(pixcmd[url.type], fd)) < 0)
		goto Err;
	if(ap->width>0 || ap->height>0){
		char buf[80];
		char *p;

		p = buf;
		p += sprint(p, "resize");
		if(ap->width>0)
			p += sprint(p, " -x %d", ap->width);
		if(ap->height>0)
			p += sprint(p, " -y %d", ap->height);
		if((fd = pipeline(buf, fd)) < 0)
			goto Err;
	}
	b=readimage(display, fd, 1);
	if(b==0){
		werrstr("can't read image");
		goto Err;
	}
	close(fd);
	p = emallocz(sizeof(Pix), 1);
	strncpy(p->name, ap->image, sizeof(p->name));
	p->b=b;
	p->width=ap->width;
	p->height=ap->height;
	p->next=w->pix;
	w->pix=p;
	storebitmap(t, b);
	free(ap->image);
	ap->image=0;
	w->changed=1;
}

void getpix(Rtext *t, Www *w){
	Action *ap;

	for(;t!=0;t=t->next){
		ap=t->user;
		if(ap && ap->image)
			getimage(t, w);
	}
}

void freepix(void *p)
{
	Pix *x, *xx;
	xx = p;
	while(x = xx){
		xx = x->next;
		freeimage(x->b);
		free(x);
	}
}
