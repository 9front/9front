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

	long	lastseen;

	int	aid;
};

struct Wifi
{
	Ether	*ether;

	Queue	*iq;
	char	*status;
	void	(*transmit)(Wifi*, Wnode*, Block*);

	Wnode	node[16];
	Wnode	*bss;

	uint	txseq;
	char	essid[32+2];
};

Wifi *wifiattach(Ether *ether, void (*transmit)(Wifi*, Wnode*, Block*));
void wifiiq(Wifi*, Block*);

long wifistat(Wifi*, void*, long, ulong);
long wifictl(Wifi*, void*, long);

