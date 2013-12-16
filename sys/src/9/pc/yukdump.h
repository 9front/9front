typedef struct Regdump Regdump;
struct Regdump {
	uint	offset;
	uint	size;
	char	*name;
};

Regdump pcireg[] = {
	Pciphy,	32,	"Pciphy",
	Pciclk,	32,	"Pciclk",
	Pcistate,	32,	"Pcistate",
};

static Regdump gmacreg[] = {
	Stat,	16,	"Stat",
	Ctl,	16,	"Ctl",
	Txctl,	16,	"Txctl",
	Rxctl,	16,	"Rxctl",
	Txflow,	16,	"Txflow",
	Txparm,	16,	"Txparm",
	Serctl,	16,	"Serctl",
	Txirq,	16,	"Txirq",
	Rxirq,	16,	"Rxirq",
	Trirq,	16,	"Trirq",
	Txmask,	16,	"Txmask",
	Rxmask,	16,	"Rxmask",
	Trmask,	16,	"Trmask",
	Smictl,	16,	"Smictl",
	Smidata,	16,	"Smidata",
	Phyaddr,	16,	"Phyaddr",
	Mchash+0,	16,	"Mchash",
	Mchash+2,	16,	"Mchash",
	Mchash+4,	16,	"Mchash",
	Mchash+6,	16,	"Mchash",
	Ea0,	16,	"Ea0",
	Ea0+2,	16,	"Ea0",
	Ea0+4,	16,	"Ea0",
	Ea1,	16,	"Ea1",
	Ea1+2,	16,	"Ea1",
	Ea1+4,	16,	"Ea1",
};

static Regdump macreg[] = {
	Txactl,	8,	"Txactl",
	Gfrxctl,	32,	"Gfrxctl",
	Grxfm,	32,	"Grxfm",
	Grxft,	32,	"Grxft",
	Grxtt,	32,	"Grxtt",
	Gmfctl,	32,	"Gmfctl",
	Mac,	32,	"Mac",
	Phy,	32,	"Phy",
	Irqm,	8,	"Irqm",
	Linkctl,	8,	"Linkctl",

	Rxwp,	32,	"Rxwp",
	Rxrp,	32,	"Rxrp",
	Rxrlev,	32,	"Rxrlev",

};

static Regdump reg[] = {
	Ctst,	16,	"Ctst",
	Pwrctl,	8,	"Pwrctl",
	Isr,	32,	"Isr",
	Ism,	32,	"Ism",
	Hwe,	32,	"Hwe",
	Hwem,	32,	"Hwem",
	Isrc2,	32,	"Isrc2",

	Macadr/2,	16,	"Macadr",
	Macadr/2+1,	16,	"Macadr",
	Macadr/2+2,	16,	"Macadr",

	Pmd,	8,	"Pmd",
	Maccfg,	8,	"Maccfg",
	Chip,	8,	"Chip",
	Ramcnt,	8,	"Ramcnt",
	Clkgate,	8,	"Clkgate",
	Hres,	8,	"Hres",
	Clkctl,	32,	"Clkctl",
	Tstctl1,	8,	"Tstctl1",

	Asfcs,	8,	"Asfcs",
	Asfhost,	32,	"Asfhost",
	Statctl,	32,	"Statctl",
	Stattl,	16,	"Stattl",
	Stataddr,	32,	"Stataddr",
	Statth,	16,	"Statth",
	Stathd,	16,	"Stathd",
	Statwm,	8,	"Statwm",
	Statiwm,	8,	"Statiwm",
};

static char*
dumppci(Ctlr *c, char *p, char *e)
{
	int i;
	Regdump *r;

	r = pcireg;
	p = seprint(p, e, "/* pci reg */\n");
	for(i = 0; i < nelem(pcireg); i++)
		switch(r[i].size){
		default:
			p = seprint(p, e, "%s: bug size %d\n", r[i].name, r[i].size);
			break;
		case 32:
			p = seprint(p, e, "%s: %.8ux\n", r[i].name, pcicfgr32(c->p, r[i].offset));
			break;
		}
	return p;
}

static char*
dumpgmac(Ctlr *c, char *p, char *e)
{
	int i;
	Regdump *r;

	r = gmacreg;
	p = seprint(p, e, "/* gmac reg */\n");
	for(i = 0; i < nelem(gmacreg); i++)
		switch(r[i].size){
		default:
			p = seprint(p, e, "%s: bug size %d\n", r[i].name, r[i].size);
			break;
		case 16:
			p = seprint(p, e, "%s: %.4ux\n", r[i].name, gmacread(c, r[i].offset));
			break;
		case 32:
			p = seprint(p, e, "%s: %.8ux\n", r[i].name, gmacread32(c, r[i].offset));
			break;
		}
	return p;
}

static char*
dumpmac(Ctlr *c, char *p, char *e)
{
	int i;
	Regdump *r;

	r = macreg;
	p = seprint(p, e, "/* mac reg */\n");
	for(i = 0; i < nelem(macreg); i++)
		switch(r[i].size){
		default:
			p = seprint(p, e, "%s: bug size %d\n", r[i].name, r[i].size);
			break;
		case 8:
			p = seprint(p, e, "%s: %.2ux\n", r[i].name, macread8(c, r[i].offset));
			break;
		case 32:
			p = seprint(p, e, "%s: %.8ux\n", r[i].name, macread32(c, r[i].offset));
			break;
		}
	return p;
}

static char*
dumpreg(Ctlr *c, char *p, char *e)
{
	int i;
	Regdump *r;

	r = reg;
	p = seprint(p, e, "/* reg */\n");
	for(i = 0; i < nelem(reg); i++)
		switch(r[i].size){
		default:
			p = seprint(p, e, "%s: bug size %d\n", r[i].name, r[i].size);
			break;
		case 8:
			p = seprint(p, e, "%s: %.2ux\n", r[i].name, c->reg8[r[i].offset]);
			break;
		case 16:
			p = seprint(p, e, "%s: %.4ux\n", r[i].name, c->reg16[r[i].offset]);
			break;
		case 32:
			p = seprint(p, e, "%s: %.8ux\n", r[i].name, c->reg[r[i].offset]);
			break;
		}
	return p;
}

static char *optab[256] = {
[Orxchks]	"rxsum",
[Orxstat]		"rxstat",
[Otxidx]		"txidx",
};

static char*
rs(uint r)
{
	char *s;

	s = optab[r & 0xff];
	if(s == nil)
		s = "";
	return s;
}

static char*
dumpring(Sring *r, Block **t, char *p, char *e)
{
	int m, n;
	uint i;

	p = seprint(p, e, 
		"bring: rp %ud wp %ud cnt %ud m %ud	: ",
		r->rp, r->wp, r->cnt, r->m);
	m = -1;
	n = 0;
	for(i = 0; i < r->cnt; i++){
		n += t[i] != nil;
		if(m>=0 && t[i] == nil){
			p = seprint(p, e, "%ud", m);
			if(i != m + 1)
				p = seprint(p, e, "-%ud", i-1);
			p = seprint(p, e, " ");
			m = -1;
		}else if(m<0 && t[i] != nil)
			m = i;
	}
	if(m>=0){
		p = seprint(p, e, "%ud", m);
		if(i != m + 1)
			p = seprint(p, e, "-%ud ", i-1);
	}
	return seprint(p, e, "n=%d/%d", n, r->cnt);
}

/* debug; remove */
static void
descriptorfu(Ether *e, uint qoff)
{
	char buf[PRINTSIZE], *p, *f;
	uint q, qm1;
	Block *b, *a, **bring;
	Ctlr *c;
	Status *t, *v;
	Sring *r;

	c = e->ctlr;
	f = buf + sizeof buf;
	if(qoff == Qtx){
		bring = c->tbring;
		r = &c->tx;
		p = seprint(buf, f, "tx ");
	}else{
		bring = c->rbring;
		r = &c->rx;
		p = seprint(buf, f, "rx ");
	}

	q = prread16(c, qoff + Pgetidx);
	print("  getidx %ud\n", q);
	if(q >= r->cnt){
		q &= r->m;
		print("  try getidx %ud\n", q);
	}
	qm1 = q-1 & r->m;
	t = r->r + q;
	v = r->r + qm1;
	b = bring[q];
	a = bring[qm1];
	print("	%0.4d %.2ux	l=%d p=%#p @%#p\n", q, t->op, 
		(uint)getle(t->l, 2), KZERO+(ulong)getle(t->status, 4), Pciwaddrl(t));
	print("	%0.4d %.2ux	l=%d p=%#p @%#p\n", qm1, t->op,
		(uint)getle(v->l, 2), KZERO+(ulong)getle(v->status, 4), Pciwaddrl(v));
	if(r == &c->rx)
		print("	%#p %#p  (wp)\n", b? b->wp: 0, a? a->wp: 0);
	else
		print("	%#p %#p  (rp)\n", b? b->rp: 0, a? a->rp: 0);

	dumpring(r, bring, p, f);
	print("	%s", buf);
}
