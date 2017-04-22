#include <u.h>
#include <libc.h>
#include <bio.h>
#include <disk.h>
#include <avl.h>

/* db.c */
typedef struct Db Db;
typedef struct Entry Entry;
struct Entry
{
	Avl;
	char *name;
	struct {
		char *name;
		char *uid;
		char *gid;
		ulong mtime;
		ulong mode;
		int mark;
		vlong length;
	} d;
};


typedef struct Db Db;
struct Db
{
	Avltree *avl;
	int fd;
};
Db *opendb(char*);
int finddb(Db*, char*, Dir*);
void removedb(Db*, char*);
void insertdb(Db*, char*, Dir*);
int markdb(Db*, char*, Dir*);

/* util.c */
void *erealloc(void*, int);
void *emalloc(int);
char *estrdup(char*);
char *atom(char*);
char *unroot(char*, char*);

/* revproto.c */
int revrdproto(char*, char*, char*, Protoenum*, Protowarn*, void*);

