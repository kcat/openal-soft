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
 *   @file       SOFAPosition.cpp
 *   @brief      Represents a sofa position variable
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAPosition.h"
#include "SOFANcUtils.h"
#include "SOFAUtils.h"

using namespace sofa;

/************************************************************************************/
/*!
 *  @brief          Class constructor
 *
 */
/************************************************************************************/
PositionVariable::PositionVariable(const netCDF::NcVar & variable)
: var( variable )
{
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
PositionVariable::~PositionVariable()
{
}

const sofa::Units::Type PositionVariable::GetUnits() const
{
    /// Position:Units attribute
    const netCDF::NcVarAtt attrUnits = sofa::NcUtils::GetAttribute( var, "Units" );
    
    if( sofa::Units::IsValid( attrUnits ) == false )
    {
        ///@todo A VERIFIER
        return sofa::Units::kNumUnitsTypes;
    }
    else
    {
        const std::string unitsName   = sofa::NcUtils::GetAttributeValueAsString( attrUnits );
        const sofa::Units::Type units = sofa::Units::GetType( unitsName );
        return units;
    }    
}

const sofa::Coordinates::Type PositionVariable::GetCoordinates() const
{
    /// Position:Type attribute
    const netCDF::NcVarAtt attrType = sofa::NcUtils::GetAttribute( var, "Type" );
    
    if( sofa::Coordinates::IsValid( attrType ) == false )
    {
        ///@todo A VERIFIER
        return sofa::Coordinates::kNumCoordinatesTypes;
    }
    else
    {
        const std::string coordinatesName = sofa::NcUtils::GetAttributeValueAsString( attrType );
        const sofa::Coordinates::Type coordinates = sofa::Coordinates::GetType( coordinatesName );
        return coordinates;
    }
}

const unsigned int PositionVariable::GetDimensionality() const
{
    const int dimensionality = sofa::NcUtils::GetDimensionality( var );
    
    return (unsigned int) sofa::smax( (int) 0, dimensionality );
}

const bool PositionVariable::HasDimensions(const std::size_t dim1, const std::size_t dim2) const
{
    return sofa::NcUtils::HasDimensions( dim1, dim2, var );
}

const bool PositionVariable::HasDimensions(const std::size_t dim1, const std::size_t dim2, const std::size_t dim3) const
{
    return sofa::NcUtils::HasDimensions( dim1, dim2, dim3, var );    
}

const bool PositionVariable::HasUnits() const
{
    SOFA_ASSERT( sofa::NcUtils::IsValid( var ) == true );
    SOFA_ASSERT( sofa::NcUtils::IsDouble( var ) == true );
    
    const netCDF::NcVarAtt attrType = sofa::NcUtils::GetAttribute( var, "Type" );
    
    return sofa::Coordinates::IsValid( attrType );
}

const bool PositionVariable::HasCoordinates() const
{
    SOFA_ASSERT( sofa::NcUtils::IsValid( var ) == true );
    SOFA_ASSERT( sofa::NcUtils::IsDouble( var ) == true );

    const netCDF::NcVarAtt attrUnits = sofa::NcUtils::GetAttribute( var, "Units" );
    
    return sofa::Units::IsValid( attrUnits );
}

/************************************************************************************/
/*!
 *  @brief          Checks if the NcVar corresponds to a valid NcVar, of type double, of dimensionality 2 or 3
 *                  with valid "Type" and "Units" attributes
 *                    
 *                  Returns true if everything is conform to the specifications
 *                  False otherwise or if any error occured
 *  @param[in]      -
 *  @param[out]     -
 *  @param[in, out] -
 *  @return         -
 *
 *  @details
 *  @n                
 */
/************************************************************************************/
const bool PositionVariable::IsValid(const bool shouldHaveTypeAndUnits) const
{
    if( sofa::NcUtils::IsValid( var ) == false )
    {
        return false;
    }
    
    if( sofa::NcUtils::IsDouble( var ) == false )
    {
        return false;
    }
    
    const int dimensionality = sofa::NcUtils::GetDimensionality( var );
    if( dimensionality != 2 && dimensionality != 3 )
    {
        /// all SOFA elements must have a dimensionality of 2 or 3
        return false;
    }
    
    if( shouldHaveTypeAndUnits == true )
    {
        /// Position:Type attribute
        const netCDF::NcVarAtt attrType = sofa::NcUtils::GetAttribute( var, "Type" );
        
        if( sofa::Coordinates::IsValid( attrType ) == false )
        {
            return false;
        }
        
        /// Position:Units attribute
        const netCDF::NcVarAtt attrUnits = sofa::NcUtils::GetAttribute( var, "Units" );
        
        if( sofa::Units::IsValid( attrUnits ) == false )
        {
            return false;
        }
        
        /// if Type is Cartesian, the Unit should be meter
        /// if Type is Spherical, the Unit should be 'degree, degree, meter'
        
        const sofa::Coordinates::Type type  = sofa::Coordinates::GetType( sofa::NcUtils::GetAttributeValueAsString( attrType ) );
        const sofa::Units::Type units       = sofa::Units::GetType( sofa::NcUtils::GetAttributeValueAsString( attrUnits ) );
        
        if( type == sofa::Coordinates::kCartesian )
        {
            if( units != sofa::Units::kMeter )
            {
                return false;
            }
        }
        else if( type == sofa::Coordinates::kSpherical )
        {
            if( units != sofa::Units::kSphericalUnits )
            {
                return false;
            }
        }
        else
        {
            return false;
        }
        
    }
    
    
    return true;
}

