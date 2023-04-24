enum {
	IRQfiq		= -1,

	PPI		= 16,
	SPI		= 32,

	IRQcntps	= PPI+13,
	IRQcntpns	= PPI+14,

	IRQlcdif	= SPI+5,
	IRQvpug1	= SPI+7,
	IRQvpug2	= SPI+8,

	IRQusdhc1	= SPI+22,
	IRQusdhc2	= SPI+23,

	IRQuart1	= SPI+26,
	IRQuart2	= SPI+27,
	IRQuart3	= SPI+28,
	IRQuart4	= SPI+29,

	IRQi2c1		= SPI+35,
	IRQi2c2		= SPI+36,
	IRQi2c3		= SPI+37,
	IRQi2c4		= SPI+38,

	IRQrdc		= SPI+39,

	IRQusb1		= SPI+40,
	IRQusb2		= SPI+41,

	IRQsctr0	= SPI+47,
	IRQsctr1	= SPI+48,

	IRQgpio1l	= SPI+64,
	IRQgpio1h	= SPI+65,
	IRQgpio2l	= SPI+66,
	IRQgpio2h	= SPI+67,
	IRQgpio3l	= SPI+68,
	IRQgpio3h	= SPI+69,
	IRQgpio4l	= SPI+70,
	IRQgpio4h	= SPI+71,
	IRQgpio5l	= SPI+72,
	IRQgpio5h	= SPI+73,

	IRQpci2		= SPI+74,

	IRQsai2		= SPI+96,

	IRQenet1	= SPI+118,

	IRQpci1		= SPI+122,
};

#define BUSUNKNOWN (-1)
#define PCIWINDOW	0
#define	PCIWADDR(x)	(PADDR(x)+PCIWINDOW)
