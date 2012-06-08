typedef struct Aux Aux;
typedef struct DirEntry DirEntry;

enum
{
	TROOT,
	TADDR,
	TADDRSUB,
	TADDRTX,
	TADDRBALANCE,
};

struct DirEntry
{
	char *name;
	Qid qid;
	int par;
	char *(*walk)(Fid *, char *, Qid *);
	char *(*str)(DirEntry *, Aux *);
	void (*write)(Req *);
	int sub[20];
};

struct Aux
{
	char *addr;
	char *str;
};
