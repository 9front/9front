/* pack */
int unpack(uchar *b, uchar *p, uchar *e, char *f, ...);
int vunpack(uchar *b, uchar *p, uchar *e, char *f, va_list a);
int pack(uchar *b, uchar *p, uchar *e, char *f, ...);
int vpack(uchar *b, uchar *p, uchar *e, char *f, va_list a);

/* error */
int smbmkerror(void);
int doserror(int err);

/* util */
void logit(char *fmt, ...);
#pragma varargck argpos logit 1
char *getremote(char *dir);
char *conspath(char *base, char *name);
int splitpath(char *path, char **base, char **name);
void dumphex(char *s, uchar *h, uchar *e);
void todatetime(long time, int *pdate, int *ptime);
long fromdatetime(int date, int time);
vlong tofiletime(long time);
long fromfiletime(vlong filetime);
int filesize32(vlong);
vlong allocsize(vlong size, int blocksize);
int extfileattr(Dir *d);
int dosfileattr(Dir *d);
ulong namehash(char *s);
char *strtr(char *s, Rune (*tr)(Rune));
char *strchrs(char *s, char *c);
int smbstrpack8(uchar *, uchar *p, uchar *e, void *arg);
int smbstrpack16(uchar *b, uchar *p, uchar *e, void *arg);
int smbstrunpack8(uchar *, uchar *p, uchar *e, void *arg);
int smbstrunpack16(uchar *b, uchar *p, uchar *e, void *arg);
int smbnamepack8(uchar *b, uchar *p, uchar *e, void *arg);
int smbnamepack16(uchar *b, uchar *p, uchar *e, void *arg);
int smbnameunpack8(uchar *b, uchar *p, uchar *e, void *arg);
int smbnameunpack16(uchar *b, uchar *p, uchar *e, void *arg);
int smbuntermstrpack8(uchar *b, uchar *p, uchar *e, void *arg);
int smbuntermstrpack16(uchar *b, uchar *p, uchar *e, void *arg);
int smbuntermnamepack8(uchar *b, uchar *p, uchar *e, void *arg);
int smbuntermnamepack16(uchar *b, uchar *p, uchar *e, void *arg);

/* smb */
void smbcmd(Req *r, int cmd, uchar *h, uchar *p, uchar *e);

/* share */
Share *mapshare(char *path);

/* rap */
void transrap(Trans *t);

/* tree */
Tree *connecttree(char *service, char *path, int *perr);
int disconnecttree(int tid);
void logoff(void);

Tree *gettree(int tid);
int newfid(Tree *t, File *f);
void delfid(Tree *t, int fid);
File *getfile(int tid, int fid, Tree **ptree, int *perr);
char *getpath(int tid, char *name, Tree **ptree, int *perr);

int newsid(Tree *t, Find *f);
void delsid(Tree *t, int sid);
Find *getfind(int tid, int sid, Tree **ptree, int *perr);

/* file */
File* createfile(char *path, int (*namecmp)(char *, char *),
	int dacc, int sacc, int cdisp, int copt, vlong csize, int fattr, int *pact, Dir **pdir, int *perr);
Dir* statfile(File *f);
void putfile(File *f);
int lockfile(File *f);
void deletefile(File *f, int delete);
int deletedfile(File *f);

/* find */
Find *openfind(char *path, int (*namecmp)(char *, char *),
	int attr, int withdot, int *perr);
int matchattr(Dir *d, int s);
int readfind(Find *f, int i, Dir **dp);
void putfind(Find *f);

/* dir */
int xdirread(char **path, int (*namecmp)(char *, char *), Dir **d);
Dir *xdirstat(char **path, int (*namecmp)(char *, char *));
void xdirflush(char *path, int (*namecmp)(char *, char *));
