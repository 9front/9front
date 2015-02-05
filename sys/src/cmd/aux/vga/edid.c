#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>

#include "pci.h"
#include "vga.h"

static Modelist*
addmode(Modelist *l, Mode m)
{
	Modelist *ll;
	int rr;

	rr = (m.frequency+m.ht*m.vt/2)/(m.ht*m.vt);
	snprint(m.name, sizeof m.name, "%dx%d@%dHz", m.x, m.y, rr);

	for(ll = l; ll != nil; ll = ll->next){
		if(strcmp(ll->name, m.name) == 0){
			ll->Mode = m;
			return l;
		}
	}

	ll = alloc(sizeof(Modelist));
	ll->Mode = m;
	ll->next = l;
	return ll;
}

/*
 * Parse VESA EDID information.  Based on the VESA
 * Extended Display Identification Data standard, Version 3,
 * November 13, 1997.  See /public/doc/vesa/edidv3.pdf.
 *
 * This only handles 128-byte EDID blocks.  Until I find
 * a monitor that produces 256-byte blocks, I'm not going
 * to try to decode them.
 */

/*
 * Established timings block.  There is a bitmap
 * that says whether each mode is supported.  Most
 * of these have VESA definitions.  Those that don't are marked
 * as such, and we ignore them (the lookup fails).
 */
static char *estabtime[] = {
	"720x400@70Hz",	/* non-VESA: IBM, VGA */
	"720x400@88Hz",	/* non-VESA: IBM, XGA2 */
	"640x480@60Hz",
	"640x480@67Hz",	/* non-VESA: Apple, Mac II */
	"640x480@72Hz",
	"640x480@75Hz",
	"800x600@56Hz",
	"800x600@60Hz",

	"800x600@72Hz",
	"800x600@75Hz",
	"832x624@75Hz",	/* non-VESA: Apple, Mac II */
	"1024x768i@87Hz",	/* non-VESA: IBM */
	"1024x768@60Hz",
	"1024x768@70Hz",
	"1024x768@75Hz",
	"1280x1024@75Hz",

	"1152x870@75Hz",	/* non-VESA: Apple, Mac II */
};

/*
 * Decode the EDID detailed timing block.  See pp. 20-21 of the standard.
 */
static int
decodedtb(Mode *m, uchar *p)
{
	int ha, hb, hso, hspw, rr, va, vb, vso, vspw;
	/* int hbord, vbord, dxmm, dymm, hbord, vbord; */

	memset(m, 0, sizeof *m);

	m->frequency = ((p[1]<<8) | p[0]) * 10000;

	ha = ((p[4] & 0xF0)<<4) | p[2];		/* horizontal active */
	hb = ((p[4] & 0x0F)<<8) | p[3];		/* horizontal blanking */
	va = ((p[7] & 0xF0)<<4) | p[5];		/* vertical active */
	vb = ((p[7] & 0x0F)<<8) | p[6];		/* vertical blanking */
	hso = ((p[11] & 0xC0)<<2) | p[8];	/* horizontal sync offset */
	hspw = ((p[11] & 0x30)<<4) | p[9];	/* horizontal sync pulse width */
	vso = ((p[11] & 0x0C)<<2) | ((p[10] & 0xF0)>>4);	/* vertical sync offset */
	vspw = ((p[11] & 0x03)<<4) | (p[10] & 0x0F);		/* vertical sync pulse width */

	/* dxmm = (p[14] & 0xF0)<<4) | p[12]; 	/* horizontal image size (mm) */
	/* dymm = (p[14] & 0x0F)<<8) | p[13];	/* vertical image size (mm) */
	/* hbord = p[15];		/* horizontal border (pixels) */
	/* vbord = p[16];		/* vertical border (pixels) */

	m->x = ha;
	m->y = va;

	m->ht = ha+hb;
	m->shb = ha+hso;
	m->ehb = ha+hso+hspw;

	m->vt = va+vb;
	m->vrs = va+vso;
	m->vre = va+vso+vspw;

	if(p[17] & 0x80)	/* interlaced */
		m->interlace = 'v';

	if(p[17] & 0x60)	/* some form of stereo monitor mode; no support */
		return -1;

	/*
	 * Sync signal description.  I have no idea how to properly handle the 
	 * first three cases, which I think are aimed at things other than
	 * canonical SVGA monitors.
	 */
	switch((p[17] & 0x18)>>3) {
	case 0:	/* analog composite sync signal*/
	case 1:	/* bipolar analog composite sync signal */
		/* p[17] & 0x04 means serration: hsync during vsync */
		/* p[17] & 0x02 means sync pulse appears on RGB not just G */
		break;

	case 2:	/* digital composite sync signal */
		/* p[17] & 0x04 means serration: hsync during vsync */
		/* p[17] & 0x02 means hsync positive outside vsync */
		break;

	case 3:	/* digital separate sync signal; the norm */
		m->vsync = (p[17] & 0x04) ? '+' : '-';
		m->hsync = (p[17] & 0x02) ? '+' : '-';
		break;
	}
	/* p[17] & 0x01 is another stereo bit, only referenced if p[17] & 0x60 != 0 */

	rr = (m->frequency+m->ht*m->vt/2) / (m->ht*m->vt);

	snprint(m->name, sizeof m->name, "%dx%d@%dHz", m->x, m->y, rr);

	return 0;
}

static int
vesalookup(Mode *m, char *name)
{
	Mode **p;

	for(p=vesamodes; *p; p++)
		if(strcmp((*p)->name, name) == 0) {
			*m = **p;
			return 0;
		}
	return -1;
}

static int
decodesti(Mode *m, uchar *p)
{
	int x, y, rr;
	char str[20];

	x = (p[0]+31)*8;
	switch((p[1]>>6) & 3){
	default:
	case 0:
		y = x;
		break;
	case 1:
		y = (x*4)/3;
		break;
	case 2:
		y = (x*5)/4;
		break;
	case 3:
		y = (x*16)/9;
		break;
	}
	rr = (p[1] & 0x1F) + 60;

	sprint(str, "%dx%d@%dHz", x, y, rr);
	return vesalookup(m, str);
}

Edid*
parseedid128(void *v)
{
	static uchar magic[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	uchar *p, *q, sum;
	int dpms, estab, i, m, vid;
	Mode mode;
	Edid *e;

	e = alloc(sizeof(Edid));

	p = (uchar*)v;
	if(memcmp(p, magic, 8) != 0) {
		free(e);
		werrstr("bad edid header");
		return nil;
	}

	sum = 0;
	for(i=0; i<128; i++) 
		sum += p[i];
	if(sum != 0) {
		free(e);
		werrstr("bad edid checksum");
		return nil;
	}
	p += 8;

	assert(p == (uchar*)v+8);	/* assertion offsets from pp. 12-13 of the standard */
	/*
	 * Manufacturer name is three 5-bit ascii letters, packed
	 * into a big endian [sic] short in big endian order.  The high bit is unused.
	 */
	i = (p[0]<<8) | p[1];
	p += 2;
	e->mfr[0] = 'A'-1 + ((i>>10) & 0x1F);
	e->mfr[1] = 'A'-1 + ((i>>5) & 0x1F);
	e->mfr[2] = 'A'-1 + (i & 0x1F);
	e->mfr[3] = '\0';

	/*
	 * Product code is a little endian short.
	 */
	e->product = (p[1]<<8) | p[0];
	p += 2;

	/*
	 * Serial number is a little endian long, 0x01010101 = unused.
	 */
	e->serial = (p[3]<<24) | (p[2]<<16) | (p[1]<<8) | p[0];
	p += 4;
	if(e->serial == 0x01010101)
		e->serial = 0;

	e->mfrweek = *p++;
	e->mfryear = 1990 + *p++;

	assert(p == (uchar*)v+8+10);
	/*
	 * Structure version is next two bytes: major.minor.
	 */
	e->version = *p++;
	e->revision = *p++;

	assert(p == (uchar*)v+8+10+2);
	/*
	 * Basic display parameters / features.
	 */
	/*
	 * Video input definition byte: 0x80 tells whether it is
	 * an analog or digital screen; we ignore the other bits.
	 * See p. 15 of the standard.
	 */
	vid = *p++;
	if(vid & 0x80)
		e->flags |= Fdigital;

	e->dxcm = *p++;
	e->dycm = *p++;
	e->gamma = 100 + *p++;
	dpms = *p++;
	if(dpms & 0x80)
		e->flags |= Fdpmsstandby;
	if(dpms & 0x40)
		e->flags |= Fdpmssuspend;
	if(dpms & 0x20)
		e->flags |= Fdpmsactiveoff;
	if((dpms & 0x18) == 0x00)
		e->flags |= Fmonochrome;
	if(dpms & 0x01)
		e->flags |= Fgtf;

	assert(p == (uchar*)v+8+10+2+5);
	/*
	 * Color characteristics currently ignored.
	 */
	p += 10;

	assert(p == (uchar*)v+8+10+2+5+10);
	/*
	 * Established timings: a bitmask of 19 preset timings.
	 */
	estab = (p[0]<<16) | (p[1]<<8) | p[2];
	p += 3;

	for(i=0, m=1<<23; i<nelem(estabtime); i++, m>>=1)
		if(estab & m)
			if(vesalookup(&mode, estabtime[i]) == 0)
				e->modelist = addmode(e->modelist,  mode);

	assert(p == (uchar*)v+8+10+2+5+10+3);
	/*
	 * Standard Timing Identifications: eight 2-byte selectors
	 * of more standard timings.
	 */

	for(i=0; i<8; i++, p+=2)
		if(decodesti(&mode, p+2*i) == 0)
			e->modelist = addmode(e->modelist, mode);

	assert(p == (uchar*)v+8+10+2+5+10+3+16);
	/*
	 * Detailed Timings
	 */
	for(i=0; i<4; i++, p+=18) {
		if(p[0] || p[1]) {	/* detailed timing block: p[0] or p[1] != 0 */
			if(decodedtb(&mode, p) == 0)
				e->modelist = addmode(e->modelist, mode);
		} else if(p[2]==0) {	/* monitor descriptor block */
			switch(p[3]) {
			case 0xFF:	/* monitor serial number (13-byte ascii, 0A terminated) */
				if(q = memchr(p+5, 0x0A, 13))
					*q = '\0';
				memset(e->serialstr, 0, sizeof(e->serialstr));
				strncpy(e->serialstr, (char*)p+5, 13);
				break;
			case 0xFE:	/* ascii string (13-byte ascii, 0A terminated) */
				break;
			case 0xFD:	/* monitor range limits */
				e->rrmin = p[5];
				e->rrmax = p[6];
				e->hrmin = p[7]*1000;
				e->hrmax = p[8]*1000;
				if(p[9] != 0xFF)
					e->pclkmax = p[9]*10*1000000;
				break;
			case 0xFC:	/* monitor name (13-byte ascii, 0A terminated) */
				if(q = memchr(p+5, 0x0A, 13))
					*q = '\0';
				memset(e->name, 0, sizeof(e->name));
				strncpy(e->name, (char*)p+5, 13);
				break;
			case 0xFB:	/* extra color point data */
				break;
			case 0xFA:	/* extra standard timing identifications */
				for(i=0; i<6; i++)
					if(decodesti(&mode, p+5+2*i) == 0)
						e->modelist = addmode(e->modelist, mode);
				break;
			}
		}
	}

	assert(p == (uchar*)v+8+10+2+5+10+3+16+72);
	return e;
}

Flag edidflags[] = {
	Fdigital, "digital",
	Fdpmsstandby, "standby",
	Fdpmssuspend, "suspend",
	Fdpmsactiveoff, "activeoff",
	Fmonochrome, "monochrome",
	Fgtf, "gtf",
	0
};

void
printflags(Flag *f, int b)
{
	int i;

	for(i=0; f[i].bit; i++)
		if(f[i].bit & b)
			Bprint(&stdout, " %s", f[i].desc);
	Bprint(&stdout, "\n");
}

void
printedid(Edid *e)
{
	Modelist *l;

	printitem("edid", "mfr");
	Bprint(&stdout, "%s\n", e->mfr);
	printitem("edid", "serialstr");
	Bprint(&stdout, "%s\n", e->serialstr);
	printitem("edid", "name");
	Bprint(&stdout, "%s\n", e->name);
	printitem("edid", "product");
	Bprint(&stdout, "%d\n", e->product);
	printitem("edid", "serial");
	Bprint(&stdout, "%lud\n", e->serial);
	printitem("edid", "version");
	Bprint(&stdout, "%d.%d\n", e->version, e->revision);
	printitem("edid", "mfrdate");
	Bprint(&stdout, "%d.%d\n", e->mfryear, e->mfrweek);
	printitem("edid", "size (cm)");
	Bprint(&stdout, "%dx%d\n", e->dxcm, e->dycm);
	printitem("edid", "gamma");
	Bprint(&stdout, "%.2f\n", e->gamma/100.);
	printitem("edid", "vert (Hz)");
	Bprint(&stdout, "%d-%d\n", e->rrmin, e->rrmax);
	printitem("edid", "horz (Hz)");
	Bprint(&stdout, "%d-%d\n", e->hrmin, e->hrmax);
	printitem("edid", "pclkmax");
	Bprint(&stdout, "%lud\n", e->pclkmax);
	printitem("edid", "flags");
	printflags(edidflags, e->flags);

	for(l=e->modelist; l; l=l->next){
		printitem("edid", l->name);
		Bprint(&stdout, "\n\t\tclock=%g\n", l->frequency/1.e6);
		Bprint(&stdout, "\t\tshb=%d ehb=%d ht=%d\n", l->shb, l->ehb, l->ht);
		Bprint(&stdout, "\t\tvrs=%d vre=%d vt=%d\n", l->vrs, l->vre, l->vt);
		Bprint(&stdout, "\t\thsync=%c vsync=%c %s\n",
			l->hsync?l->hsync:'?',
			l->vsync?l->vsync:'?',
			l->interlace?"interlace=v" : "");
	}
}
