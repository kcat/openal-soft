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
 *   @file       SOFAHostArchitecture.h
 *   @brief      
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 *   @details
 *   @n
 */
/************************************************************************************/
#ifndef _SOFA_HOST_ARCHITECTURE_H__
#define _SOFA_HOST_ARCHITECTURE_H__

#if ( defined(unix) || defined(__unix) || defined(__unix__) || defined(__linux__) )

    //==============================================================================
    /// UNIX
    //==============================================================================
    #define SOFA_UNIX 1
    #undef SOFA_MAC
    #undef SOFA_WINDOWS

#elif ( defined(macintosh) || defined(__MACH__) || defined(__APPLE__) )

    //==============================================================================
    /// MAC OS
    //==============================================================================
    #define SOFA_MAC 1
    #undef SOFA_WINDOWS
    #undef SOFA_UNIX

#elif ( defined(_WIN32) || defined (_WIN64)) || (defined (WIN32) || defined(__DOS__) || defined(_MSC_VER) )

    //==============================================================================
    /// WINDOWS
    //==============================================================================
    #define SOFA_WINDOWS 1
    #undef SOFA_MAC
    #undef SOFA_UNIX

#else
    
    #error "Unknown host architecture"

#endif



#endif /* _SOFA_HOST_ARCHITECTURE_H__ */
