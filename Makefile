SRCS=cxgbe_i2c.c
KMOD=cxgbe_i2c

CFLAGS+= -I/usr/src/sys/dev/cxgbe
SRCS+= device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
