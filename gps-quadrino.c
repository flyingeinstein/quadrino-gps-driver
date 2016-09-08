/* Quadrino GPS I2C driver
 *
 * This driver connects to a Quadrino GPS receiver module and provides
 * a typical NMEA serial interface. This driver should also work with any
 * MultiWii I2C GPS device that used the Eosbandi I2C_GPS_NAV code.
 *
 * Portions of this driver were based on the Ublox 6 I2C GPS driver by
 * Felipe Tonello. Thank you!
 *
 * Copyright (C) 2016 Colin F. MacKenzie <colin@flyingeinstein.com>
 * Portions Copyright (C) 2015 Felipe F. Tonello <eu@felipetonello.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>

#define DEBUG 1

#include "nmea.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.1"
#define DRIVER_DESC "Quadrino GPS I2C driver"

#define QUADRINO_GPS_MAJOR  4

#define QUADRINO_GPS_I2C_ADDRESS 0x20   /* the 7bit I2C address */
#define QUADRINO_GPS_NUM 1 /* Only support 1 GPS at a time */

/* By default u-blox GPS fill its buffer every 1 second (1000 msecs) */
#define READ_TIME 1000

// detection of the board we are connected to
typedef enum {
    OTHER,
    GPS_QUADRINO
} GPSDeviceModel;

static const struct of_device_id gps_quadrino_of_match[];   // defined at end of file


static struct tty_port *quadrino_gps_tty_port;
static struct i2c_client *quadrino_gps_i2c_client;
static int quadrino_gps_is_open;
static struct file *quadrino_gps_filp;

struct quadrino_gps_port {
        struct tty_port port;
        struct mutex port_write_mutex;
};
static struct quadrino_gps_port gps_port;

static void quadrino_gps_read_worker(struct work_struct *private);

static DECLARE_DELAYED_WORK(quadrino_gps_wq, quadrino_gps_read_worker);

static void quadrino_gps_read_worker(struct work_struct *private)
{
   STATUS_REGISTER status;
   GPS_COORDINATES location; 
   GPS_DETAIL detail;

   char sout[256];
   s32 gps_buf_size, buf_size = 0;

   if (!quadrino_gps_is_open)
       return;

   /* check if driver was removed */
   if (!quadrino_gps_i2c_client)
       return;

   // read gps status
   gps_buf_size = i2c_smbus_read_word_data(quadrino_gps_i2c_client, I2C_GPS_STATUS_00);
   if (gps_buf_size < 0) {
       dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't read status from GPS.\n");
       /* try one more time */
       goto end;
   }
   status = *(STATUS_REGISTER*)&gps_buf_size;   // alias the return value as gps status

   if(status.gps2dfix || status.gps3dfix) {
       // read the current time from the device 
       gps_buf_size = i2c_smbus_read_i2c_block_data(quadrino_gps_i2c_client, I2C_GPS_GROUND_SPEED, sizeof(GPS_DETAIL), (u8*)&detail);
       if (gps_buf_size < 0) {
           dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't read gps detail from GPS.\n");
           goto end;
       }

       gps_buf_size = i2c_smbus_read_i2c_block_data(quadrino_gps_i2c_client, I2C_GPS_LOCATION, 8, (u8*)&location);
       if (gps_buf_size < 0) {
           dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't read location from GPS.\n");
           goto end;
       }
   } else {
       // no fix
       memset(&detail, 0, sizeof(detail));
       memset(&location, 0, sizeof(location));
   }


   buf_size = nmea_zda(sout, sizeof(sout), &detail);
   if(buf_size >0) {
      tty_insert_flip_string(quadrino_gps_tty_port, sout, buf_size);
   }

   // format and output nmea GPGGA sentence
   buf_size = nmea_gga(sout, sizeof(sout), &status, &location, &detail);
   if(buf_size >0) { 
       tty_insert_flip_string(quadrino_gps_tty_port, sout, buf_size);
       tty_flip_buffer_push(quadrino_gps_tty_port);
   }
end:
   /* resubmit the workqueue again */
   schedule_delayed_work(&quadrino_gps_wq, msecs_to_jiffies(READ_TIME)); /* 1 sec delay */
}

static int quadrino_gps_serial_open(struct tty_struct *tty, struct file *filp)
{
   if (quadrino_gps_is_open)
       return -EBUSY;

   quadrino_gps_filp = filp;
   quadrino_gps_tty_port = tty->port;
   quadrino_gps_tty_port->low_latency = true; /* make sure we push data immediately */
   quadrino_gps_is_open = true;

   schedule_delayed_work(&quadrino_gps_wq, 0);

   return tty_port_open(&gps_port.port, tty, filp);
}

static void quadrino_gps_serial_close(struct tty_struct *tty, struct file *filp)
{
   if (!quadrino_gps_is_open)
       return;

   /* avoid stop when the denied (in open) file structure closes itself */
   if (quadrino_gps_filp != filp)
       return;

   quadrino_gps_is_open = false;
   quadrino_gps_filp = NULL;
   quadrino_gps_tty_port = NULL;

   tty_port_close(&gps_port.port, tty, filp);
}

static int quadrino_gps_serial_write(struct tty_struct *tty, const unsigned char *buf,
   int count)
{
   if (!quadrino_gps_is_open)
       return 0;

   /* check if driver was removed */
   if (!quadrino_gps_i2c_client)
       return 0;

   /* we don't write back to the GPS so just return same value here */
   return count;
}

static int quadrino_gps_write_room(struct tty_struct *tty)
{
   if (!quadrino_gps_is_open)
       return 0;

   /* check if driver was removed */
   if (!quadrino_gps_i2c_client)
       return 0;

   /* we don't write back to the GPS so just return some value here */
   return 1024;
}

static const struct tty_operations quadrino_gps_serial_ops = {
   .open = quadrino_gps_serial_open,
   .close = quadrino_gps_serial_close,
   .write = quadrino_gps_serial_write,
   .write_room = quadrino_gps_write_room,
};

static struct tty_driver *quadrino_gps_tty_driver;

static struct tty_port_operations null_ops = { };


static int quadrino_gps_probe(struct i2c_client *client,
   const struct i2c_device_id *id)
{
   int result = 0;
   const struct of_device_id *of_id;
   GPSDeviceModel board;

   printk("gps_quadrino: probing devices\n");
   quadrino_gps_i2c_client = client;

   // read what Device Tree (DT) config we matched to hardware
   // this can be used to enable/disable features based on being a QuadrinoGPS or generic MultiWii I2C GPS module
   of_id = of_match_node(gps_quadrino_of_match, quadrino_gps_i2c_client->dev.of_node);
   board = (GPSDeviceModel)of_id->data;

   // based on the DT config we matched inform the user
   if(board == GPS_QUADRINO)
       printk("gps_quadrino: detected Quadrino GPS\n");
   else
       printk("gps_quadrino: detected generic MultiWii GPS\n");

   mutex_init(&gps_port.port_write_mutex);

#if 1
   quadrino_gps_tty_driver = tty_alloc_driver(1,
            TTY_DRIVER_RESET_TERMIOS |
            TTY_DRIVER_REAL_RAW |
            TTY_DRIVER_UNNUMBERED_NODE);
   if (IS_ERR(quadrino_gps_tty_driver))
            return PTR_ERR(quadrino_gps_tty_driver);
#else
   quadrino_gps_tty_driver = alloc_tty_driver(QUADRINO_GPS_NUM);
#endif

   if (!quadrino_gps_tty_driver)
       return -ENOMEM;

   tty_port_init(&gps_port.port);
   gps_port.port.ops = &null_ops;

   quadrino_gps_tty_driver->owner = THIS_MODULE;
   quadrino_gps_tty_driver->driver_name = "gps_quadrino";
   quadrino_gps_tty_driver->name = "ttyGPS";
   quadrino_gps_tty_driver->major = QUADRINO_GPS_MAJOR;
   quadrino_gps_tty_driver->minor_start = 96;
   quadrino_gps_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
   quadrino_gps_tty_driver->subtype = SERIAL_TYPE_NORMAL;
   quadrino_gps_tty_driver->flags = TTY_DRIVER_REAL_RAW;
   quadrino_gps_tty_driver->init_termios = tty_std_termios;
   quadrino_gps_tty_driver->init_termios.c_iflag = IGNCR | IXON;
   quadrino_gps_tty_driver->init_termios.c_oflag = OPOST;
   quadrino_gps_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD |
       HUPCL | CLOCAL;
   quadrino_gps_tty_driver->init_termios.c_ispeed = 9600;
   quadrino_gps_tty_driver->init_termios.c_ospeed = 9600;
   tty_set_operations(quadrino_gps_tty_driver, &quadrino_gps_serial_ops);
   tty_port_link_device(&gps_port.port, quadrino_gps_tty_driver, 0);
   result = tty_register_driver(quadrino_gps_tty_driver);
   if (result) {
       dev_err(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": %s - tty_register_driver failed\n",
           __func__);
       goto err;
   }

   quadrino_gps_filp = NULL;
   quadrino_gps_tty_port = NULL;
   quadrino_gps_is_open = false;

   /* i2c_set_clientdata(client, NULL); */

   dev_info(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": " DRIVER_VERSION ": "
       DRIVER_DESC "\n");

   return result;

err:
   dev_err(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": %s - returning with error %d\n",
       __func__, result);

   put_tty_driver(quadrino_gps_tty_driver);
   tty_port_destroy(&gps_port.port);
   return result;
}

static int quadrino_gps_remove(struct i2c_client *client)
{
   tty_unregister_driver(quadrino_gps_tty_driver);
   put_tty_driver(quadrino_gps_tty_driver);
   tty_port_destroy(&gps_port.port);

   quadrino_gps_i2c_client = NULL;

   return 0;
}



// Device Tree configuration
// This will match on the overlay fragment for gps_quadrino in the gps_quadrino.dts
static const struct of_device_id gps_quadrino_of_match[] = {
        { .compatible = "gps_quadrino", .data = (void*)GPS_QUADRINO },
        { }
};
MODULE_DEVICE_TABLE(of, gps_quadrino_of_match);


static const struct i2c_device_id quadrino_gps_id[] = {
   { "gps_quadrino", 0 }, 
   { }
};
MODULE_DEVICE_TABLE(i2c, quadrino_gps_id);


static struct i2c_driver quadrino_gps_i2c_driver = {
   .driver = {
       .name  = "gps_quadrino",
       .owner = THIS_MODULE,
       .of_match_table = of_match_ptr(gps_quadrino_of_match)
   },
   .id_table  = quadrino_gps_id,
   .probe     = quadrino_gps_probe,
   .remove    = quadrino_gps_remove,
};

module_i2c_driver(quadrino_gps_i2c_driver);

MODULE_AUTHOR("Colin F. MacKenzie <colin@flyingeinstein.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

