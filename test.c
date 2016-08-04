// Test app for NMEA generation
// compile with: gcc -I/usr/include -I/usr/local/include test.c -o test; ./test



#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#include "registers.h"

typedef struct {
    GPS_COORDINATES location;
    GPS_DETAIL detail;
} gpsdata;


gpsdata sample[] = {
    // St Petersburg, Florida
    {
       { 278165079,-827941423 }, // lat, lon
       { 0,         // ground speed
         105,       // altitude
         0,         // ground course
         1904,      // gps week number (weeks since Jan 1, 1970)
         28036800   // gps time of week (100th of a second)
       }
    },

    // Sao Paulo, Brazil
    {
       { -235399468,-466339043 },
       { 0,53,0,1704,8036800 }
    },

    // Rome, Italy
    {
        { 41808205,124628523 },
        { 0, 853, 0, 1907, 2525263 }
    },

    // end of sample data
    { 0 }

};

int nmea_gga(char* sout, int sout_length, GPS_COORDINATES* loc, GPS_DETAIL* detail)
{
   // will need to parse datetime components for NMEA display
   double dow;   // computed day of week
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
   dow = detail->time/8640000;
   tod = detail->time - dow;
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
   lat_deg = abs(loc->lat / 10000000);
   lon_deg = abs(loc->lon / 10000000);

   lat_frak = (loc->lat % 10000000) * 0.6; 
   lon_frak = (loc->lon % 10000000) * 0.6; 
   lat_min = abs(lat_frak  / 100000);
   lon_min = abs(lon_frak  / 100000);
   lat_dec = abs(lat_frak % 100000);
   lon_dec = abs(lon_frak % 100000);

   // output the GPS location
   // GPGGA,time,lat,N,lon,E,fix,sats,hdop,alt,M,height_geod,M,,*chksum
   printf("# %4.6f, %4.6f\n", (double)loc->lat/10000000.0, (double)loc->lon/10000000.0);
   len = sprintf(sout, "$GPGGA," "%02d%02d%02d," "%d%02d.%06d,%c," "%d%02d.%06d,%c,",
           hour,min,sec,
           lat_deg, lat_min, lat_dec, (loc->lat<0) ? 'S':'N',
           lon_deg, lon_min, lon_dec, (loc->lon<0) ? 'E':'W');

   return len;
}


void main()
{
    char sout[512];
    gpsdata* pdata = sample; 
    while(pdata->location.lat!=0) {
        nmea_gga(sout, sizeof(sout), &pdata->location, &pdata->detail);     
        printf("%s\n", sout);
        pdata++;
    }
}

