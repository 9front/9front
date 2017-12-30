#include "os.h"
#include <bio.h>
#include <libsec.h>

enum{ ThumbTab = 1<<10 };

static Thumbprint*
tablehead(uchar *hash, Thumbprint *table)
{
	return &table[((hash[0]<<8) + hash[1]) & (ThumbTab-1)];
}

void
freeThumbprints(Thumbprint *table)
{
	Thumbprint *hd, *p, *q;

	if(table == nil)
		return;
	for(hd = table; hd < table+ThumbTab; hd++){
		for(p = hd->next; p && p != hd; p = q){
			q = p->next;
			free(p);
		}
	}
	free(table);
}

int
okThumbprint(uchar *hash, int len, Thumbprint *table)
{
	Thumbprint *hd, *p;

	if(table == nil)
		return 0;
	hd = tablehead(hash, table);
	for(p = hd->next; p; p = p->next){
		if(p->len == len && memcmp(hash, p->hash, len) == 0)
			return 1;
		if(p == hd)
			break;
	}
	return 0;
}

int
okCertificate(uchar *cert, int len, Thumbprint *table)
{
	uchar hash[SHA2_256dlen];
	char thumb[2*SHA2_256dlen+1];

	if(table == nil){
		werrstr("no thumbprints provided");
		return 0;
	}
	if(cert == nil || len <= 0){
		werrstr("no certificate provided");
		return 0;
	}

	sha1(cert, len, hash, nil);
	if(okThumbprint(hash, SHA1dlen, table))
		return 1;

	sha2_256(cert, len, hash, nil);
	if(okThumbprint(hash, SHA2_256dlen, table))
		return 1;

	if(X509digestSPKI(cert, len, sha2_256, hash) < 0)
		return 0;
	if(okThumbprint(hash, SHA2_256dlen, table))
		return 1;

	len = enc64(thumb, sizeof(thumb), hash, SHA2_256dlen);
	while(len > 0 && thumb[len-1] == '=')
		len--;
	thumb[len] = '\0';
	werrstr("sha256=%s", thumb);

	return 0;
}

static int
loadThumbprints(char *file, char *tag, Thumbprint *table, Thumbprint *crltab, int depth)
{
	Thumbprint *hd, *entry;
	char *line, *field[50];
	uchar hash[SHA2_256dlen];
	Biobuf *bin;
	int len, n;

	if(depth > 8){
		werrstr("too many includes, last file %s", file);
		return -1;
	}
	if(access(file, AEXIST) < 0)
		return 0;	/* not an error */
	if((bin = Bopen(file, OREAD)) == nil)
		return -1;
	for(; (line = Brdstr(bin, '\n', 1)) != nil; free(line)){
		if(tokenize(line, field, nelem(field)) < 2)
			continue;
		if(strcmp(field[0], "#include") == 0){
			if(loadThumbprints(field[1], tag, table, crltab, depth+1) < 0)
				goto err;
			continue;
		}
		if(strcmp(field[0], tag) != 0)
			continue;
		if(strncmp(field[1], "sha1=", 5) == 0){
			field[1] += 5;
			len = SHA1dlen;
		} else if(strncmp(field[1], "sha256=", 7) == 0){
			field[1] += 7;
			len = SHA2_256dlen;
		} else {
			continue;
		}
		n = strlen(field[1]);
		if((n != len*2 || dec16(hash, len, field[1], n) != len)
		&& dec64(hash, len, field[1], n) != len){
			werrstr("malformed %s entry in %s: %s", tag, file, field[1]);
			goto err;
		}
		if(crltab && okThumbprint(hash, len, crltab))
			continue;
		hd = tablehead(hash, table);
		if(hd->next == nil)
			entry = hd;
		else {
			if((entry = malloc(sizeof(*entry))) == nil)
				goto err;
			entry->next = hd->next;
		}
		hd->next = entry;
		entry->len = len;
		memcpy(entry->hash, hash, len);
	}
	Bterm(bin);
	return 0;
err:
	free(line);
	Bterm(bin);
	return -1;
}

Thumbprint *
initThumbprints(char *ok, char *crl, char *tag)
{
	Thumbprint *table, *crltab;

	table = crltab = nil;
	if(crl){
		if((crltab = malloc(ThumbTab * sizeof(*crltab))) == nil)
			goto err;
		memset(crltab, 0, ThumbTab * sizeof(*crltab));
		if(loadThumbprints(crl, tag, crltab, nil, 0) < 0)
			goto err;
	}
	if((table = malloc(ThumbTab * sizeof(*table))) == nil)
		goto err;
	memset(table, 0, ThumbTab * sizeof(*table));
	if(loadThumbprints(ok, tag, table, crltab, 0) < 0){
		freeThumbprints(table);
		table = nil;
	}
err:
	freeThumbprints(crltab);
	return table;
}
