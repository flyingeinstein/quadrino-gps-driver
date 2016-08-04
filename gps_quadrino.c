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

#include "registers.h"

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
   STATUS_REGISTER gps_status;
   char sout[256];
   s32 gps_buf_size, buf_size = 0;

   if (!quadrino_gps_is_open)
       return;

   /* check if driver was removed */
   if (!quadrino_gps_i2c_client)
       return;
#if 0
   gps_buf_size = i2c_smbus_read_word_data(quadrino_gps_i2c_client, 0xfd);
   if (gps_buf_size < 0) {
       dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't read register(0xfd) from GPS.\n");
       /* try one more time */
       goto end;
   }

   /* 0xfd is the MSB and 0xfe is the LSB */
   gps_buf_size = ((gps_buf_size & 0xf) << 8) | ((gps_buf_size & 0xf0) >> 8);

   if (gps_buf_size > 0) {

       buf = kcalloc(gps_buf_size, sizeof(*buf), GFP_KERNEL);
       if (!buf) {
           dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't allocate memory.\n");
           /* try one more time */
           goto end;
       }

       do {
           buf_size = i2c_master_recv(quadrino_gps_i2c_client, (char *)buf, gps_buf_size);
           if (buf_size < 0) {
               dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't read data from GPS.\n");
               kfree(buf);
               /* try one more time */
               goto end;
           }

           tty_insert_flip_string(quadrino_gps_tty_port, buf, buf_size);

           gps_buf_size -= buf_size;

           /* There is a small chance that we need to split the data over
              several buffers. If this is the case we must loop */
       } while (unlikely(gps_buf_size > 0));

       tty_flip_buffer_push(quadrino_gps_tty_port);

       kfree(buf);
   }
#else
#if 1
   // read gps status
   gps_buf_size = i2c_smbus_read_word_data(quadrino_gps_i2c_client, I2C_GPS_STATUS_00);
   if (gps_buf_size < 0) {
       dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't read status from GPS.\n");
       /* try one more time */
       goto end;
   }
   gps_status = *(STATUS_REGISTER*)&gps_buf_size;   // alias the return value as gps status

   // output the GPS status
   buf_size = sprintf(sout, "$GPV,%s,%d\n", 
           gps_status.gps3dfix 
               ? "3D"
               : gps_status.gps2dfix 
                   ? "2D"
                   : "NO-FIX",
           gps_status.numsats);
    tty_insert_flip_string(quadrino_gps_tty_port, sout, buf_size);

    // only output GPS location if we have at least a 2D fix
    if(gps_status.gps2dfix) {
       GPS_COORDINATES loc; 
       GPS_DETAIL gps_detail;

       // will need to parse datetime components for NMEA display
       double dow;   // computed day of week
       uint32_t tod; // computed time of day
       uint32_t date, time; // computed days since 1970, and 1/100th seconds since midnight
       int day, month, year, hour, min, sec;

       // must use fixed point techniques to display lat/lon because kernel drivers shouldnt use floating point
       int32_t latp, lats;
       int32_t lonp, lons;
 
       // read the current time from the device 
       gps_buf_size = i2c_smbus_read_i2c_block_data(quadrino_gps_i2c_client, I2C_GPS_GROUND_SPEED, sizeof(GPS_DETAIL), (u8*)&gps_detail);
       if (gps_buf_size < 0) {
           dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't read gps detail from GPS.\n");
           goto end;
       }

      gps_buf_size = i2c_smbus_read_i2c_block_data(quadrino_gps_i2c_client, I2C_GPS_LOCATION, 8, (u8*)&loc);
       if (gps_buf_size < 0) {
           dev_warn(&quadrino_gps_i2c_client->dev, KBUILD_MODNAME ": couldn't read location from GPS.\n");
           goto end;
       }

       // parse the gps week number and tow into date/time
       dow = gps_detail.time/8640000;
       tod = gps_detail.time - dow;
       date = gps_detail.week*52 + gps_detail.time/8640000;
       time = gps_detail.time%8640000;

       // now get components of date and time since we have to print in NMEA format
       year = 1970 + (date/365);
       month = (date % 365) / 30;
       day = (date %30);
       hour = time / 360000;
       min = (time / 6000) % 60;
       sec = (time / 100) % 60;

       // get the integer and decimal portion of the lat/lon as seperate ints
       latp = loc.lat/10000000;
       lonp = loc.lon/10000000;
       lats = abs(loc.lat%10000000);
       lons = abs(loc.lon%10000000);

       // output the GPS location
       // GPGGA,time,lat,N,lon,E,fix,sats,hdop,alt,M,height_geod,M,,*chksum
       buf_size = sprintf(sout, "$GPGGA," "%02d%02d%02d" "%d.%d,%d.%d\n", 
               hour,min,sec,
               latp, lats, lonp, lons);
       tty_insert_flip_string(quadrino_gps_tty_port, sout, buf_size);
    }


    tty_flip_buffer_push(quadrino_gps_tty_port);
#else
    buf_size = sprintf(sout, "$GPV,test,n/a\n");
    tty_insert_flip_string(quadrino_gps_tty_port, sout, buf_size);
    tty_flip_buffer_push(quadrino_gps_tty_port);
#endif
#endif
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

   printk("gps_quadrino: probing devices\n");
   quadrino_gps_i2c_client = client;

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

static const struct i2c_device_id quadrino_gps_id[] = {
   { "gps_quadrino", 0 }, //QUADRINO_GPS_I2C_ADDRESS },
   { }
};

MODULE_DEVICE_TABLE(i2c, quadrino_gps_id);

static struct i2c_driver quadrino_gps_i2c_driver = {
   .driver = {
       .name  = "gps_quadrino",
       .owner = THIS_MODULE,
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
