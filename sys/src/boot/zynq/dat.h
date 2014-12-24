enum {
	DHCPTIMEOUT = 2000,
	ARPTIMEOUT = 1000,
	TFTPTIMEOUT = 10000,
	
	TZERO = 0x80000,
	CONFSIZE = 65536,
	CONF = TZERO - CONFSIZE,
};

#define nelem(x) (sizeof(x)/sizeof(*(x)))
