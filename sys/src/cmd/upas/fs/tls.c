#include "common.h"
#include <libsec.h>
#include <auth.h>
#include "dat.h"

int
wraptls(int ofd, char *host)
{
	Thumbprint *thumb;
	TLSconn conn;
	int fd;

	memset(&conn, 0, sizeof conn);
	conn.serverName = host;
	fd = tlsClient(ofd, &conn);
	if(fd < 0){
		close(ofd);
		return -1;
	}
	thumb = initThumbprints("/sys/lib/tls/mail", "/sys/lib/tls/mail.exclude", "x509");
	if(thumb != nil){
		if(!okCertificate(conn.cert, conn.certlen, thumb)){
			werrstr("cert for %s not recognized: %r", host);
			close(fd);
			fd = -1;
		}
		freeThumbprints(thumb);
	}
	free(conn.cert);
	free(conn.sessionID);
	return fd;
}
