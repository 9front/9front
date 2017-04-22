#pragma lib "libavl.a"
#pragma src "/sys/src/libavl"

typedef struct Avl Avl;
typedef struct Avltree Avltree;

struct Avl {
	Avl *c[2];
	Avl *p;
	schar balance;
};

struct Avltree {
	int (*cmp)(Avl*, Avl*);
	Avl *root;
};

Avltree *avlinit(Avltree*, int(*)(Avl*, Avl*));
Avltree *avlcreate(int(*)(Avl*, Avl*));
Avl *avllookup(Avltree*, Avl*, int);
Avl *avldelete(Avltree*, Avl*);
Avl *avlinsert(Avltree*, Avl*);
Avl *avlmin(Avltree*);
Avl *avlmax(Avltree*);
Avl *avlnext(Avl*);
Avl *avlprev(Avl*);
