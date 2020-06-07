#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dat.h"
#include "protos.h"

typedef struct Hdr Hdr;
struct Hdr {
	uchar	ia[16];
	uchar	data[1];
};

static Mux p_mux[] =
{
	{"ip",		0x40, },
	{"ip6", 	0x60, },
	{0}
};

enum
{
	Oia,	/* interface address */
	Ot,	/* ip type */
};

static Field p_fields[] =
{
	{"ia",	Fv6ip,	Oia,	"interface address", },
	{"t",	Fnum,	Ot,	"ip type" },
	{0}
};

static void
p_compile(Filter *f)
{
	Mux *m;

	if(f->op == '='){
		compile_cmp(ipmux.name, f, p_fields);
		return;
	}
	for(m = p_mux; m->name != nil; m++)
		if(strcmp(f->s, m->name) == 0){
			f->pr = m->pr;
			f->ulv = m->val;
			f->subop = Ot;
			return;
		}
	sysfatal("unknown ipmux field or protocol: %s", f->s);
}

static int
p_filter(Filter *f, Msg *m)
{
	Hdr *h;

	if(m->pe - m->ps <= 16)
		return 0;

	h = (Hdr*)m->ps;
	m->ps += 16;

	switch(f->subop){
	case Oia:
		return memcmp(h->ia, f->a, 16) == 0;
	case Ot:
		return (h->data[0]&0xF0) == f->ulv;
	}
	return 0;
}

static int
p_seprint(Msg *m)
{
	int len;
	uint t;
	Hdr *h;

	len = m->pe - m->ps;
	if(len <= 16)
		return -1;

	h = (Hdr*)m->ps;
	m->ps += 16;

	t = h->data[0]&0xF0;
	demux(p_mux, t, t, m, &dump);

	m->p = seprint(m->p, m->e, "ia=%I t=%2.2ux ln=%d", h->ia, t, len);
	return 0;
}

Proto ipmux =
{
	"ipmux",
	p_compile,
	p_filter,
	p_seprint,
	p_mux,
	"%#.2lux",
	p_fields,
	defaultframer
};
