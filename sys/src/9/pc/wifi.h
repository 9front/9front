typedef struct Wnode Wnode;
typedef struct Wifi Wifi;

typedef struct Wifipkt Wifipkt;

struct Wifipkt
{
	uchar	fc[2];
	uchar	dur[2];
	uchar	a1[Eaddrlen];
	uchar	a2[Eaddrlen];
	uchar	a3[Eaddrlen];
	uchar	seq[2];
};

enum {
	WIFIHDRSIZE = 2+2+3*6+2,
};

struct Wnode
{
	uchar	bssid[Eaddrlen];
	char	ssid[32+2];
	int	ival;
	int	cap;
	int	aid;
	int	channel;
	long	lastseen;
};

struct Wifi
{
	Ether	*ether;

	Queue	*iq;
	char	*status;
	Ref	txseq;
	void	(*transmit)(Wifi*, Wnode*, Block*);

	char	essid[32+2];
	Wnode	*bss;

	Wnode	node[32];
};

Wifi *wifiattach(Ether *ether, void (*transmit)(Wifi*, Wnode*, Block*));
void wifiiq(Wifi*, Block*);

long wifistat(Wifi*, void*, long, ulong);
long wifictl(Wifi*, void*, long);

