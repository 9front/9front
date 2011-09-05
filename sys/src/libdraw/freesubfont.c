#include <u.h>
#include <libc.h>
#include <draw.h>

void
freesubfont(Subfont *f)
{
	if(f == nil || --f->ref)
		return;
	uninstallsubfont(f);
	free(f->name);
	free(f->info);	/* note: f->info must have been malloc'ed! */
	freeimage(f->bits);
	free(f);
}
