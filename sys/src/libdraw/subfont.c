#include <u.h>
#include <libc.h>
#include <draw.h>

Subfont*
allocsubfont(char *name, int n, int height, int ascent, Fontchar *info, Image *i)
{
	Subfont *f;

	assert(height != 0 /* allocsubfont */);

	f = malloc(sizeof(Subfont));
	if(f == nil)
		return nil;
	f->n = n;
	f->height = height;
	f->ascent = ascent;
	f->info = info;
	f->bits = i;
	f->ref = 1;
	if(name){
		f->name = strdup(name);
		if(f->name == nil){
			free(f);
			return nil;
		}
		installsubfont(name, f);
	}else
		f->name = nil;
	return f;
}
