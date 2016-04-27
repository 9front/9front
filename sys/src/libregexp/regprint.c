#include <u.h>
#include <libc.h>
#include <regexp.h>
#include <regimpl.h>

static int
fmtprinst(Fmt *f, Reinst *inst)
{
	int r;

	r = fmtprint(f, "%p ", inst);
	switch(inst->op) {
	case ORUNE:
		r += fmtprint(f, "ORUNE\t%C\n", inst->r);
		break;
	case ONOTNL:
		r += fmtprint(f, "ONOTNL\n");
		break;
	case OCLASS:
		r += fmtprint(f, "OCLASS\t%C-%C %p\n", inst->r, inst->r1, inst->a);
		break;
	case OSPLIT:
		r += fmtprint(f, "OSPLIT\t%p %p\n", inst->a, inst->b);
		break;
	case OJMP:
		r += fmtprint(f, "OJMP \t%p\n", inst->a);
		break;
	case OSAVE:
		r += fmtprint(f, "OSAVE\t%d\n", inst->sub);
		break;
	case OUNSAVE:
		r += fmtprint(f, "OUNSAVE\t%d\n", inst->sub);
		break;
	case OANY:
		r += fmtprint(f, "OANY \t.\n");
		break;
	case OEOL:
		r += fmtprint(f, "OEOL \t$\n");
		break;
	case OBOL:
		r += fmtprint(f, "OBOL \t^\n");
		break;
	}
	return r;
}

static int
fmtprprog(Fmt *f, Reprog *reprog)
{
	Reinst *inst;
	int r;

	r = 0;
	for(inst = reprog->startinst; inst < reprog->startinst + reprog->len; inst++)
		r += fmtprinst(f, inst);
	return r;
}

int
reprogfmt(Fmt *f)
{
	Reprog *r;

	r = va_arg(f->args, Reprog*);
	return fmtprprog(f, r);
}
