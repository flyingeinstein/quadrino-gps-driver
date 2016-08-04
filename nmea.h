#ifndef __QUADRINO_GPS_NMEA_H
#define __QUADRINO_GPS_NMEA_H

#include "registers.h"


/// \brief Calculates the checksum for a nmea sentence and appends the checksum to the sentence.
/// The input nmea sentence can optionally start with $ character which will be ignored in the checksum. This function
/// will add the checksum to the end of the input buffer (ex. "*47").
///
/// \param nmea_sentence the buffer containing the nmea sentence. Must contain enough space to append the checksum value.
/// \param output_length if not NULL, receives the total length of the string after the checksum string was added
/// \param add_lf if non-zero, a line feed is added to the output after the checksum
/// \returns The computed checksum value that was appended to the input nmea sentence.
int nmea_checksum(char* nmea_sentence, int* output_length, int add_lf);

/// \brief Formats a NMEA GPGGA sentence from GPS sensor data.
/// \param sout The output buffer that will receive the NMEA sentence
/// \param sout_length The capacity of the output buffer for safety, typically use strlen(sout) when calling this function.
/// \param status The status register from the GPS sensor data
/// \param location The lat/lon registers from the GPS sensor data
/// \param detail The speed, altitude, course and date/time registers from the GPS sensor data
int nmea_gga(char* sout, int sout_length, STATUS_REGISTER* status, GPS_COORDINATES* location, GPS_DETAIL* detail);


#endif // __QUADRINO_GPS_NMEA_H
