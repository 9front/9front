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
int	hmaprepl(Hmap **h, char *key, void *new, void *old, int freekeys);
int	hmapupd(Hmap **h, char *key, void *new);
int	hmapdel(Hmap *h, char *key, void *dst, int freekey);
char*	hmapkey(Hmap *h, char *key);
void	hmapreset(Hmap *h, int freekeys);
