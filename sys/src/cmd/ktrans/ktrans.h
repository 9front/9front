typedef	struct	Map	Map;
struct	Map {
	char	*roma;
	char	*kana;
	char leadstomore;
};

typedef struct Msg Msg;
struct Msg {
	char code;
	char buf[64];
};

typedef struct Trans Trans;
struct Trans {
	Channel *input;
	Channel *output;
	Channel *dict;
	Channel *done;
	Channel *lang;
};

void	keyproc(void*);
void	launchfs(char*,char*,char*);
int	parselang(char*);
