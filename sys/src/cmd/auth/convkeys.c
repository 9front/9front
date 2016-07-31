#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>
#include <authsrv.h>
#include "authcmdlib.h"

Authkey	okey, nkey;

int	verb;
int	usepass;
int	convaes;

uchar	zeros[16];

int	convert(char**, int);
void	usage(void);

void
main(int argc, char *argv[])
{
	Dir *d;
	char *p, *file;
	int fd, len;

	ARGBEGIN{
	case 'p':
		usepass = 1;
		break;
	case 'v':
		verb = 1;
		break;
	case 'a':
		convaes = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();
	file = argv[0];

	private();

	/* get original key */
	if(usepass){
		print("enter password file is encoded with\n");
		getpass(&okey, nil, 0, 1);
	} else {
		getauthkey(&okey);
	}
	if(!verb){
		print("enter password to reencode with\n");
		getpass(&nkey, nil, 0, 1);
	}

	fd = open(file, ORDWR);
	if(fd < 0)
		error("can't open %s: %r\n", file);
	d = dirfstat(fd);
	if(d == nil)
		error("can't stat %s: %r\n", file);
	len = d->length;
	p = malloc(len);
	if(p == nil)
		error("out of memory");
	if(read(fd, p, len) != len)
		error("can't read key file: %r\n");
	len = convert(&p, len);
	if(pwrite(fd, p, len, 0) != len)
		error("can't write key file: %r\n");
	close(fd);
	exits(nil);
}

int
badname(char *s)
{
	int n;
	Rune r;

	for (; *s != '\0'; s += n) {
		n = chartorune(&r, s);
		if (r == Runeerror)
			return 1;
	}
	return 0;
}

int
convert(char **db, int len)
{
	int i, nu, keydblen, keydboff, keydbaes;
	char *p = *db;

	keydblen = KEYDBLEN;
	keydboff = KEYDBOFF;
	keydbaes = len > 24 && memcmp(p, "AES KEYS", 8) == 0;
	if(keydbaes){
		keydblen += AESKEYLEN;
		keydboff = 8+16;		/* signature[8] + iv[16] */
	}

	len -= keydboff;
	if(len % keydblen){
		fprint(2, "%s: file odd length; not converting %d bytes\n", argv0, len % keydblen);
		len -= len % keydblen;
	}
	len += keydboff;

	if(keydbaes){
		AESstate s;

		/* make sure we have aes key for decryption */
		if(memcmp(okey.aes, zeros, AESKEYLEN) == 0){
			fprint(2, "%s: no aes key in NVRAM\n", argv0);
			exits("no aes key");
		}
		setupAESstate(&s, okey.aes, AESKEYLEN, zeros);
		aesCBCdecrypt((uchar*)p+8, len-8, &s);
	} else {
		DESstate s;
		uchar k[8];

		des56to64((uchar*)okey.des, k);
		setupDESstate(&s, k, zeros);
		desCBCdecrypt((uchar*)p, len, &s);
	}

	nu = 0;
	for(i = keydboff; i < len; i += keydblen) {
		if (badname(&p[i])) {
			fprint(2, "%s: bad name %.30s... - aborting\n", argv0, &p[i]);
			exits("bad name");
		}
		nu++;
	}

	if(verb){
		for(i = keydboff; i < len; i += keydblen)
			print("%s\n", &p[i]);
		exits(nil);
	}

	if(convaes && !keydbaes){
		char *s, *d;

		keydboff = 8+16;
		keydblen += AESKEYLEN;
		len = keydboff + keydblen*nu;
		p = realloc(p, len);
		if(p == nil)
			error("out of memory");
		*db = p;
		s = p + KEYDBOFF + nu*KEYDBLEN;
		d = p + keydboff + nu*keydblen;
		for(i=0; i<nu; i++){
			s -= KEYDBLEN;
			d -= keydblen;
			memmove(d, s, KEYDBLEN);
			memset(d + KEYDBLEN, 0, keydblen-KEYDBLEN);
		}
		keydbaes = 1;
	}

	genrandom((uchar*)p, keydboff);
	if(keydbaes){
		AESstate s;

		memmove(p, "AES KEYS", 8);
		setupAESstate(&s, nkey.aes, AESKEYLEN, zeros);
		aesCBCencrypt((uchar*)p+8, len-8, &s);
	} else {
		DESstate s;
		uchar k[8];

		des56to64((uchar*)nkey.des, k);
		setupDESstate(&s, k, zeros);
		desCBCencrypt((uchar*)p, len, &s);
	}
	return len;
}

void
usage(void)
{
	fprint(2, "usage: %s [-pva] keyfile\n", argv0);
	exits("usage");
}
