
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


int nmea_gga(char* sout, int sout_length, STATUS_REGISTER* status, GPS_COORDINATES* location, GPS_DETAIL* detail)
{
    // will need to parse datetime components for NMEA display
    uint32_t tod; // computed time of day
    uint32_t date, time; // computed days since 1970, and 1/100th seconds since midnight
    time_t datetime;  // seconds since 1970
    struct timeval tv;
    struct tm broken;
    int day, month, year, hour, min, sec;
    int len;

    // must use fixed point techniques to display lat/lon because kernel drivers shouldnt use floating point
    int32_t lat_deg, lon_deg;
    int32_t lat_frak, lon_frak;
    int32_t lat_min, lon_min;
    int32_t lat_dec, lon_dec;

    // parse the gps week number and tow into date/time
    tod = detail->time - detail->time/8640000;
    date = detail->week*52 + detail->time/8640000;
    time = detail->time%8640000;
    datetime = detail->week*52 + detail->time/100;

    // convert to human readable components
    tv.tv_sec = datetime;
    tv.tv_usec = 0;
    time_to_tm(tv.tv_sec, 0, &broken);

    // now get components of date and time since we have to print in NMEA format
    year = 1970 + (date/365);
    month = (date % 365) / 30;
    day = (date %30);
    hour = time / 360000;
    min = (time / 6000) % 60;
    sec = (time / 100) % 60;

    // get the integer and decimal portion of the lat/lon as seperate ints
    lat_deg = abs(location->lat / 10000000);
    lon_deg = abs(location->lon / 10000000);

    lat_frak = (location->lat % 10000000) * 100 / 60;
    lon_frak = (location->lon % 10000000) * 100 / 60;
    lat_min = abs(lat_frak  / 100000);
    lon_min = abs(lon_frak  / 100000);
    lat_dec = abs(lat_frak % 100000);
    lon_dec = abs(lon_frak % 100000);

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

    // output the GPS location
    // GPGGA,time,lat,N,lon,E,fix,sats,hdop,alt,M,height_geod,M,,*chksum
//   printf("# %4.6f, %4.6f\n", (double)location->lat/10000000.0, (double)location->lon/10000000.0);
    len = sprintf(sout, "$GPGGA,"
                          "%02d%02d%02d,"        // time
                          "%d%02d.%06d,%c,"      // lat
                          "%d%02d.%06d,%c,"      // lon
                          "%d,%d,"               // fix + sats
                          "0.9,"               // hdop
                          "%d.0,M,"             // altitude + unit
                          "0.0,M,"             // height of geoid + unit
                          ",,",               // 2x empty fields plus checksum
                  broken.tm_hour,broken.tm_min, broken.tm_sec,
                  lat_deg, lat_min, lat_dec, (location->lat<0) ? 'S':'N',
                  lon_deg, lon_min, lon_dec, (location->lon<0) ? 'E':'W',
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

