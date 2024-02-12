typedef struct Group Group;
typedef struct Groups Groups;

struct Group {
	u32int id;
	char *name;
	Group *memb;
	int nmemb;
};

struct Groups {
	char *raw;
	Group *g;
	int ng;
};

int loadgroups(Groups *gs, char *raw);
void freegroups(Groups *gs);
Group *findgroup(Groups *gs, char *name, u32int *id);
Group *findgroupid(Groups *gs, u32int id);
int ingroup(Group *g, u32int id);
