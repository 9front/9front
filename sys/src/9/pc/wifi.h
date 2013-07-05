typedef struct Wkey Wkey;
typedef struct Wnode Wnode;
typedef struct Wifi Wifi;
typedef struct Wifipkt Wifipkt;

enum {
	Essidlen = 32,
};

/* cipher */
enum {
	TKIP	= 1,
	CCMP	= 2,
};

struct Wkey
{
	int	cipher;
	int	len;
	uchar	key[32];
	uvlong	tsc;
};

struct Wnode
{
	uchar	bssid[Eaddrlen];
	char	ssid[Essidlen+2];

	char	*status;

	int	rsnelen;
	uchar	rsne[258];
	Wkey	txkey[1];
	Wkey	rxkey[5];

	int	aid;		/* association id */
	ulong	lastsend;
	ulong	lastseen;

	/* stuff from beacon */
	int	ival;
	int	cap;
	int	channel;
	int	brsnelen;
	uchar	brsne[258];
};

struct Wifi
{
	Ether	*ether;

	int	debug;

	Queue	*iq;
	ulong	watchdog;
	Ref	txseq;
	void	(*transmit)(Wifi*, Wnode*, Block*);

	/* for searching */
	uchar	bssid[Eaddrlen];
	char	essid[Essidlen+2];

	/* effective base station */
	Wnode	*bss;

	Wnode	node[32];
};

struct Wifipkt
{
	uchar	fc[2];
	uchar	dur[2];
	uchar	a1[Eaddrlen];
	uchar	a2[Eaddrlen];
	uchar	a3[Eaddrlen];
	uchar	seq[2];
};

Wifi *wifiattach(Ether *ether, void (*transmit)(Wifi*, Wnode*, Block*));
void wifiiq(Wifi*, Block*);

long wifistat(Wifi*, void*, long, ulong);
long wifictl(Wifi*, void*, long);
