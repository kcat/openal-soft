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
 *   @file       SOFAPoint3.cpp
 *   @brief      Represents one point in 3D
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAPoint3.h"
#include "SOFAPosition.h"
#include "SOFANcUtils.h"

using namespace sofa;

/************************************************************************************/
/*!
 *  @brief          Class constructor
 *
 */
/************************************************************************************/
Point3::Point3()
: data()
, units( sofa::Units::kMeter )
, coordinates( sofa::Coordinates::kCartesian )
{
    data[0] = 0.0;
    data[1] = 0.0;
    data[2] = 0.0;
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
Point3::~Point3()
{
}

/************************************************************************************/
/*!
 *  @brief          Copy constructor
 *
 */
/************************************************************************************/
Point3::Point3( const Point3 &other )
: data()
, units( other.units )
, coordinates( other.coordinates )
{
    data[0] = other.data[0];
    data[1] = other.data[1];
    data[2] = other.data[2];
}

/************************************************************************************/
/*!
 *  @brief          Copy operator
 *
 */
/************************************************************************************/
const Point3 & Point3::operator= ( const Point3 &other )
{
    units        = other.units;
    coordinates = other.coordinates;
    
    data[0] = other.data[0];
    data[1] = other.data[1];
    data[2] = other.data[2];
    
    return *this;
}

const sofa::Units::Type Point3::GetUnits() const
{
    return units;
}

const sofa::Coordinates::Type Point3::GetCoordinates() const
{
    return coordinates;
}

void Point3::Set(const sofa::Units::Type &type_)
{
    units = type_;
}

void Point3::Set(const sofa::Coordinates::Type &type_)
{
    coordinates = type_;
}

void Point3::Set(const double data_[3])
{
    data[0] = data_[0];
    data[1] = data_[1];
    data[2] = data_[2];
}

const double Point3::operator[](const unsigned int index) const
{
    switch( index )
    {
        case 0    : return data[0];
        case 1    : return data[1];
        case 2    : return data[2];
        default    : SOFA_ASSERT( false ); return 0.0;
    } 
}

/*
void Point3::ConvertTo(const sofa::Units::Type &newUnit)
{
    if( newUnit != units )
    {
        ///@todo A COMPLETER
        
        
        units = newUnit;
    }
}

void Point3::ConvertTo(const sofa::Coordinates::Type &newCoordinate)
{
    if( newCoordinate != coordinates )
    {
        ///@todo A COMPLETER
        
        
        
        coordinates = newCoordinate;
    }
}

void Point3::ConvertTo(const sofa::Coordinates::Type &newCoordinate, const sofa::Units::Type &newUnit)
{
    ConvertTo( newCoordinate );
    ConvertTo( newUnit );
}
 */

const bool sofa::GetPoint3(sofa::Point3 &point3, const netCDF::NcVar & variable)
{
    const sofa::PositionVariable var( variable );
    if( var.IsValid() == false )
    {
        return false;
    }
    
    const bool ok        = sofa::NcUtils::GetValues( point3.data, 3,  variable );
    if( ok == false )
    {
        return false;
    }
    
    point3.units          = var.GetUnits();
    point3.coordinates    = var.GetCoordinates();
    
    return true;
}


