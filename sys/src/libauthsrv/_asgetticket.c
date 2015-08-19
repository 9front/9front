#include <u.h>
#include <libc.h>
#include <authsrv.h>

static char *pbmsg = "AS protocol botch";

int
_asgetticket(int fd, Ticketreq *tr, char *tbuf, int tbuflen)
{
	if(_asrequest(fd, tr) < 0){
		werrstr(pbmsg);
		return -1;
	}
	if(tbuflen > 2*TICKETLEN)
		tbuflen = 2*TICKETLEN;
	return _asrdresp(fd, tbuf, tbuflen);
}
