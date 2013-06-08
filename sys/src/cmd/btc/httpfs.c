#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <String.h>
#include "dat.h"
#include "json.h"

void gofs(void);

char *
graburl(char *url)
{
	int fd, fd2, n, rc, size;
	char buf[2048];
	char *res;
	
	fd = open("/mnt/web/clone", ORDWR);
	if(fd < 0)
		return nil;
	if(read(fd, buf, 512) < 0){
		close(fd);
		return nil;
	}
	n = atoi(buf);
	sprint(buf, "url %s", url);
	if(write(fd, buf, strlen(buf)) < 0){
		close(fd);
		return nil;
	}
	sprint(buf, "/mnt/web/%d/body", n);
	fd2 = open(buf, OREAD);
	if(fd2 < 0){
		close(fd);
		return nil;
	}
	size = 0;
	res = nil;
	while((rc = readn(fd2, buf, sizeof buf)) > 0){
		res = realloc(res, size + rc + 1);
		memcpy(res + size, buf, rc);
		size += rc;
		res[size] = 0;
	}
	close(fd);
	close(fd2);
	if(rc < 0){
		free(res);
		return nil;
	}
	return res;
}

static void
parsetx(String *str, JSON *j, JSON *l)
{
	JSONEl *e;
	JSON *k;
	char buf[512];

	for(e = j->first; e != nil; e = e->next){
		k = jsonbyname(e->val, "prev_out");
		sprint(buf, "%s %lld ", jsonstr(jsonbyname(k, "addr")), (vlong)jsonbyname(k, "value")->n);
		s_append(str, buf);
	}
	s_append(str, "| ");
	for(e = l->first; e != nil; e = e->next){
		sprint(buf, "%s %lld ", jsonstr(jsonbyname(e->val, "addr")), (vlong)jsonbyname(e->val, "value")->n);
		s_append(str, buf);
	}
}

char *
balancestr(DirEntry *, Aux *a)
{
	char buf[512];

	sprint(buf, "http://blockchain.info/q/addressbalance/%s", a->addr);
	return graburl(buf);
}

char *
blocksstr(DirEntry *, Aux *)
{
	return graburl("http://blockchain.info/q/getblockcount");
}

char *
txstr(DirEntry *, Aux *a)
{
	char *s;
	char buf[512];
	JSON *j, *k;
	JSONEl *e;
	String *str;

	sprint(buf, "http://blockchain.info/rawaddr/%s", a->addr);
	s = graburl(buf);
	if(s == nil)
		return nil;
	j = jsonparse(s);
	free(s);
	if(j == nil)
		return nil;
	str = s_new();
	k = jsonbyname(j, "txs");
	if(k == nil)
		goto err;
	for(e = k->first; e != nil; e = e->next){
		sprint(buf, "%d %s %d ", (int)(jsonbyname(e->val, "time")->n), jsonstr(jsonbyname(e->val, "hash")), (int)(jsonbyname(e->val, "block_height")->n));
		s_append(str, buf);
		parsetx(str, jsonbyname(e->val, "inputs"), jsonbyname(e->val, "out"));
		s_putc(str, '\n');
	}
	s_terminate(str);
	s = str->base;
	free(str);
	jsonfree(j);
	return s;
err:
	s_free(str);
	jsonfree(j);
	return nil;
}

void
threadmain()
{
	gofs();
}
