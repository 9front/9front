#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>
#include <authsrv.h>
#include "authcmdlib.h"

static uchar zeros[16];

int
readfile(char *file, char *buf, int n)
{
	int fd;

	fd = open(file, OREAD);
	if(fd < 0){
		werrstr("%s: %r", file);
		return -1;
	}
	n = read(fd, buf, n);
	close(fd);
	return n;
}

int
writefile(char *file, char *buf, int n)
{
	int fd;

	fd = open(file, OWRITE);
	if(fd < 0)
		return -1;
	n = write(fd, buf, n);
	close(fd);
	return n;
}

char*
finddeskey(char *db, char *user, char *key)
{
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/key", db, user);
	if(readfile(filename, key, DESKEYLEN) != DESKEYLEN)
		return nil;
	return key;
}

uchar*
findaeskey(char *db, char *user, uchar *key)
{
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/aeskey", db, user);
	if(readfile(filename, (char*)key, AESKEYLEN) != AESKEYLEN)
		return nil;
	return key;
}

int
findkey(char *db, char *user, Authkey *key)
{
	int ret;

	memset(key, 0, sizeof(Authkey));
	ret = findaeskey(db, user, key->aes) != nil;
	if(ret && tsmemcmp(key->aes, zeros, AESKEYLEN) != 0){
		char filename[Maxpath];

		snprint(filename, sizeof filename, "%s/%s/pakhash", db, user);
		if(readfile(filename, (char*)key->pakhash, PAKHASHLEN) != PAKHASHLEN)
			authpak_hash(key, user);
	}
	ret |= finddeskey(db, user, key->des) != nil;
	return ret;
}

char*
findsecret(char *db, char *user, char *secret)
{
	int n;
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/secret", db, user);
	if((n = readfile(filename, secret, SECRETLEN-1)) <= 0)
		return nil;
	secret[n]=0;
	return secret;
}

char*
setdeskey(char *db, char *user, char *key)
{
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/key", db, user);
	if(writefile(filename, key, DESKEYLEN) != DESKEYLEN)
		return nil;
	return key;
}

uchar*
setaeskey(char *db, char *user, uchar *key)
{
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/aeskey", db, user);
	if(writefile(filename, (char*)key, AESKEYLEN) != AESKEYLEN)
		return nil;
	return key;
}

int
setkey(char *db, char *user, Authkey *key)
{
	int ret;

	ret = setdeskey(db, user, key->des) != nil;
	if(tsmemcmp(key->aes, zeros, AESKEYLEN) != 0)
		ret |= setaeskey(db, user, key->aes) != nil;
	return ret;
}

char*
setsecret(char *db, char *user, char *secret)
{
	char filename[Maxpath];

	snprint(filename, sizeof filename, "%s/%s/secret", db, user);
	if(writefile(filename, secret, strlen(secret)) != strlen(secret))
		return nil;
	return secret;
}
