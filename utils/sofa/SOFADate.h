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
 *   @file       SOFADate.h
 *   @brief      Useful methods to represent and manipulate date and time
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_DATE_H__
#define _SOFA_DATE_H__

#include "SOFAPlatform.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @class          Date 
     *  @brief          Useful methods to represent and manipulate date and time
     *
     *  @details        When saved as attributes string in ISO 8601 format “yyyy-mm-dd HH:MM:SS” is used.
     */
    /************************************************************************************/
    class SOFA_API Date
    {
    public:
        static const Date GetCurrentDate();
        
        static const long long GetCurrentSystemTime();
        
        static const bool IsValid(const std::string &iso8601);
        
    public:
        Date();
        ~Date();
        
        Date(const std::string &iso8601);
        
        Date(const Date &other);
        Date & operator= (const Date &other);
        
        Date( const unsigned int year,
              const unsigned int month_,
              const unsigned int day,
              const unsigned int hours,
              const unsigned int minutes,
              const unsigned int seconds        = 0,
              const unsigned int milliseconds    = 0);
        
        explicit Date(const long long millisecondsSinceEpoch);
        
        const long long GetMillisecondsSinceEpoch() const;
        
        //==============================================================================
        const unsigned int GetYear() const;
        const unsigned int GetMonth() const;
        const unsigned int GetDay() const;
        const unsigned int GetHours() const;
        const unsigned int GetMinutes() const;
        const unsigned int GetSeconds() const;
        const unsigned int GetMilliSeconds() const;
        
        const std::string ToISO8601() const;
        
        const bool IsValid() const;
        
    protected:
        static const unsigned long long getMillisecondsSinceStartup();
        
    private:
        long long millisSinceEpoch;
        
    };
    
}

#endif /* _SOFA_DATE_H__ */ 

