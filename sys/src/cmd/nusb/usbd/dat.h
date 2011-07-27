typedef struct Rule Rule;
typedef struct Cond Cond;
typedef struct Dev Dev;

struct Rule {
	char **argv;
	int argc;
	Cond *cond;
	Rule *next;
} *rulefirst, *rulelast;

RWLock rulelock;

struct Cond {
	int field;
	u32int value;
	Cond *and, *or;
};

struct Dev {
	u32int class, vid, did;
};
