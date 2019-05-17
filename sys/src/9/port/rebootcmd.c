#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"a.out.h"

static ulong
l2be(long l)
{
	uchar *cp;

	cp = (uchar*)&l;
	return (cp[0]<<24) | (cp[1]<<16) | (cp[2]<<8) | cp[3];
}


static void
readn(Chan *c, void *vp, long n)
{
	char *p = vp;
	long nn;

	while(n > 0) {
		nn = devtab[c->type]->read(c, p, n, c->offset);
		if(nn == 0)
			error(Eshort);
		c->offset += nn;
		p += nn;
		n -= nn;
	}
}

void
rebootcmd(int argc, char *argv[])
{
	Chan *c;
	Exec exec;
	ulong magic, text, rtext, entry, data, size, align;
	uchar *p;

	if(argc == 0)
		exit(0);

	c = namec(argv[0], Aopen, OEXEC, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}

	readn(c, &exec, sizeof(Exec));
	magic = l2be(exec.magic);
	entry = l2be(exec.entry);
	text = l2be(exec.text);
	data = l2be(exec.data);

	if(!(magic == AOUT_MAGIC)){
		switch(magic){
		case I_MAGIC:
		case S_MAGIC:
			if((I_MAGIC == AOUT_MAGIC) || (S_MAGIC == AOUT_MAGIC))
				break;
		default:
			error(Ebadexec);
		}
	}
	if(magic & HDR_MAGIC)
		readn(c, &exec, 8);

	switch(magic){
	case R_MAGIC:
		align = 0x10000;	/* 64k segment alignment for arm64 */
		break;
	default:
		align = BY2PG;
		break;
	}

	/* round text out to page boundary */
	rtext = ROUND(entry+text, align)-entry;
	size = rtext + data;
	p = malloc(size);
	if(p == nil)
		error(Enomem);

	if(waserror()){
		free(p);
		nexterror();
	}

	memset(p, 0, size);
	readn(c, p, text);
	readn(c, p + rtext, data);

	ksetenv("bootfile", argv[0], 1);

	reboot((void*)entry, p, size);
	error(Egreg);
}
