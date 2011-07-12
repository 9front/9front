#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

/*
 * flags:
 * P = present
 * A = accessed (for code/data)
 * E = expand down (for data)
 * W = writable (for data)
 * R = readable (for code)
 * C = conforming (for code)
 * G = limit granularity in pages (for code/data)
 * D = 32 bit operand size (for code)
 * B = 32 bit stack pointer (for data)
 * Y = busy (for tss and tss16)
 * U = available for use by system software
 */

static struct {
	char	*name;
	char	*flags;
} descrtypes[] = {
	"data",		"--------AWE01--P----U.BG--------",
	"code",		"--------ARC11--P----U.DG--------",
	"tss16",	"--------1Y000--P----U..G--------",
	"ldt",		"--------01000--P----U..G--------",
	"callg16",	"--------00100--P----U..G--------",
	"taskg",	"--------10100--P----U..G--------",
	"intrg16",	"--------01100--P----U..G--------",
	"trapg16",	"--------11100--P----U..G--------",
	"tss",		"--------1Y010--P----U..G--------",
	"callg",	"--------00110--P----U..G--------",
	"intrg",	"--------01110--P----U..G--------",
	"trapg",	"--------11110--P----U..G--------",
};

/*
 * format:
 * idx[4] type[8] flags[8] dpl[1] base[8] limit[5]\n
 */

enum
{
	RECLEN	= 4+1 + 8+1 + 8+1 + 1+1 + 8+1 + 5+1,
};

static long
descwrite(Proc *proc, int local, void *v, long n, vlong)
{
	int i, j, t;
	char buf[RECLEN+1];
	char c, *p, *s, *e, *f[6];
	Segdesc d;

	int dpl;
	ulong base;
	ulong limit;

	s = (char*)v;
	e = s + n;

	if(waserror()){
		if(proc == up)
			flushmmu();
		nexterror();
	}

	while(s < e){
		for(p = s; p < e && *p != '\n'; p++);
			;
		if((p - s) > RECLEN)
			error(Ebadarg);
		memmove(buf, s, p - s);
		buf[p-s] = 0;
		s = p+1;

		if(getfields(buf, f, nelem(f), 1, " ") != nelem(f))
			error(Ebadarg);

		i = strtoul(f[0], nil, 16);

		for(t=0; t<nelem(descrtypes); t++)
			if(strcmp(descrtypes[t].name, f[1]) == 0)
				break;
		if(t == nelem(descrtypes))
			error(Ebadarg);

		dpl = atoi(f[3]);
		base = strtoul(f[4], nil, 16);
		limit = strtoul(f[5], nil, 16);

		d.d0 = ((base & 0xFFFF)<<16) | (limit & 0xFFFF);
		d.d1 = (base & 0xFF000000) | (limit & 0xF0000) | ((dpl & 3)<<13) | ((base & 0xFF0000)>>16);

		for(j=0; c = descrtypes[t].flags[j]; j++){
			switch(c){
			default:
				if(strchr(f[2], c) == nil){
			case '0':
			case '.':
					d.d1 &= ~(1<<j);
					break;					
				} else {
			case '1':
					d.d1 |= (1<<j);
					break;
				}
			case '-':
				continue;
			}
		}

		/* dont allow system segments */
		if((d.d1 & SEGP) && ((dpl != 3) || !(d.d1 & (1<<12))))
			error(Eperm);
		
		if(local){
			Segdesc *new, *old;
			int c;

			if(i < 0 || i >= 8192)
				error(Ebadarg);
			if(i >= (c = ((old = proc->ldt) ? proc->nldt : 0))){
				if((new = malloc(sizeof(Segdesc) * (i+1))) == nil)
					error(Enomem);
				if(c > 0)
					memmove(new, old, sizeof(Segdesc) * c);
				memset(new + c, 0, sizeof(Segdesc) * ((i+1) - c));
				proc->ldt = new;
				proc->nldt = i+1;
				free(old);
			}
			proc->ldt[i] = d;
		} else {
			if(i < PROCSEG0 || i >= PROCSEG0 + NPROCSEG)
				error(Ebadarg);
			proc->gdt[i - PROCSEG0] = d;
		}
	}
	poperror();

	if(proc == up)
		flushmmu();

	return n;
}

static long
descread(Proc *proc, int local, void *v, long n, vlong o)
{
	int i, j, k, t;
	char *s;

	int dpl;
	ulong base;
	ulong limit;

	s = v;
	for(i = 0;;i++){
		Segdesc d;

		if(local){
			if(proc->ldt == nil || i >= proc->nldt)
				break;
			d = proc->ldt[i];
		} else {
			if(i < PROCSEG0)
				i = PROCSEG0;
			if(i >= PROCSEG0 + NPROCSEG)
				break;
			d = proc->gdt[i - PROCSEG0];
		}

		if(o >= RECLEN){
			o -= RECLEN;
			continue;
		}

		if(s + RECLEN+1 >= (char*)v  + n)
			break;

		for(t=0; t<nelem(descrtypes); t++){
			for(j=0; descrtypes[t].flags[j]; j++){
				if(descrtypes[t].flags[j]=='0' && (d.d1 & (1<<j)) != 0)
					break;
				if(descrtypes[t].flags[j]=='1' && (d.d1 & (1<<j)) == 0)
					break;
			}
			if(descrtypes[t].flags[j] == 0)
				break;
		}
		if(t == nelem(descrtypes))
			t = 0;

		s += sprint(s, "%.4lux ", (ulong)i);
		s += sprint(s, "%-8s ", descrtypes[t].name);

		k = 0;
		for(j=0; descrtypes[t].flags[j]; j++){
			switch(descrtypes[t].flags[j]){
			case '-':
			case '.':
			case '0':
			case '1':
				continue;
			}
			if(d.d1 & (1 << j))
				s[k++] = descrtypes[t].flags[j];
		}
		if(k == 0)
			s[k++] = '-';
		while(k < 9)
			s[k++] = ' ';
		s += k;

		dpl = (d.d1 & 0x6000)>>13;
		base = ((d.d0 & 0xFFFF0000)>>16) | ((d.d1 & 0xFF)<<16) | (d.d1 & 0xFF000000);
		limit = (d.d1 & 0xF0000) | (d.d0 & 0xFFFF);

		s += sprint(s, "%.1d ", dpl);
		s += sprint(s, "%.8lux ", base);
		s += sprint(s, "%.5lux\n", limit);
	}

	return s-(char*)v;
}

static long
gdtread(Chan*, void *v, long n, vlong o)
{
	return descread(up, 0, v, n, o);
}

static long
gdtwrite(Chan*, void *v, long n, vlong o)
{
	return descwrite(up, 0, v, n, o);
}

static long
ldtread(Chan*, void *v, long n, vlong o)
{
	return descread(up, 1, v, n, o);
}

static long
ldtwrite(Chan*, void *v, long n, vlong o)
{
	return descwrite(up, 1, v, n, o);
}

void
segdesclink(void)
{
	addarchfile("gdt", 0666, gdtread, gdtwrite);
	addarchfile("ldt", 0666, ldtread, ldtwrite);
}
