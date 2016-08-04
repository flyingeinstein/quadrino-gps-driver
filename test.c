// Test app for NMEA generation
// compile with: gcc -I/usr/include -I/usr/local/include test.c -o test; ./test



#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
//#include <sys/time.h>
#include <time.h>
#include <memory.h>

#include "nmea.h"

typedef struct {
    STATUS_REGISTER status;
    GPS_COORDINATES location;
    GPS_DETAIL detail;
} gpsdata;


/// \brief Sample data containing multiple GPS sensor readings
gpsdata sample[] = {
    // St Petersburg, Florida
    {
        { 1,1,1,0,6 },
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
        { 1,1,0,0,4 },
        { -235399468,-466339043 },
        { 0,53,0,1704,8036800 }
    },

    // Rome, Italy
    {
        { 1,0,0,0,2 },
        { 41808205,124628523 },
        { 0, 853, 0, 1907, 2525263 }
    },

    // end of sample data
    { 0 }

};

/// \brief Converts seconds since epoch to broken out date/time components
/// The kernel contains this function but it is not available in user-space so we define it here and use localtime()
/// to do the conversion. Epoch is seconds since Jan 1, 1970
void time_to_tm(time_t totalsecs, int offset, struct tm *result)
{
    if(result!=NULL)
        *result = *localtime(&totalsecs);
}


void main()
{
    char sout[512];

    // check our nmea checksum calculator
    strcpy(sout, "$GPGLL,5300.97914,N,00259.98174,E,125926,A");
    if(0x28 != nmea_checksum(sout)) {
        printf("FAILED   CHECKSUM  28 != %s\n", sout);
    }
    strcpy(sout, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    if(0x47 != nmea_checksum(sout)) {
        printf("FAILED   CHECKSUM  47 != %s\n", sout);
    }

    // output sample GPGAA sentences
    gpsdata* pdata = sample; 
    while(pdata->location.lat!=0) {
        nmea_gga(sout, sizeof(sout), &pdata->status, &pdata->location, &pdata->detail);
        printf("%s\n", sout);
        pdata++;
    }
}

