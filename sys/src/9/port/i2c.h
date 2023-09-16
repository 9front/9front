typedef struct I2Cbus I2Cbus;
typedef struct I2Cdev I2Cdev;

struct I2Cbus
{
	char	*name;
	int	speed;

	void	*ctlr;
	int	(*init)(I2Cbus *bus);
	int	(*io)(I2Cdev *dev, uchar *pkt, int olen, int ilen);

	int	probed;
	QLock;
};

struct I2Cdev
{
	I2Cbus	*bus;

	int	a10;
	int	addr;
	int	subaddr;
	ulong	size;
};

/*
 * Register busses (controllers) and devices (addresses)
 */
extern void addi2cbus(I2Cbus *bus);
extern void addi2cdev(I2Cdev *dev);

/*
 * Look-up busses and devices by name and address
 */
extern I2Cbus* i2cbus(char *name);
extern I2Cdev* i2cdev(I2Cbus *bus, int addr);

/*
 * generic I/O
 */
extern int i2cbusio(I2Cdev *dev, uchar *pkt, int olen, int ilen);
extern int i2crecv(I2Cdev *dev, void *data, int len, vlong addr);
extern int i2csend(I2Cdev *dev, void *data, int len, vlong addr);

/*
 * common I/O for SMbus
 */
extern int i2cquick(I2Cdev *dev, int rw);
extern int i2crecvbyte(I2Cdev *dev);
extern int i2csendbyte(I2Cdev *dev, uchar b);
extern int i2creadbyte(I2Cdev *dev, ulong addr);
extern int i2cwritebyte(I2Cdev *dev, ulong addr, uchar b);
extern int i2creadword(I2Cdev *dev, ulong addr);
extern int i2cwriteword(I2Cdev *dev, ulong addr, ushort w);
extern vlong i2cread32(I2Cdev *dev, ulong addr);
extern vlong i2cwrite32(I2Cdev *dev, ulong addr, ulong u);
