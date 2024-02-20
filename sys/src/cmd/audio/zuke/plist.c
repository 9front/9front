#include <u.h>
#include <libc.h>
#include <bio.h>
#include "plist.h"

void
printmeta(Biobuf *b, Meta *m)
{
	int i;

	Bprint(b, "%c %s\n%c %s\n", Ppath, m->path, Pfilefmt, m->filefmt);
	for(i = 0; i < m->numartist; i++)
		Bprint(b, "%c %s\n", Partist, m->artist[i]);
	if(m->album != nil)
		Bprint(b, "%c %s\n", Palbum, m->album);
	if(m->title != nil)
		Bprint(b, "%c %s\n", Ptitle, m->title);
	if(m->composer != nil)
		Bprint(b, "%c %s\n", Pcomposer, m->composer);
	if(m->date != nil)
		Bprint(b, "%c %s\n", Pdate, m->date);
	if(m->track != nil)
		Bprint(b, "%c %s\n", Ptrack, m->track);
	if(m->duration > 0)
		Bprint(b, "%c %llud\n", Pduration, m->duration);
	if(m->rgtrack != 0.0)
		Bprint(b, "%c %g\n", Prgtrack, m->rgtrack);
	if(m->rgalbum != 0.0)
		Bprint(b, "%c %g\n", Prgalbum, m->rgalbum);
	if(m->imagesize > 0)
		Bprint(b, "%c %d %d %d %s\n", Pimage, m->imageoffset, m->imagesize, m->imagereader, m->imagefmt);
	Bprint(b, "\n");
}
