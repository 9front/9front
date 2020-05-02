#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static int
run9fs(char *arg)
{
	char buf[1024], *argv[3], *s;
	Waitmsg *w;
	int fd, pid;

	switch(pid = rfork(RFCFDG|RFREND|RFPROC)){
	case -1:
		return -1;
	case 0:
		open("/dev/null", ORDWR);
		snprint(buf, sizeof(buf), "/sys/log/%s", progname);
		if((fd = open(buf, OWRITE)) >= 0)
			seek(fd, 0, 2);
		else
			fd = 0;
		dup(fd, 1);
		dup(fd, 2);
		argv[0] = "/bin/9fs";
		argv[1] = arg;
		argv[2] = 0;
		exec(argv[0], argv);
		exits("failed to exec 9fs");
	}
	for (;;) {
		if((w = wait()) == nil)
			return -1;
		if (w->pid == pid)
			break;
		free(w);
	}
	if(w->msg[0]){
		if(s = strchr(w->msg, ':'))
			s = s+1;
		else
			s = w->msg;
		werrstr("%s", s);
		free(w);
		return -1;
	} else {
		free(w);
		return 0;
	}
}

static Share *shares;

Share*
mapshare(char *path)
{
	char *tmp, *tmp2, *name, *root, *service, *fsname, *remark;
	int stype;
	Share *s;

	if(name = strrchr(path, '/'))
		name++;
	else if(name = strrchr(path, '\\'))
		name++;
	else
		name = path;
	if(name==nil || *name==0 || *name=='.' || strchrs(name, "\\* ") || strstr(name, ".."))
		return nil;
	root = tmp = smprint("/n/%s", name);
	name = strtr(strrchr(root, '/')+1, tolowerrune);
	service = "A:";
	stype = STYPE_DISKTREE;
	fsname = "9fs";
	remark = tmp2 = smprint("9fs %s; cd %s", name, root);
	if(!strcmp(name, "local")){
		root = "/";
		fsname = "local";
		remark = "The standard namespace";
	}
	if(!strcmp(name, "ipc$")){
		root = "/dev/null";
		name = "IPC$";
		fsname = "";
		service = "IPC";
		stype = STYPE_IPC;
		remark = "The IPC service";
	}

	for(s = shares; s; s=s->next)
		if(!strcmp(s->name, name))
			goto out;

	logit("mapshare %s -> %s %s %s", path, service, name, root);

	if(!strcmp(service, "A:") && (stype == STYPE_DISKTREE)){
		if(!strcmp(fsname, "9fs") && (run9fs(name) < 0)){
			logit("9fs %s: %r", name);
			goto out;
		}
	}

	s = malloc(sizeof(*s));
	s->service = strdup(service);
	s->stype = stype;

	s->name = strdup(name);
	s->root = strdup(root);

	s->remark = strdup(remark);
	s->fsname = strdup(fsname);
	s->namelen = 255;
	s->sectorsize = 0x200;
	s->blocksize = 0x2000;
	s->allocsize = 0;
	s->freesize = s->blocksize;

	unixidmap(s);

	s->next = shares;
	shares = s;

out:
	free(tmp);
	free(tmp2);
	return s;
}
