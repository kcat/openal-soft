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
 *   @file       SOFAPosition.h
 *   @brief      Represents a sofa position variable
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_POSITION_H__
#define _SOFA_POSITION_H__

#include "SOFACoordinates.h"
#include "SOFAUnits.h"
#include "ncVar.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @class          PositionVariable 
     *  @brief          Represents a sofa position variable
     *
     *  @details        Within SOFA, Position variables are represented by a variable (2 or 3 dimensions)
     *                  which has two attributes : coordinate type and unit
     */
    /************************************************************************************/
    class SOFA_API PositionVariable
    {        
    public:
        PositionVariable(const netCDF::NcVar & variable);
        ~PositionVariable();
        
        const bool IsValid(const bool shouldHaveTypeAndUnits = true) const;
        
        const bool HasUnits() const;
        const bool HasCoordinates() const;
        
        const sofa::Units::Type GetUnits() const;
        const sofa::Coordinates::Type GetCoordinates() const;
        
        const unsigned int GetDimensionality() const;
        
        const bool HasDimensions(const std::size_t dim1, const std::size_t dim2) const;
        const bool HasDimensions(const std::size_t dim1, const std::size_t dim2, const std::size_t dim3) const;
                        
    protected:        
        const netCDF::NcVar var;                ///< the NcVar is not hold
        
    private:
        //==============================================================================
        /// avoid shallow and copy constructor
        PositionVariable( const PositionVariable &other );                    
        const PositionVariable & operator= ( const PositionVariable &other );
    };
    
}

#endif /* _SOFA_POSITION_H__ */ 

