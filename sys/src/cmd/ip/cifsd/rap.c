#include <u.h>
#include <libc.h>
#include <auth.h>
#include "dat.h"
#include "fns.h"

static void
padname(uchar *buf, int len, char *name)
{
	int n;
	n = strlen(name);
	if(n >= len)
		n = len-1;
	memset(buf, 0, len);
	memmove(buf, name, n);
}

static int
packshareinfo(Trans *t, int level, char *name, int *pstatus)
{
	Share *share;
	uchar buf[13];

	if((share = mapshare(name)) == nil){
		if(pstatus)
			*pstatus = 0x906;	/* NERR_NetNameNotFound */
		return 0;
	}
	padname(buf, sizeof(buf), share->name);
	switch(level){
	case 0:
		return pack(t->out.data.b, t->out.data.p, t->out.data.e, "[]",
			buf, buf+sizeof(buf));
	case 1:
		return pack(t->out.data.b, t->out.data.p, t->out.data.e, "[]_w@1l{f}",
			buf, buf+sizeof(buf), share->stype, smbstrpack8, share->remark);
	case 2:
		return pack(t->out.data.b, t->out.data.p, t->out.data.e, "[]_w@1l__ww@2l__________{f}{f}",
			buf, buf+sizeof(buf), share->stype, 100, 1, smbstrpack8, share->remark,
			smbnamepack8, share->root);
	default:
		return -1;
	}
}

void
transrap(Trans *t)
{
	char *pd, *dd, *name;
	int n, code, status, level, rbs;
	uchar *ip, *ipe, *op, *opb, *ope;
	uchar buf[16];

	code = 0;
	name = nil;
	pd = dd = nil;
	ip = ipe = t->in.param.e;
	if(!unpack(t->in.param.b, t->in.param.p, t->in.param.e, "wff[]", &code, 
		smbstrunpack8, &pd, smbstrunpack8, &dd, &ip, nil)){
		t->respond(t, STATUS_NOT_SUPPORTED);
		goto out;
	}

	ope = t->out.param.e;
	opb = op = t->out.param.b+2+2;

	n = status = level = 0;
	switch(code){
	case 0x0000:	/* NetShareEnum */
		op += pack(opb, op, ope, "ww", 0, 0);
		if(!unpack(ip, ip, ipe, "ww", &level, &rbs))
			break;
		if((n = packshareinfo(t, level, "local", nil)) > 0){
			t->out.data.p += n;
			pack(opb, opb, ope, "ww", 1, 1);
		}
		break;

	case 0x0001:	/* NetShareGetInfo */
		op += pack(opb, op, ope, "w", 0);
		if(!unpack(ip, ip, ipe, "fww", smbstrunpack8, &name, &level, &rbs))
			break;
		if((n = packshareinfo(t, level, name, &status)) > 0){
outlen:
			t->out.data.p += n;
			pack(opb, opb, ope, "w", n);
		}
		break;

	case 0x000d:	/* NetServerGetInfo */
		op += pack(opb, op, ope, "w", 0);
		if(!unpack(ip, ip, ipe, "ww", &level, &rbs))
			break;
		padname(buf, sizeof(buf), "");
		switch(level){
		case 0:
			if((n = pack(t->out.data.b, t->out.data.p, t->out.data.e, "[]", 
				buf, buf+sizeof(buf))) > 0)
				goto outlen;
			break;
		case 1:
			if((n = pack(t->out.data.b, t->out.data.p, t->out.data.e, "[]bbl@1l{f}", 
				buf, buf+sizeof(buf), 0x05, 0x00, 2, smbstrpack8, osname)) > 0)
				goto outlen;
		default:
			n = -1;
		}
		break;

	case 0x003f:	/* NetWrkstaGetInfo */
		op += pack(opb, op, ope, "w", 0);
		if(!unpack(ip, ip, ipe, "ww", &level, &rbs))
			break;
		if(level != 10){
			n = -1;
			break;
		}
		if((n = pack(t->out.data.b, t->out.data.p, t->out.data.e, 
			"@0l____@1lbb________{f}{f}", 0x05, 0x00, 
			smbstrpack8, sysname(), smbstrpack8, domain)) > 0)
			goto outlen;
		break;

	default:
		logit("[%.4x] unknown rap command pd=%s dd=%s", code, pd, dd);
	}
	if(n < 0){
		logit("[%.4x] unknown rap level [%.4x]", code, level);
		status = 0x7C;
	}
	if((n = pack(t->out.param.b, t->out.param.p, t->out.param.e, "w__[]", status, opb, op)) == 0){
		t->respond(t, STATUS_INVALID_SMB);
		goto out;
	}
	t->out.param.p += n;
	t->respond(t, 0);

out:
	free(name);
	free(pd);
	free(dd);
	return;
}
