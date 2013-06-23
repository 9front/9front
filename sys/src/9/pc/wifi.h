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

	int	rsnelen;
	uchar	rsne[258];
	Wkey	txkey[1];
	Wkey	rxkey[5];

	/* stuff from beacon */
	int	ival;
	int	cap;
	int	aid;
	int	channel;
	long	lastseen;
	int	brsnelen;
	uchar	brsne[258];
};

struct Wifi
{
	Ether	*ether;

	int	debug;

	Queue	*iq;
	char	*status;
	Ref	txseq;
	void	(*transmit)(Wifi*, Wnode*, Block*);

	char	essid[Essidlen+2];
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
