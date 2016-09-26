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
 *   @file       SOFAPoint3.h
 *   @brief      Represents one point in 3D
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_POINT3_H__
#define _SOFA_POINT3_H__

#include "SOFACoordinates.h"
#include "SOFAUnits.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @class          Point3 
     *  @brief          Represents one point in 3D with a unit and a coordinate system,
     *                  and allows for conversion between coordinate systems
     *
     */
    /************************************************************************************/
    class SOFA_API Point3
    {
    public:
        Point3();
        ~Point3();
        
        Point3( const Point3 &other );                    
        const Point3 & operator= ( const Point3 &other );
        
        const double operator[](const unsigned int index) const;
        
        const sofa::Units::Type GetUnits() const;
        const sofa::Coordinates::Type GetCoordinates() const;
        
        void Set(const sofa::Units::Type &type_);
        void Set(const sofa::Coordinates::Type &type_);                
        void Set(const double data_[3]);
        
        /*
        void ConvertTo(const sofa::Units::Type &newUnit);
        void ConvertTo(const sofa::Coordinates::Type &newCoordinate);
        void ConvertTo(const sofa::Coordinates::Type &newCoordinate, const sofa::Units::Type &newUnit);
         */
        
    public:
        //==============================================================================
        /// data members kept public for convenience
        double data[3];
        sofa::Units::Type units;
        sofa::Coordinates::Type coordinates;
    };
    
    const bool GetPoint3(sofa::Point3 &point3, const netCDF::NcVar & variable);
    
}

#endif /* _SOFA_POINT3_H__ */

