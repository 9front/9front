#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dat.h"
#include "protos.h"

extern Mux ethertypes[];

typedef struct Hdr	Hdr;
struct Hdr {
	uchar	tag[2];
	uchar	type[2];
};

enum
{
	Ov,	/* vlan */
	Oq,	/* qprio */
	Ot,	/* type */
};

static Field p_fields[] =
{
	{"v",	Fnum,	Ov,	"vlan id" } ,
	{"q",	Fnum,	Oq,	"queue prio" } ,
	{"t",	Fnum,	Ot,	"type" } ,
	{0}
};

static void
p_compile(Filter *f)
{
	Mux *m;

	if(f->op == '='){
		compile_cmp(vlan.name, f, p_fields);
		return;
	}
	for(m = ethertypes; m->name != nil; m++)
		if(strcmp(f->s, m->name) == 0){
			f->pr = m->pr;
			f->ulv = m->val;
			f->subop = Ot;
			return;
		}
	sysfatal("unknown vlan field or protocol: %s", f->s);
}

static int
p_filter(Filter *f, Msg *m)
{
	Hdr *h;

	if(m->pe - m->ps < 4)
		return 0;

	h = (Hdr*)m->ps;
	m->ps += 4;

	switch(f->subop){
	case Ov:
		return (NetS(h->tag) & 0xFFF) == f->ulv;
	case Oq:
		return (NetS(h->tag) >> 12) == f->ulv;
	case Ot:
		return NetS(h->type) == f->ulv;
	}
	return 0;
}

static int
p_seprint(Msg *m)
{
	uint v, q, t;
	int len;
	Hdr *h;

	len = m->pe - m->ps;
	if(len < 4)
		return -1;

	h = (Hdr*)m->ps;
	m->ps += 4;

	q = NetS(h->tag) >> 12;
	v = NetS(h->tag) & 0xFFF;
	t = NetS(h->type);
	demux(ethertypes, t, t, m, &dump);

	m->p = seprint(m->p, m->e, "v=%ud q=%ux pr=%4.4ux ln=%d", v, q, t, len);
	return 0;
}

Proto vlan =
{
	"vlan",
	p_compile,
	p_filter,
	p_seprint,
	ethertypes,
	"%#.4lux",
	p_fields,
	defaultframer
};
