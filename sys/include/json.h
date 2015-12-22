#pragma src "/sys/src/libjson"
#pragma lib "libjson.a"

typedef struct JSONEl JSONEl;
typedef struct JSON JSON;

#pragma varargck type "J" JSON*

enum {
	JSONNull,
	JSONBool,
	JSONNumber,
	JSONString,
	JSONArray,
	JSONObject,
};

struct JSONEl {
	char *name;
	JSON *val;
	JSONEl *next;
};

struct JSON
{
	int t;
	union {
		double n;
		char *s;
		JSONEl *first;
	};
};

JSON*	jsonparse(char *);
void	jsonfree(JSON *);
JSON*	jsonbyname(JSON *, char *);
char*	jsonstr(JSON *);
int	JSONfmt(Fmt*);
void	JSONfmtinstall(void);
