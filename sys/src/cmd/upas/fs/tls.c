#include "common.h"
#include <libsec.h>
#include <auth.h>
#include "dat.h"

int
wraptls(int ofd)
{
	uchar digest[SHA1dlen];
	Thumbprint *thumb;
	TLSconn conn;
	int fd;

	memset(&conn, 0, sizeof conn);
	fd = tlsClient(ofd, &conn);
	if(fd < 0){
		close(ofd);
		return -1;
	}
	thumb = initThumbprints("/sys/lib/tls/mail", "/sys/lib/tls/mail.exclude");
	if(thumb != nil){
		if(conn.cert == nil || conn.certlen <= 0){
			werrstr("server did not provide TLS certificate");
			goto Err;
		}
		sha1(conn.cert, conn.certlen, digest, nil);
		if(!okThumbprint(digest, thumb)){
			werrstr("server certificate %.*H not recognized",
				SHA1dlen, digest);
		Err:
			close(fd);
			fd = -1;
		}
		freeThumbprints(thumb);
	}
	free(conn.cert);
	free(conn.sessionID);
	return fd;
}
