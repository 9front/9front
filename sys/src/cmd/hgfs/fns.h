/* hash */
int Hfmt(Fmt *f);
int hex2hash(char *s, uchar *h);
uvlong hash2qid(uchar *h);
int fhash(int fd, uchar p1[], uchar p2[], uchar h[]);

/* patch */
int fpatchmark(int pfd, char *mark);
int fpatch(int ofd, int bfd, int pfd);

/* zip */
int funzip(int ofd, int zfd, int len);

/* revlog */
int fmktemp(void);
int revlogopen(Revlog *r, char *path, int mode);
void revlogupdate(Revlog *r);
void revlogclose(Revlog *r);
int revlogextract(Revlog *r, int rev, int ofd);
uchar *revhash(Revlog *r, int rev);
int hashrev(Revlog *r, uchar hash[]);
int revlogopentemp(Revlog *r, int rev);

/* info */
Revinfo *loadrevinfo(Revlog *changelog, int rev);

/* tree */
char *nodepath(char *s, char *e, Revnode *nd, int mange);
Revnode *mknode(char *name, uchar *hash, char mode);
Revtree *loadfilestree(Revlog *changelog, Revlog *manifest, Revinfo *ri);
Revtree *loadchangestree(Revlog *changelog, Revlog *manifest, Revinfo *ri);
void closerevtree(Revtree *t);

