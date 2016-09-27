/*
Copyright (c) 2013-2014, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**

Spatial acoustic data file format - AES69-2015 - Standard for File Exchange - Spatial Acoustic Data File Format
http://www.aes.org

SOFA (Spatially Oriented Format for Acoustics)
http://www.sofaconventions.org

*/


/************************************************************************************/
/*  FILE DESCRIPTION                                                                */
/*----------------------------------------------------------------------------------*/
/*!
 *   @file       SOFADate.cpp
 *   @brief      Useful methods to represent and manipulate date and time
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFADate.h"
#include "SOFAString.h"
#include <sstream>
#include <iostream>

#if( SOFA_UNIX == 1 || SOFA_MAC == 1 )
    #include <time.h>    
    #include <sys/time.h>
#endif

#if( SOFA_MAC == 1 )
    #include <sys/time.h>
    #include <mach/mach_time.h>
#endif

using namespace sofa;



#if ( SOFA_WINDOWS == 1 )
    #include <sys/timeb.h>
    #define literal64bit(longLiteral)     ((__int64) longLiteral)
#else
    #define literal64bit(longLiteral)     (longLiteral##LL)
#endif

namespace DateHelper
{
    
    
    static struct tm ConvertMillisecondsToLocalTime (const long long millis) 
    {
        struct tm result;
        const long long seconds = millis / 1000;
        
        if (seconds < literal64bit (86400) || seconds >= literal64bit (2145916800))
        {
            // use extended maths for dates beyond 1970 to 2037..
            const int timeZoneAdjustment = 31536000 - (int) ( sofa::Date(1971, 0, 1, 0, 0).GetMillisecondsSinceEpoch() / 1000);
            const long long jdm = seconds + timeZoneAdjustment + literal64bit (210866803200);
            
            const int days = (int) (jdm / literal64bit (86400));
            const int a = 32044 + days;
            const int b = (4 * a + 3) / 146097;
            const int c = a - (b * 146097) / 4;
            const int d = (4 * c + 3) / 1461;
            const int e = c - (d * 1461) / 4;
            const int m = (5 * e + 2) / 153;
            
            result.tm_mday  = e - (153 * m + 2) / 5 + 1;
            result.tm_mon   = m + 2 - 12 * (m / 10);
            result.tm_year  = b * 100 + d - 6700 + (m / 10);
            result.tm_wday  = (days + 1) % 7;
            result.tm_yday  = -1;
            
            int t = (int) (jdm % literal64bit (86400));
            result.tm_hour  = t / 3600;
            t %= 3600;
            result.tm_min   = t / 60;
            result.tm_sec   = t % 60;
            result.tm_isdst = -1;
        }
        else
        {
            time_t now = static_cast <time_t> (seconds);
            
#if ( SOFA_WINDOWS == 1 )
    #ifdef _INC_TIME_INL
            if (now >= 0 && now <= 0x793406fff)
            {
                localtime_s (&result, &now);
            }
            else
            {                
                memset( &result, 0, sizeof (result) );
            }
    #else
            result = *localtime (&now);
    #endif
#else
            // more thread-safe
            localtime_r (&now, &result);
#endif
        }
        
        return result;
    }
    
    static int extendedModulo (const long long value, const int modulo) 
    {
        return (int) (value >= 0 ? (value % modulo)
                      : (value - ((value / modulo) + 1) * modulo));
    }
    
}



const Date Date::GetCurrentDate() 
{
    return Date( Date::GetCurrentSystemTime() );
}

/************************************************************************************/
/*!
 *  @brief          Creates a Date object
 *
 *  @details        This default constructor creates a time of 1st January 1970, (which is
 *                  represented internally as 0ms).
 */
/************************************************************************************/
Date::Date()
: millisSinceEpoch( 0 )
{
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
Date::~Date()
{
}

/************************************************************************************/
/*!
 *  @brief          Copy constructor
 *
 */
/************************************************************************************/
Date::Date( const Date &other )
: millisSinceEpoch( other.millisSinceEpoch )
{
}

/************************************************************************************/
/*!
 *  @brief          Copy operator
 *
 */
/************************************************************************************/
Date & Date::operator= (const Date &other)
{
    millisSinceEpoch = other.millisSinceEpoch;
    return *this;
}

/************************************************************************************/
/*!
 *  @brief          Creates a Date object based on a number of milliseconds.
 *
 *  @details        The internal millisecond count is set to 0 (1st January 1970). To create a
 *                  time object set to the current time, use GetCurrentTime().
 */
/************************************************************************************/
Date::Date(const long long millisecondsSinceEpoch) 
: millisSinceEpoch( millisecondsSinceEpoch )
{
}

const long long Date::GetMillisecondsSinceEpoch() const
{
    return millisSinceEpoch;
}

/************************************************************************************/
/*!
 *  @brief          Creates a date from a string literal in ISO8601 format
 *                    i.e. yyyy-mm-dd HH:MM:SS
 *  @param[in]      -
 *  @param[out]     -
 *  @param[in, out] -
 *  @return         -
 *
 *  @details
 *  @n  
 */
/************************************************************************************/
Date::Date(const std::string &iso8601) 
: millisSinceEpoch( 0 )
{
    const bool valid = Date::IsValid( iso8601 );
    
    if( valid == true )
    {
        const std::string yyyy = iso8601.substr( 0, 4 );
        const std::string mm   = iso8601.substr( 5, 2 );
        const std::string dd   = iso8601.substr( 8, 2 );
        
        const std::string hh   = iso8601.substr( 11, 2 );
        const std::string min  = iso8601.substr( 14, 2 );
        const std::string sec  = iso8601.substr( 17, 2 );
        
        const int year        =    atoi( yyyy.c_str() );
        const int month     =    atoi( mm.c_str() );
        const int day        =    atoi( dd.c_str() );
        const int hours        =    atoi( hh.c_str() );
        const int minutes    =    atoi( min.c_str() );
        const int seconds    =    atoi( sec.c_str() );
    
        if( year < 0 || month < 0 || day < 0 || hours < 0 || minutes < 0 || seconds < 0)
        {
            SOFA_ASSERT( false );
        }
        
        sofa::Date tmp( year, month, day, hours, minutes, seconds );
        
        *this = tmp;
    }
}

/************************************************************************************/
/*!
 *  @brief          Creates a Date from a set of date components
 *  @param[in]      year : the year, in 4-digit format, e.g. 2004
 *  @param[in]      month : the month, in the range 1 to 12
 *  @param[in]      day : the day of the month, in the range 1 to 31
 *  @param[in]      hours : hours in 24-hour clock format, 0 to 23
 *  @param[in]      minutes : minutes 0 to 59
 *  @param[in]      seconds :seconds 0 to 59
 *  @param[in]      milliseconds : milliseconds 0 to 999
 *
 */
/************************************************************************************/
Date::Date (const unsigned int year,
            const unsigned int month_,
            const unsigned int day,
            const unsigned int hours,
            const unsigned int minutes,
            const unsigned int seconds,
            const unsigned int milliseconds) 
: millisSinceEpoch( 0 )
{
    SOFA_ASSERT( year > 100 ); // year must be a 4-digit version
    
    SOFA_ASSERT( month_ >= 1 && month_ <= 12);
    SOFA_ASSERT( day >= 1 && day <= 31);
    
    const unsigned int month = month_ - 1;  ///< struct tm use [0-11] for month range
    
    if( year < 1971 || year >= 2038 )
    {
        // use extended maths for dates beyond 1970 to 2037..
        const int timeZoneAdjustment = 0;
        const int a = (13 - month) / 12;
        const int y = year + 4800 - a;
        const int jd = day + (153 * (month + 12 * a - 2) + 2) / 5
        + (y * 365) + (y /  4) - (y / 100) + (y / 400)
        - 32045;
        
        const long long s = ((long long) jd) * literal64bit (86400) - literal64bit (210866803200);
        
        millisSinceEpoch = 1000 * (s + (hours * 3600 + minutes * 60 + seconds - timeZoneAdjustment))
        + milliseconds;
    }
    else
    {
        struct tm t;
        t.tm_year   = year - 1900;
        t.tm_mon    = month;
        t.tm_mday   = day;
        t.tm_hour   = hours;
        t.tm_min    = minutes;
        t.tm_sec    = seconds;
        t.tm_isdst  = -1;
        
        millisSinceEpoch = 1000 * (long long) mktime (&t);
        
        if (millisSinceEpoch < 0)
        {
            millisSinceEpoch = 0;
        }
        else
        {
            millisSinceEpoch += milliseconds;
        }
    }
}


const unsigned long long Date::getMillisecondsSinceStartup()
{    
#if( SOFA_WINDOWS == 1 ) 
    
    return (unsigned long long) GetTickCount();
    
#elif( SOFA_MAC == 1 )
    const int64_t kOneMillion = 1000 * 1000;
    static mach_timebase_info_data_t s_timebase_info;
    
    if( s_timebase_info.denom == 0 ) 
    {
        (void) mach_timebase_info( &s_timebase_info );
    }
    
    // mach_absolute_time() returns billionth of seconds,
    // so divide by one million to get milliseconds
    return (unsigned long long)( (mach_absolute_time() * s_timebase_info.numer) / (kOneMillion * s_timebase_info.denom) );
    
#elif( SOFA_UNIX == 1 )
    
    timespec t;
    clock_gettime( CLOCK_MONOTONIC, &t );
    
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
#else
    #error "Unknown host architecture"
#endif    
    
}

/************************************************************************************/
/*!
 *  @brief          Returns the current system time
 *
 *  @details        Returns the number of milliseconds since midnight jan 1st 1970
 */
/************************************************************************************/
const long long Date::GetCurrentSystemTime() 
{
    static unsigned int lastCounterResult = 0xffffffff;
    static long long correction = 0;
    
    const unsigned int now = (unsigned int) Date::getMillisecondsSinceStartup();
    
    // check the counter hasn't wrapped (also triggered the first time this function is called)
    if( now < lastCounterResult )
    {
        // double-check it's actually wrapped, in case multi-cpu machines have timers that drift a bit.
        if ( lastCounterResult == 0xffffffff || now < lastCounterResult - 10 )
        {
            // get the time once using normal library calls, and store the difference needed to
            // turn the millisecond counter into a real time.
#if ( SOFA_WINDOWS == 1 )
            struct _timeb t;
#ifdef _INC_TIME_INL
            _ftime_s (&t);
#else
            _ftime (&t);
#endif
            correction = (((long long) t.time) * 1000 + t.millitm) - now;
#else
            struct timeval tv;
            struct timezone tz;
            gettimeofday (&tv, &tz);
            correction = (((long long) tv.tv_sec) * 1000 + tv.tv_usec / 1000) - now;
#endif
        }
    }
    
    lastCounterResult = now;
    
    return correction + now;
}


const unsigned int Date::GetYear() const 
{
    return DateHelper::ConvertMillisecondsToLocalTime( this->millisSinceEpoch ).tm_year + 1900;
}

const unsigned int Date::GetMonth() const 
{
    ///@n returns month in the range [1-12]
    return DateHelper::ConvertMillisecondsToLocalTime( this->millisSinceEpoch ).tm_mon + 1;
}

const unsigned int Date::GetDay() const 
{
    return DateHelper::ConvertMillisecondsToLocalTime( this->millisSinceEpoch ).tm_mday;
}

const unsigned int Date::GetHours() const 
{
    return DateHelper::ConvertMillisecondsToLocalTime( this->millisSinceEpoch ).tm_hour;
}

const unsigned int Date::GetMinutes() const 
{
    return DateHelper::ConvertMillisecondsToLocalTime( this->millisSinceEpoch ).tm_min;
}

const unsigned int Date::GetSeconds() const 
{
    return DateHelper::extendedModulo ( this->millisSinceEpoch / 1000, 60 );
}

const unsigned int Date::GetMilliSeconds() const 
{
    return DateHelper::extendedModulo( this->millisSinceEpoch, 1000 );
}

const std::string Date::ToISO8601() const
{
    const unsigned int yyyy = GetYear();
    const unsigned int mm    = GetMonth();
    const unsigned int dd    = GetDay();
    const unsigned int hh    = GetHours();
    const unsigned int min    = GetMinutes();
    const unsigned int ss    = GetSeconds();
    
    std::ostringstream str;
    str << yyyy;
    str << "-";
    if( mm < 10 )
    {
        /// zero-pad
        str << "0";
    }
    str << mm;
    str << "-";
    if( dd < 10 )
    {
        /// zero-pad
        str << "0";
    }
    str << dd;
    str << " ";
    if( hh < 10 )
    {
        /// zero-pad
        str << "0";
    }    
    str << hh;
    str << ":";
    if( min < 10 )
    {
        /// zero-pad
        str << "0";
    }    
    str << min;
    str << ":";
    if( ss < 10 )
    {
        /// zero-pad
        str << "0";
    }
    str << ss;
    
    return str.str();
}

const bool Date::IsValid() const
{
    const unsigned int d = GetDay();
    const unsigned int m = GetMonth();
    const unsigned int y = GetYear();
    
    const unsigned int hh  = GetHours();
    const unsigned int min = GetMinutes();
    const unsigned int sec = GetSeconds();
        
    if( d < 1 || d > 31 )
    {
        return false;
    }
    if( m < 1 || m > 12 )
    {
        return false;
    }
    if( y < 100 )
    {
        return false;
    }
    
    if( m == 4 || m == 6 || m == 9 || m == 11 )
    {
        if( d > 30 )
        {
            return false;
        }
    }
    if( m == 2 && d > 29 )
    {
        return false;
    }
    
    if( hh > 24 )
    {
        return false;
    }
    
    if( min > 59 )
    {
        return false;
    }
    
    if( sec > 60 )
    {
        return false;
    }
    
    return true;
}

/************************************************************************************/
/*!
 *  @brief          Returns true if a string represents a Date with ISO8601 format
 *                    i.e. yyyy-mm-dd HH:MM:SS
 *  @param[in]      -
 *  @param[out]     -
 *  @param[in, out] -
 *  @return         -
 *
 *  @details
 *  @n              
 */
/************************************************************************************/
const bool Date::IsValid(const std::string &iso8601)
{
    const std::size_t length = iso8601.length();
    
    if( length != 19 )
    {
        return false;
    }
    
    const char * content = iso8601.c_str();
    
    if( sofa::String::IsInt( content[0] ) == false 
       || sofa::String::IsInt( content[1] ) == false  
       || sofa::String::IsInt( content[2] ) == false  
       || sofa::String::IsInt( content[3] ) == false  
       || sofa::String::IsInt( content[5] ) == false  
       || sofa::String::IsInt( content[6] ) == false  
       || sofa::String::IsInt( content[8] ) == false  
       || sofa::String::IsInt( content[9] ) == false  
       || sofa::String::IsInt( content[11] ) == false  
       || sofa::String::IsInt( content[12] ) == false  
       || sofa::String::IsInt( content[14] ) == false  
       || sofa::String::IsInt( content[15] ) == false  
       || sofa::String::IsInt( content[17] ) == false  
       || sofa::String::IsInt( content[18] ) == false  
       )
    {
        return false;
    }
    
    if( content[4] != '-' || content[7] != '-'  )
    {
        return false;
    }
    
    if( content[10] != ' ' )
    {
        return false;
    }
    
    if( content[13] != ':' || content[16] != ':' )
    {
        return false;
    }
    
    const std::string yyyy = iso8601.substr( 0, 4 );
    const std::string mm   = iso8601.substr( 5, 2 );
    const std::string dd   = iso8601.substr( 8, 2 );
    
    const std::string hh   = iso8601.substr( 11, 2 );
    const std::string min  = iso8601.substr( 14, 2 );
    const std::string sec  = iso8601.substr( 17, 2 );
    
    const int year        =    sofa::String::String2Int( yyyy );
    const int month     =    sofa::String::String2Int( mm );
    const int day        =    sofa::String::String2Int( dd );
    const int hours        =    sofa::String::String2Int( hh );
    const int minutes    =    sofa::String::String2Int( min );
    const int seconds    =    sofa::String::String2Int( sec );
    
    if( year < 0 || month < 0 || day < 0 || hours < 0 || minutes < 0 || seconds < 0)
    {
        return false;
    }
    
    const sofa::Date tmp( year, month, day, hours, minutes, seconds );
    
    return tmp.IsValid();        
}
