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
 *   @file       SOFACoordinates.h
 *   @brief      SOFA Coordinates systems
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_COORDINATES_H__
#define _SOFA_COORDINATES_H__

#include "SOFAPlatform.h"
#include "netcdf.h"
#include "ncFile.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @class          Coordinates 
     *  @brief          Static class to represent information about SOFA coordinates
     *
     *  @details        So far, SOFA specifications consider two coordinates system: cartesian and spherical
     */
    /************************************************************************************/
    class SOFA_API Coordinates
    {
    public:
        
        enum Type
        {
            kCartesian              = 0,    ///< cartesian
            kSpherical              = 1,    ///< spherical
            kNumCoordinatesTypes    = 2
        };
        
    public:
        static const std::string GetName(const sofa::Coordinates::Type &type_);
        static const sofa::Coordinates::Type GetType(const std::string &name);
        
        static const bool IsValid(const std::string &name);
        
        static const bool IsValid(const netCDF::NcAtt & attr);
        
    protected:
        Coordinates();
    };
    
}

#endif /* _SOFA_COORDINATES_H__ */
