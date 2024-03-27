#include <u.h>
#include <libc.h>
#include <pcm.h>

Pcmdesc	pcmdescdef =
{
	.rate = 44100,
	.channels = 2,
	.framesz = 4,
	.abits = 16,
	.bits = 16,
	.fmt = L's',
};

int
pcmdescfmt(Fmt *f)
{
	Pcmdesc d;

	d = va_arg(f->args, Pcmdesc);
	return fmtprint(f, "%C%dc%dr%d", d.fmt, d.bits, d.channels, d.rate);
}

int
mkpcmdesc(char *f, Pcmdesc *d)
{
	Rune r;
	char *p;

	memset(d, 0, sizeof(*d));
	p = f;
	while(*p != 0){
		p += chartorune(&r, p);
		switch(r){
		case L'r':
			d->rate = strtol(p, &p, 10);
			break;
		case L'c':
			d->channels = strtol(p, &p, 10);
			break;
		case L'm':
			r = L'µ';
		case L's':
		case L'S':
		case L'u':
		case L'U':
		case L'f':
		case L'a':
		case L'µ':
			d->fmt = r;
			d->bits = d->abits = strtol(p, &p, 10);
			break;
		default:
			goto Bad;
		}
	}
	if(d->rate <= 0 || d->channels <= 0)
		goto Bad;
	if(d->fmt == L'a' || d->fmt == L'µ'){
		if(d->bits != 8)
			goto Bad;
		d->abits = 16;
	} else if(d->fmt == L'f'){
		if(d->bits != 32 && d->bits != 64)
			goto Bad;
		d->abits = sizeof(int)*8;
	} else if(d->bits <= 0 || d->bits > 32)
		goto Bad;
	d->framesz = ((d->bits+7)/8) * d->channels;
	if(d->framesz <= 0)
		goto Bad;
	return 0;
Bad:
	werrstr("bad format: %s", f);
	return -1;
}
