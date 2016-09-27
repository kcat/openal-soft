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
 *   @file       SOFACoordinates.cpp
 *   @brief      SOFA Coordinates systems
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFACoordinates.h"
#include "SOFANcUtils.h"
#include <map>

using namespace sofa;


namespace CoordinatesHelper
{
    static std::map< std::string, sofa::Coordinates::Type > typeMap;
    
    /************************************************************************************/
    /*!
     *  @brief          Creates a mapping between coordinates type and their names
     *
     */
    /************************************************************************************/
    static void initTypeMap()
    {
        if( typeMap.empty() == true )
        {    
            typeMap["cartesian"]                    = sofa::Coordinates::kCartesian;
            typeMap["spherical"]                    = sofa::Coordinates::kSpherical;
        }
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns the name of coordinate system based on its type
 *  @param[in]      type_ : the coordinate system to query
 *
 */
/************************************************************************************/
const std::string sofa::Coordinates::GetName(const sofa::Coordinates::Type &type_)
{
    switch( type_ )
    {
        case sofa::Coordinates::kCartesian              : return "cartesian";
        case sofa::Coordinates::kSpherical              : return "spherical";
            
        default                                         : SOFA_ASSERT( false ); return "";    
        case sofa::Coordinates::kNumCoordinatesTypes    : SOFA_ASSERT( false ); return "";    
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns the coordinate system based on its name
 *                  Returns 'sofa::Coordinates::kNumCoordinatesTypes' in case the string does not 
 *                  correspond to a valid coordinate system
 *  @param[in]      name : the string to query
 *
 */
/************************************************************************************/
const sofa::Coordinates::Type sofa::Coordinates::GetType(const std::string &name)
{
    CoordinatesHelper::initTypeMap();
    
    if( CoordinatesHelper::typeMap.count( name ) == 0 )
    {        
        SOFA_ASSERT( false );
        
        return sofa::Coordinates::kNumCoordinatesTypes;
    }
    else
    {
        return CoordinatesHelper::typeMap[ name ];
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns true if the string corresponds to a valid coordinate system
 *  @param[in]      name : the string to query
 *
 */
/************************************************************************************/
const bool sofa::Coordinates::IsValid(const std::string &name)
{
    CoordinatesHelper::initTypeMap();
    
    return ( CoordinatesHelper::typeMap.count( name ) != 0 );
}

/************************************************************************************/
/*!
 *  @brief          Returns true if a NcAtt properly represents a SOFA Coordinates
 *  @param[in]      attr : the Nc attribute to query
 *
 */
/************************************************************************************/
const bool sofa::Coordinates::IsValid(const netCDF::NcAtt & attr)
{
    if( sofa::NcUtils::IsValid( attr ) == false )
    {
        return false;
    }
    
    if( sofa::NcUtils::IsChar( attr ) == false )
    {
        return false;
    }
    
    const std::string positionType = sofa::NcUtils::GetAttributeValueAsString( attr );
    
    if( sofa::Coordinates::IsValid( positionType ) == false )
    {
        return false;
    }
    
    return true;
}


