/* sub */
void*	emalloc(int n);
char*	estrdup(char *s);

void	nstrcpy(char *to, char *from, int n);

Key*	addkey(Key *h, char *key, char *val);
Key*	delkey(Key *h, char *key);
Key*	getkey(Key *h, char *key);
char*	lookkey(Key *k, char *key);
Key*	parsehdr(char *s);
char*	unquote(char *s, char **ps);

/* url */
#pragma	varargck type "U" Url*
#pragma varargck type "E" Str2

int	Efmt(Fmt*);
int	Hfmt(Fmt*);
int	Ufmt(Fmt*);
char*	Upath(Url *);
Url*	url(char *s, Url *b);
Url*	saneurl(Url *u);
int	matchurl(Url *u, Url *s);
void	freeurl(Url *u);

/* idn */
char*	idn2utf(char *name, char *buf, int nbuf);
char*	utf2idn(char *name, char *buf, int nbuf);

/* buq */
int	buread(Buq *q, void *v, int l);
int	buwrite(Buq *q, void *v, int l);
void	buclose(Buq *q, char *error);
Buq*	bualloc(int limit);
void	bufree(Buq *q);

void	bureq(Buq *q, Req *r);
void	buflushreq(Buq *q, Req *r);

/* http */
int authenticate(Url *u, Url *ru, char *method, char *s);
void flushauth(Url *u, char *t);
void http(char *m, Url *u, Key *shdr, Buq *qbody, Buq *qpost);
