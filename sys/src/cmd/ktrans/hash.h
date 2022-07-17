typedef union Hkey Hkey;
union Hkey {
	void *p;
	int v;
};

typedef struct Hmap Hmap;
struct Hmap {
	int nbs;
	int nsz;

	int len;
	int cap;
	uchar *nodes;
};

Hmap*	hmapalloc(int nbuckets, int size);
int	hmapget(Hmap *h, char *key, void *dst);
int	hmapset(Hmap **h, char *key, void *new, void *old);
int	hmapdel(Hmap *h, char *key, void *dst, int freekey);
void	hmapfree(Hmap *h, int freekeys);
char*	hmapkey(Hmap *h, char *key);
void	hmapreset(Hmap *h, int freekeys);
