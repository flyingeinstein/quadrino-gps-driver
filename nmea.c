
#include "nmea.h"

#if !defined(__KERNEL__)
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <memory.h>
#else
#include <linux/kernel.h>
#include <linux/module.h>
#endif


int nmea_checksum(char* nmea_sentence, int* output_length, int add_lf)
{
    // see this page for reference on computing nmea checksums
    // https://rietman.wordpress.com/2008/09/25/how-to-calculate-the-nmea-checksum/
    char* p = nmea_sentence;
    int checksum=0;
    if(*p=='$') p++;
    while(*p)
        checksum ^= *p++;
    *p++ = '*';
    p += sprintf(p, add_lf ? "%02x\n" : "%02x", checksum);
    if(output_length !=NULL)
        *output_length = p-nmea_sentence;
    return checksum;
}


void degrees2dms(long micro_degrees, geodms* geo)
{
    /* Must use fixed point techniques to display lat/lon because kernel
     * drivers shouldnt use floating point.
     *
     * Portions taken from http://forum.arduino.cc/index.php?topic=111549.0
     *
     * Original code from link converted to degrees, minutes, seconds. We have to
     * convert to degrees, minutes then fractional minutes. We also use an extra
     * digit of precision than this example.
     *
     *   long micro_degrees = latitude;
     *   int  degrees = micro_degrees / 1000000L ;      // degrees = 123
     *   micro_degrees -= 1000000L * degrees ;          // micro_degrees = 456789
     *   long  milli_minutes = micro_degrees * 3 / 50 ; // miili_minutes = 27407
     *   int minutes = milli_minutes / 1000 ;           // minutes = 27
     *   milli_minutes -= 1000L * minutes ;             // milli_minutes = 407
     *   int centi_seconds = milli_minutes * 6 ;        // centi_seconds = 2442
     *   int seconds = deci_seconds / 10 ;              // seconds = 24
     *   centi_seconds -= 10 * seconds ;                // centi_seconds = 42
     */

    // ex. 1234567890
    long milli_minutes;
    int degrees, minutes;
    degrees = micro_degrees / 10000000L ;      // degrees = 123
    micro_degrees -= 10000000L * degrees ;          // micro_degrees = 4567890
    milli_minutes = micro_degrees * 3 / 5 ; // miili_minutes = 2740734    or *3/5 (and then by 10000 to get minutes)
    minutes = milli_minutes / 100000 ;           // minutes = 27
    milli_minutes -= 100000L * minutes ;             // milli_minutes = 407
    geo->degrees = degrees;
    geo->minutes = abs(minutes);
    geo->fraction = abs(milli_minutes);
}


int nmea_gga(char* sout, int sout_length, STATUS_REGISTER* status, GPS_COORDINATES* location, GPS_DETAIL* detail)
{
    // will need to parse datetime components for NMEA display
    //uint32_t date, time; // computed days since 1970, and 1/100th seconds since midnight
    //time_t datetime;  // seconds since 1970
    //struct timeval tv;
    //struct tm broken;
    //int day, month, year;
    uint32_t tod, time;
    int hour, min, sec;
    int len;
    geodms dms_lat, dms_lon;

#if 0
    // parse the gps week number and tow into date/time
    // TODO: This doesnt work yet because Jan 1, 1970 fell on a Thursday not a Sunday!
    date = detail->week*52 + detail->time/8640000;
    time = detail->time%8640000;
    datetime = (time_t)detail->week*52 + (time_t)detail->time/100;

    // convert to human readable components
    tv.tv_sec = datetime;
    tv.tv_usec = 0;
    time_to_tm(tv.tv_sec, 0, &broken);

    // now get components of date and time since we have to print in NMEA format
    year = 1970 + (date/365);
    month = (date % 365) / 30;
    day = (date %30);
#else
    time = detail->time%8640000;
    hour = time / 360000;
    tod = time - hour*360000;
    min = (tod / 6000);
    tod -= min*6000;
    sec = (tod / 100);
#endif

    degrees2dms(location->lat, &dms_lat);
    degrees2dms(location->lon, &dms_lon);

/* $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47

    Where:
         GGA          Global Positioning System Fix Data
         123519       Fix taken at 12:35:19 UTC
         4807.038,N   Latitude 48 deg 07.038' N
         01131.000,E  Longitude 11 deg 31.000' E
         1            Fix quality: 0 = invalid
                           1 = GPS fix (SPS)
                           2 = DGPS fix
                           3 = PPS fix
                           4 = Real Time Kinematic
                           5 = Float RTK
                           6 = estimated (dead reckoning) (2.3 feature)
                           7 = Manual input mode
                           8 = Simulation mode
         08           Number of satellites being tracked
         0.9          Horizontal dilution of position
         545.4,M      Altitude, Meters, above mean sea level
         46.9,M       Height of geoid (mean sea level) above WGS84
                          ellipsoid
         (empty field) time in seconds since last DGPS update
         (empty field) DGPS station ID number
         *47          the checksum data, always begins with *
*/
    //printf("https://www.google.com/maps/@%9.7f,%9.7f,21z\n", location->lat/10000000.0, location->lon/10000000.0);
    //printf("LAT: %9.7f => %d:%d:%d\n", location->lat/10000000.0, lat_deg, lat_min, lat_dec);
    //printf("LON: %9.7f => %d:%d:%d\n", location->lon/10000000.0, lon_deg, lon_min, lon_dec);

    // output the GPS location
    // GPGGA,time,lat,N,lon,E,fix,sats,hdop,alt,M,height_geod,M,,*chksum
//   printf("# %4.6f, %4.6f\n", (double)location->lat/10000000.0, (double)location->lon/10000000.0);
    len = sprintf(sout, "$GPGGA,"
                          "%02d%02d%02d,"        // time
                          "%02d%02d.%06d,%c,"      // lat
                          "%03d%02d.%06d,%c,"      // lon
                          "%d,%d,"               // fix + sats
                          "0.9,"               // hdop
                          "%d.0,M,"             // altitude + unit
                          "0.0,M,"             // height of geoid + unit
                          ",,",               // 2x empty fields plus checksum
                  hour, min, sec,
                  abs(dms_lat.degrees), dms_lat.minutes, dms_lat.fraction, (location->lat<0) ? 'S':'N',
                  abs(dms_lon.degrees), dms_lon.minutes, dms_lon.fraction, (location->lon<0) ? 'W':'E',
                  status->gps3dfix
                  ? 2
                  : status->gps2dfix
                    ? 1
                    : 0,
                  status->numsats,
                  detail->altitude
    );

    // add checksum
    len = sizeof(sout);
    nmea_checksum(sout, &len, 1);

    return len;
}

