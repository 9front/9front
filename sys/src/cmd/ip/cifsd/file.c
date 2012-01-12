#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

typedef struct Opl Opl;
struct Opl
{
	int ref;
	ulong hash;
	char *path;
	Opl *next;
	File *locked;
	int dacc;
	int sacc;
	int delete;
};

static Opl *locktab[64];

static Opl*
getopl(char **path, int (*namecmp)(char *, char *), int dacc, int sacc)
{
	Opl *opl, **pp;
	ulong h;

	h = namehash(*path);
	for(pp = &locktab[h % nelem(locktab)]; *pp; pp=&((*pp)->next)){
		opl = *pp;
		if(namecmp(opl->path, *path))
			continue;
		if(sacc == FILE_SHARE_COMPAT){
			if(sacc != opl->sacc)
				return nil;
			if((opl->dacc | dacc) & WRITEMASK)
				return nil;
		} else {
			if(opl->sacc == FILE_SHARE_COMPAT)
				return nil;
			if((dacc & READMASK) && (opl->sacc & FILE_SHARE_READ)==0)
				return nil;
			if((dacc & WRITEMASK) && (opl->sacc & FILE_SHARE_WRITE)==0)
				return nil;
			if((dacc & FILE_DELETE) && (opl->sacc & FILE_SHARE_DELETE)==0)
				return nil;
		}
		opl->ref++;
		if(strcmp(opl->path, *path)){
			free(*path);
			*path = strdup(opl->path);
		}
		return opl;
	}

	opl = mallocz(sizeof(*opl), 1);
	opl->ref = 1;
	opl->hash = h;
	opl->dacc = dacc;
	opl->sacc = sacc;
	*pp = opl;
	return opl;
}

static void
putopl(Opl *opl)
{
	Opl **pp;

	if(opl==nil || --opl->ref)
		return;
	for(pp = &locktab[opl->hash % nelem(locktab)]; *pp; pp=&((*pp)->next)){
		if(*pp == opl){
			*pp = opl->next;
			opl->next = nil;
			break;
		}
	}
	if(opl->path && opl->delete){
		if(debug)
			fprint(2, "remove on close: %s\n", opl->path);
		if(remove(opl->path) < 0)
			logit("remove %s: %r", opl->path);
	}
	free(opl->path);
	free(opl);
}

File*
createfile(char *path, int (*namecmp)(char *, char *),
	int dacc, int sacc, int cdisp, int copt, vlong csize, int fattr, int *pact, Dir **pdir, int *perr)
{
	int err, act, fd, mode, perm, isdir, delete;
	Opl *o;
	File *f;
	Dir *d;

	o = nil;
	f = nil;
	d = nil;
	fd = -1;
	path = strdup(path);

	if(copt & FILE_OPEN_BY_FILE_ID){
unsup:
		err = STATUS_NOT_SUPPORTED;
		goto out;
	}
	if((o = getopl(&path, namecmp, dacc, sacc)) == nil){
		err = STATUS_SHARING_VIOLATION;
		goto out;
	}
	mode = -1;
	if(dacc & READMASK)
		mode += 1;
	if(dacc & WRITEMASK)
		mode += 2;
	delete = isdir = 0;
	if(d = xdirstat(&path, namecmp)){
		if(mode >= 0 && d->type != '/' && d->type != 'M'){
noaccess:
			err = STATUS_ACCESS_DENIED;
			goto out;
		}

		isdir = (d->qid.type & QTDIR) != 0;
		switch(cdisp){
		case FILE_SUPERSEDE:
			act = FILE_SUPERSEDED;
			if(remove(path) < 0){
				logit("remove: %r");
oserror:
				err = smbmkerror();
				goto out;
			}
			goto docreate;
		case FILE_OVERWRITE:
		case FILE_OVERWRITE_IF:
			act = FILE_OVERWRITTEN;
			if(isdir || (mode != OWRITE && mode != ORDWR))
				goto noaccess;
			d->length = 0;
			mode |= OTRUNC;
			break;
		case FILE_OPEN:
		case FILE_OPEN_IF:
			act = FILE_OPEND;
			break;
		case FILE_CREATE:
			err = STATUS_OBJECT_NAME_COLLISION;
			goto out;
		default:
			goto unsup;
		}
		if((copt & FILE_DIRECTORY_FILE) && !isdir)
			goto noaccess;
		if((copt & FILE_NON_DIRECTORY_FILE) && isdir){
			err = STATUS_FILE_IS_A_DIRECTORY;
			goto out;
		}
		if(copt & FILE_DELETE_ON_CLOSE){
			if(isdir || (dacc & FILE_DELETE)==0)
				goto noaccess;
			delete = 1;
		}
		if(mode >= 0 && !isdir)
			if((fd = open(path, mode)) < 0)
				goto oserror;
	} else {
		switch(cdisp){
		case FILE_SUPERSEDE:
		case FILE_CREATE:
		case FILE_OPEN_IF:
		case FILE_OVERWRITE_IF:
			act = FILE_CREATED;
			break;
		case FILE_OVERWRITE:
		case FILE_OPEN:
			err = smbmkerror();
			goto out;
		default:
			goto unsup;
		}

docreate:
		perm = 0666;
		if(fattr & ATTR_READONLY)
			perm &= ~0222;
		if(copt & FILE_DIRECTORY_FILE){
			perm |= DMDIR | 0111;
			mode = OREAD;
			isdir = 1;
		}
		if(mode < 0)
			mode = OREAD;
		if(copt & FILE_DELETE_ON_CLOSE){
			if(isdir || (dacc & FILE_DELETE)==0)
				goto noaccess;
			delete = 1;
		}
		if((fd = create(path, mode, perm)) < 0){
			char *name, *base;
			Dir *t;

			err = smbmkerror();
			if(!splitpath(path, &base, &name))
				goto out;
			if((t = xdirstat(&base, namecmp)) == nil){
				free(base); free(name);
				goto out;
			}
			free(t);
			free(path); path = conspath(base, name);
			free(base); free(name);
			if((fd = create(path, mode, perm)) < 0)
				goto oserror;
		}
		if(csize > 0 && !isdir){
			Dir nd;

			nulldir(&nd);
			nd.length = csize;
			if(dirfwstat(fd, &nd) < 0)
				goto oserror;
		}
		if(pdir)
			if((d = dirfstat(fd)) == nil)
				goto oserror;
		if(isdir){
			close(fd);
			fd = -1;
		}
	}

	f = mallocz(sizeof(*f), 1);
	f->ref = 1;
	f->fd = fd; fd = -1;
	f->rtype = FileTypeDisk;
	f->dacc = dacc;
	o->delete |= delete;
	if(o->path == nil){
		o->path = path;
		path = nil;
	}
	f->path = o->path;
	f->aux = o; o = nil;
	if(pact)
		*pact = act;
	if(pdir){
		*pdir = d;
		d = nil;
	}
	err = 0;

out:
	if(perr)
		*perr = err;
	if(fd >= 0)
		close(fd);
	free(path);
	putopl(o);
	free(d);

	return f;
}

Dir*
statfile(File *f)
{
	if(f == nil)
		return nil;
	if(f->fd >= 0)
		return dirfstat(f->fd);
	else
		return dirstat(f->path);
}

int
lockfile(File *f)
{
	Opl *opl = f->aux;
	if(opl->locked && opl->locked != f)
		return 0;
	opl->locked = f;
	return 1;
}

void
deletefile(File *f, int delete)
{
	Opl *opl = f->aux;
	if(opl->delete == delete)
		return;
	opl->delete = delete;
}

int
deletedfile(File *f)
{
	Opl *opl = f->aux;
	return opl->delete;
}


void
putfile(File *f)
{
	Opl *opl;

	if(f == nil || --f->ref)
		return;
	if(f->fd >= 0)
		close(f->fd);
	opl = f->aux;
	if(opl->locked == f)
		opl->locked = nil;
	putopl(opl);
	free(f);
}

