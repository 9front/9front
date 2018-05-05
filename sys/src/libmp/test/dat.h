typedef struct ldint ldint;

struct ldint {
	int n;
	u8int *b;
};

enum {NTEST = 2 * 257 + 32};

#pragma varargck type "L" ldint *
