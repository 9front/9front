#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dat.h"
#include "protos.h"

static Mux p_mux[] =
{
	{"ip",		0x40, },
	{"ip6", 	0x60, },
	{0}
};

enum
{
	Ot,	/* ip type */
};

static Field p_fields[] =
{
	{"t",	Fnum,	Ot,	"ip type" },
	{0}
};

static void
p_compile(Filter *f)
{
	Mux *m;

	if(f->op == '='){
		compile_cmp(ippkt.name, f, p_fields);
		return;
	}
	for(m = p_mux; m->name != nil; m++)
		if(strcmp(f->s, m->name) == 0){
			f->pr = m->pr;
			f->ulv = m->val;
			f->subop = Ot;
			return;
		}
	sysfatal("unknown ippkt field or protocol: %s", f->s);
}

static int
p_filter(Filter *f, Msg *m)
{
	if(m->ps >= m->pe)
		return 0;

	switch(f->subop){
	case Ot:
		return (m->ps[0]&0xF0) == f->ulv;
	}
	return 0;
}

static int
p_seprint(Msg *m)
{
	uint t;

	if(m->ps >= m->pe)
		return -1;

	t = m->ps[0]&0xF0;
	demux(p_mux, t, t, m, &dump);

	m->p = seprint(m->p, m->e, "t=%2.2ux", t);
	return 0;
}

Proto ippkt =
{
	"ippkt",
	p_compile,
	p_filter,
	p_seprint,
	p_mux,
	"%#.2lux",
	p_fields,
	defaultframer
};
