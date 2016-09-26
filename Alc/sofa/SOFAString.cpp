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
 *   @file       SOFAString.cpp
 *   @brief      Useful functions to manipulate strings
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAString.h"

using namespace sofa;

/************************************************************************************/
/*!
 *  @brief          Pad with character at the right of the original string
 *  @param[in]      src : the string to be padded
 *  @param[in]      totalLength : the total number of characters
 *  @param[in]      pad : the padding character
 *  @return         the string padded
 *
 */
/************************************************************************************/
const std::string sofa::String::PadWith(const std::string &src,
                                        const std::size_t totalLength,
                                        const std::string &pad)
{
    const std::size_t length = src.length();
    if( length > totalLength )
    {
        // the string is longer that expected...
        // let's return the original string
        return src; 
    }
    else
    {        
        const std::size_t paddingLength = pad.length();
        
        const std::size_t numPad = ( totalLength - length ) / paddingLength;
        
        std::string dest = src;
        
        for( std::size_t i = 0; i < numPad; i++ )
        {
            dest += pad;
        }
        
        return dest;
    }
}

void sofa::String::PrintSeparationLine(std::ostream & output)
{
    const unsigned int padding               = 30;
    const std::string verticalSeparator      = " ";
    const std::string horizontalSeparator    = "_";
    
    output << sofa::String::PadWith( horizontalSeparator, padding, horizontalSeparator );    
    output << horizontalSeparator;
    output << sofa::String::PadWith( horizontalSeparator, padding, horizontalSeparator );
    output << horizontalSeparator ;
    output << sofa::String::PadWith( horizontalSeparator, padding, horizontalSeparator );
    output << horizontalSeparator ;
    output << sofa::String::PadWith( horizontalSeparator, padding, horizontalSeparator );
    output << std::endl;
}

