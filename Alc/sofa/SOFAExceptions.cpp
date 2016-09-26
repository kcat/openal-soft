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
 *   @file       SOFAExceptions.cpp
 *   @brief      Exception handling
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAExceptions.h"
#include <iostream>

#if ( SOFA_MAC == 1 )
    #include <sstream>
    #include <syslog.h>
#endif

using namespace sofa;

/// specify whether raised exception prints something to cerr or not...
/// use this with care
bool sofa::Exception::logToCerr = true;

/************************************************************************************/
/*!
 *  @brief          Enables or disables the logging of sofa::Exception on the standard error.
 *                  Use this with great care !
 *                  This affects globaly all sofa exceptions
 *
 */
/************************************************************************************/
void sofa::Exception::LogToCerr(const bool value)
{
    sofa::Exception::logToCerr = value;
}

const bool sofa::Exception::IsLoggedToCerr()
{
    return sofa::Exception::logToCerr;
}

/************************************************************************************/
/*!
 *  @brief          Class constructor
 *  @param[in]      -
 *  @param[out]     -
 *  @param[in, out] -
 *  @return         -
 *
 *  @details
 *  @n  
 */
/************************************************************************************/
Exception::Exception(const std::string &text,
                     const std::string &file,
                     const unsigned long line_,
                     const bool exitAfterException)
: std::exception()
, filename( file )
, description( text )
, line( line_ )
{
/*
#if ( SOFA_MAC == 1 )
    
    std::ostringstream msg;
    msg << "Exception occured in : " << file << " at line " << line << " : " << std::endl;
    msg << "  " << description << std::endl;
    openlog("sofa", LOG_PID | LOG_NDELAY | LOG_CONS | LOG_PERROR, LOG_USER);
    setlogmask(LOG_UPTO(LOG_NOTICE));
    syslog(LOG_NOTICE, "%s", msg.str().c_str());
    closelog();
    
#else
 */
    if( sofa::Exception::logToCerr == true )
    {
        std::cerr << "Exception occured (in file " << Exception::getFileName( file ) << " at line " << line << ") : " << std::endl;
        std::cerr << "        " << description << std::endl;
    }
//#endif
    
    if( exitAfterException == true )
    {
        exit(1);
    }
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
Exception::~Exception() SOFA_NOEXCEPT
{
}

/************************************************************************************/
/*!
 *  @brief          Returns a description of the raised exception
 *
 */
/************************************************************************************/
const char* Exception::what() const SOFA_NOEXCEPT
{
    return description.c_str();
}

/************************************************************************************/
/*!
 *  @brief          Returns the name of the source file where the exception occured
 *
 */
/************************************************************************************/
const std::string Exception::GetFile() const
{
    return filename;
}

/************************************************************************************/
/*!
 *  @brief          Returns the line number of the source file where the exception occured
 *
 */
/************************************************************************************/
const unsigned long Exception::GetLine() const
{
    return line;
}

/************************************************************************************/
/*!
 *  @brief          given a complete filename, this removes the path name.
 *
 */
/************************************************************************************/
const std::string Exception::getFileName(const std::string & fullfilename)
{
#if (SOFA_MAC == 1 || SOFA_UNIX == 1 )
    const char separator = '/';
#else
    const char separator = '\\';
#endif
    
    const std::size_t found = fullfilename.find_last_of( separator );
    
    if( found != std::string::npos )
    {        
        return fullfilename.substr( found+1, std::string::npos );
    }
    else 
    {
        return fullfilename;
    }        
}

