typedef struct JSONEl JSONEl;
typedef struct JSON JSON;

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
