#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <dev/cxgbe/common/common.h>

/* Function prototypes */
static void cxgbe_i2c_identify(driver_t *driver, device_t parent);
static int cxgbe_i2c_probe(device_t dev);
static int cxgbe_i2c_attach(device_t dev);
static int cxgbe_i2c_detach(device_t dev);
static int cxgbe_i2c_rw(int wr, struct cdev *dev, struct uio *uio);

static device_method_t cxgbe_i2c_methods[] = {
  /* Device interface */
  DEVMETHOD(device_identify, cxgbe_i2c_identify),
  DEVMETHOD(device_probe,    cxgbe_i2c_probe),
  DEVMETHOD(device_attach,   cxgbe_i2c_attach),
  DEVMETHOD(device_detach,   cxgbe_i2c_detach),

  DEVMETHOD_END
};

struct cxgbe_i2c_softc {
  device_t dev;
  int port_id;
  struct cdev *cdev;
};

static driver_t cxgbe_i2c_driver = {
  "cxgbe_i2c",
  cxgbe_i2c_methods,
  sizeof(struct cxgbe_i2c_softc),
};

static devclass_t cxgbe_i2c_devclass;

MODULE_DEPEND(cxgbe_i2c, t4nex, 1, 1, 1);
DRIVER_MODULE(cxgbe_i2c, t4nex, cxgbe_i2c_driver, cxgbe_i2c_devclass, 0, 0);

static d_read_t      cxgbe_i2c_read;
static d_write_t     cxgbe_i2c_write;

static struct cdevsw cxgbe_i2c_cdevsw = {
  .d_version = D_VERSION,
  .d_flags = D_MEM,
  /*.d_open = cxgbe_i2c_open,
  .d_close = cxgbe_i2c_close,*/
  .d_read = cxgbe_i2c_read,
  .d_write = cxgbe_i2c_write,
  .d_name = "cxgbe_i2c",
};

static void
cxgbe_i2c_identify(driver_t *driver, device_t parent)
{
  device_t dev;
  struct adapter *cx_sc;
  const char *cx_name = NULL;
  int i;

  cx_sc = device_get_softc(parent);
  cx_name = device_get_nameunit(parent);

  /* Only create children if they're not already created */
  if (device_find_child(parent, driver->name, -1) == NULL)
    for_each_port(cx_sc, i){
      if ((dev = device_add_child(parent, driver->name, -1)) == NULL)
        device_printf(parent, "could not add I2C child for %s, port %d\n", cx_name, i);
    }
}

static int
cxgbe_i2c_probe(device_t dev)
{
  char buf[128];
  struct cxgbe_i2c_softc *sc;
  device_t *cx_child_devs;
  int i, cx_child_count;

  if (resource_disabled("cxgbe_i2c", device_get_unit(dev)))
      return (ENXIO);

  sc = device_get_softc(dev);
  sc->dev = dev;
  sc->port_id = 0;

  /* Determine this port's id */
  device_get_children(device_get_parent(dev), &cx_child_devs, &cx_child_count);
  for(i = 0; i < cx_child_count; i++){
    if (device_get_devclass(cx_child_devs[i]) == cxgbe_i2c_devclass) {
      if (cx_child_devs[i] == dev)
        break;
      else
        sc->port_id++;
    }
  }

  snprintf(buf, sizeof(buf), "port %d I2C", sc->port_id);
  device_set_desc_copy(dev, buf);

  return (BUS_PROBE_DEFAULT);
}

static int
cxgbe_i2c_attach(device_t dev)
{
  device_t cx_dev;

  struct cxgbe_i2c_softc *sc;
  struct make_dev_args args;
  const char *cx_name = NULL;
  int cx_unit;
  int error;

  cx_dev = device_get_parent(dev);
  sc = device_get_softc(dev);

  make_dev_args_init(&args);
  args.mda_devsw = &cxgbe_i2c_cdevsw;
  args.mda_uid = UID_ROOT;
  args.mda_gid = GID_WHEEL;
  args.mda_mode = 0660;
  args.mda_si_drv1 = dev;

  cx_name = device_get_nameunit(cx_dev);
  cx_unit = device_get_unit(cx_dev);

  if ((error = make_dev_s(&args, &sc->cdev, "%c%ci2c%d.%d", cx_name[0], cx_name[1], cx_unit, sc->port_id)) != 0) {
    device_printf(dev, "failed to create device %c%ci2c%d.%d (%d)\n", cx_name[0], cx_name[1], cx_unit, sc->port_id, error);
    return (error);
  }

  error = bus_generic_attach(dev);

  return (error);
}

static int
cxgbe_i2c_detach(device_t dev)
{
  struct cxgbe_i2c_softc *sc;
  int error;

  sc = device_get_softc(dev);

  destroy_dev(sc->cdev);
  sc->cdev = NULL;

  error = bus_generic_detach(dev);

  return (error);
}

static int
cxgbe_i2c_read(struct cdev *dev, struct uio *uio, int ioflag __unused)
{
  int rc;
  rc = cxgbe_i2c_rw(0, dev, uio);
  return (rc);
}

static int
cxgbe_i2c_write(struct cdev *dev, struct uio *uio, int ioflag __unused)
{
  int rc;
  rc = cxgbe_i2c_rw(1, dev, uio);
  return (rc);
}

static int cxgbe_i2c_rw(int wr, struct cdev *dev, struct uio *uio){
  device_t i2c_dev;
  device_t cx_dev;

  struct cxgbe_i2c_softc *i2c_sc;
  struct adapter *cx_sc;

  uint16_t addr;
  uint8_t slave;
  uint8_t slave_addr;

  uint8_t buf[8];
  int bytes, rc;

  i2c_dev = dev->si_drv1;
  cx_dev = device_get_parent(i2c_dev);
  i2c_sc = device_get_softc(i2c_dev);
  cx_sc = device_get_softc(cx_dev);

  rc = begin_synchronized_op(cx_sc, NULL, SLEEP_OK | INTR_OK, "t4i2c");
  if (rc)
    return (rc);

  while (uio->uio_resid > 0) {
    addr = uio->uio_offset & 0xffff;
    slave = (addr>>8)&0xff;
    slave_addr = addr&0xff;
    bytes = uio->uio_resid > 8 ? 8 : uio->uio_resid;

    /* if read would cross slave address boundary */
    if ((((addr+bytes)>>8)&0xff) != slave)
      bytes = 0x100 - slave_addr;

    if (!wr) {
      rc = -t4_i2c_rd(cx_sc, cx_sc->mbox, i2c_sc->port_id, slave, slave_addr, bytes, buf);
      if (rc)
        break;

      rc = uiomove(buf, bytes, uio);
      if (rc)
        break;
    } else {
      rc = uiomove(buf, bytes, uio);
      if (rc)
        break;

      rc = -t4_i2c_wr(cx_sc, cx_sc->mbox, i2c_sc->port_id, slave, slave_addr, bytes, buf);
      if (rc)
        break;
    }
  }

  end_synchronized_op(cx_sc, 0);

  return (rc);
}
