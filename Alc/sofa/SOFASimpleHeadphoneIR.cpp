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
 *   @file       SOFASimpleHeadphoneIR.cpp
 *   @brief      Class for SOFA files with SimpleHeadphoneIR convention
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 *
 */
/************************************************************************************/
#include "SOFASimpleHeadphoneIR.h"
#include "SOFAUtils.h"
#include "SOFANcUtils.h"
#include "SOFAString.h"
#include "SOFAPoint3.h"
#include "SOFAListener.h"

using namespace sofa;

const unsigned int SimpleHeadphoneIR::ConventionVersionMajor  =   0;
const unsigned int SimpleHeadphoneIR::ConventionVersionMinor  =   1;

const std::string SimpleHeadphoneIR::GetConventionVersion()
{
    return sofa::String::Int2String( SimpleHeadphoneIR::ConventionVersionMajor ) + std::string(".") + sofa::String::Int2String( SimpleHeadphoneIR::ConventionVersionMinor );
}

/************************************************************************************/
/*!
 *  @brief          Class constructor
 *  @param[in]      path : the file path
 *  @param[in]      mode : opening mode
 *
 */
/************************************************************************************/
SimpleHeadphoneIR::SimpleHeadphoneIR(const std::string &path,
                                     const netCDF::NcFile::FileMode &mode)
: sofa::File( path, mode )
{
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
SimpleHeadphoneIR::~SimpleHeadphoneIR()
{
}

const bool SimpleHeadphoneIR::hasDatabaseName() const
{
    const netCDF::NcGroupAtt att = getAttribute( "DatabaseName" );
    
    return sofa::NcUtils::IsChar( att );
}

const bool SimpleHeadphoneIR::hasSourceModel() const
{
    const netCDF::NcGroupAtt att = getAttribute( "SourceModel" );
    
    return sofa::NcUtils::IsChar( att );
}

const bool SimpleHeadphoneIR::hasSourceManufacturer() const
{
    const netCDF::NcGroupAtt att = getAttribute( "SourceManufacturer" );
    
    return sofa::NcUtils::IsChar( att );
}

const bool SimpleHeadphoneIR::hasSourceURI() const
{
    const netCDF::NcGroupAtt att = getAttribute( "SourceURI" );
    
    return sofa::NcUtils::IsChar( att );
}

const bool SimpleHeadphoneIR::checkGlobalAttributes() const
{
    sofa::Attributes attributes;
    GetGlobalAttributes( attributes );
    
    if( attributes.Get( sofa::Attributes::kSOFAConventions ) != "SimpleHeadphoneIR" )
    {
        SOFA_THROW( "Not a 'SimpleHeadphoneIR' SOFAConvention" );
        return false;
    }
    
    if( attributes.Get( sofa::Attributes::kDataType ) != "FIR" )
    {
        SOFA_THROW( "invalid 'DataType'" );
        return false;
    }
    
    if( attributes.Get( sofa::Attributes::kRoomType ) != "free field" )
    {
        /// Room type is not relevant here
        SOFA_THROW( "invalid 'RoomType'" );
        return false;
    }
    
    return true;
}

const bool SimpleHeadphoneIR::checkListenerVariables() const
{
    const long I = GetDimension( "I" );
    if( I != 1 )
    {
        SOFA_THROW( "invalid SOFA dimension : I" );
        return false;
    }
    
    const long C = GetDimension( "C" );
    if( C != 3 )
    {
        SOFA_THROW( "invalid SOFA dimension : C" );
        return false;
    }
    
    const long M = GetNumMeasurements();
    if( M <= 0 )
    {
        SOFA_THROW( "invalid SOFA dimension : M" );
        return false;
    }
    
    const netCDF::NcVar varListenerPosition        = NetCDFFile::getVariable( "ListenerPosition" );
    const netCDF::NcVar varListenerUp              = NetCDFFile::getVariable( "ListenerUp" );
    const netCDF::NcVar varListenerView            = NetCDFFile::getVariable( "ListenerView" );
    
    const sofa::Listener listener( varListenerPosition, varListenerUp, varListenerView );
    
    if( listener.IsValid() == false )
    {
        SOFA_THROW( "invalid 'Listener' variables" );
        return false;
    }
    
    if( listener.ListenerPositionHasDimensions(  I,  C ) == false
       && listener.ListenerPositionHasDimensions(  M,  C ) == false )
    {
        SOFA_THROW( "invalid 'ListenerPosition' dimensions" );
        return false;
    }
    
    if( listener.HasListenerUp() == true )
    {
        /// ListenerUp is not required in the Specifications
        /// but if it is present, is should be [ I C ] or [ M C ]
        
        if( listener.ListenerUpHasDimensions(  I,  C ) == false
           && listener.ListenerUpHasDimensions(  M,  C ) == false )
        {
            SOFA_THROW( "invalid 'ListenerUp' dimensions" );
            return false;
        }
    }
    /// 'ListenerUp' is not required in this convention
    
    if( listener.HasListenerView() == true )
    {
        /// ListenerView is not required in the Specifications
        /// but if it is present, is should be [ I C ] or [ M C ]
        
        if( listener.ListenerViewHasDimensions(  I,  C ) == false
           && listener.ListenerViewHasDimensions(  M,  C ) == false )
        {
            SOFA_THROW( "invalid 'ListenerView' dimensions" );
            return false;
        }
    }
    /// 'ListenerView' is not required in this convention
    
    /// everything is OK !
    return true;
}

/************************************************************************************/
/*!
 *  @brief          Returns true if this is a valid SOFA file with SimpleHeadphoneIR convention
 *
 */
/************************************************************************************/
const bool SimpleHeadphoneIR::IsValid() const
{
    if( sofa::File::IsValid() == false )
    {
        return false;
    }
    
    if( hasDatabaseName() == false )
    {
        SOFA_THROW( "missing 'DatabaseName' global attribute" );
        return false;
    }
    
    if( hasSourceModel() == false )
    {
        SOFA_THROW( "missing 'SourceModel' global attribute" );
        return false;
    }
    
    if( hasSourceManufacturer() == false )
    {
        SOFA_THROW( "missing 'SourceManufacturer' global attribute" );
        return false;
    }
    
    if( hasSourceURI() == false )
    {
        SOFA_THROW( "missing 'SourceURI' global attribute" );
        return false;
    }
    
    if( IsFIRDataType() == false )
    {
        SOFA_THROW( "'DataType' shall be FIR" );
        return false;
    }
    
    if( checkGlobalAttributes() == false )
    {
        return false;
    }
    
    
    /// One-to-one correspondence between emitters and receivers
    if( GetNumEmitters() != GetNumReceivers() )
    {
        SOFA_THROW( "invalid number of emitters/receivers" );
        return false;
    }
    
    /*
    /// SamplingRate is a scalar
    {
        ///@n the AES69-2015 standard is not completely clear on that point.
        /// I tend to think that Data.SamplingRate shall be a scalar in the SimpleFreeFieldHRIR convention
        /// (sofaconventions.org confirms that), but it's not 100% clear
        
        if( VariableIsScalar( "Data.SamplingRate" ) == false )
        {
            SOFA_THROW( "invalid dimensionality for 'Data.SamplingRate'");
            return false;
        }
        
        if( HasVariableType( netCDF::NcType::nc_DOUBLE, "Data.SamplingRate") == false )
        {
            SOFA_THROW( "invalid type for 'Data.SamplingRate'" );
            return false;
        }
    }
    */
    
    if( checkListenerVariables() == false )
    {
        return false;
    }
    
    
    SOFA_ASSERT( GetDimension( "I" ) == 1 );
    SOFA_ASSERT( GetDimension( "C" ) == 3 );
    
    return true;
}

/************************************************************************************/
/*!
 *  @brief          The Data.SamplingRate variable can be either [I] or [M],
 *                  according to the specifications.
 *                  This function returns true if Data.SamplingRate is [I]
 *
 */
/************************************************************************************/
const bool SimpleHeadphoneIR::isSamplingRateScalar() const
{
    return VariableIsScalar( "Data.SamplingRate" ) == true
    && HasVariableType( netCDF::NcType::nc_DOUBLE, "Data.SamplingRate");
}

/************************************************************************************/
/*!
 *  @brief          In case Data.SamplingRate is of dimension [I], this function returns
 *                  its value. In case Data.SamplingRate is of dimension [M], an error is thrown
 *  @return         true on success
 *
 */
/************************************************************************************/
const bool SimpleHeadphoneIR::GetSamplingRate(double &value) const
{
    SOFA_ASSERT( SimpleHeadphoneIR::IsValid() == true );
    
    if( isSamplingRateScalar() == true )
    {
        const netCDF::NcVar var = getVariable( "Data.SamplingRate" );
        
        return sofa::NcUtils::GetValue( value, var );
    }
    else
    {
        SOFA_THROW( "'Data.SamplingRate' is not a scalar" );
        return false;
    }
}

/************************************************************************************/
/*!
 *  @brief          Retrieves the units of the Data.SamplingRate variable
 *  @return         true on success
 *
 */
/************************************************************************************/
const bool SimpleHeadphoneIR::GetSamplingRateUnits(sofa::Units::Type &units) const
{
    const netCDF::NcVar var = getVariable( "Data.SamplingRate" );
    
    const netCDF::NcVarAtt attNUnits    = sofa::NcUtils::GetAttribute( var, "Units" );
    const std::string unitsName         = sofa::NcUtils::GetAttributeValueAsString( attNUnits );
    
    units = sofa::Units::GetType( unitsName );
    
    return true;
}

/************************************************************************************/
/*!
 *  @brief          Retrieves the Data.IR values
 *  @param[in]      values : array containing the values.
 *                  The array must be allocated large enough
 *  @param[in]      dim1 : first dimension (M)
 *  @param[in]      dim2 : second dimension (R)
 *  @param[in]      dim3 : third dimension (N)
 *  @return         true on success
 *
 */
/************************************************************************************/
const bool SimpleHeadphoneIR::GetDataIR(double *values, const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const
{
    return NetCDFFile::GetValues( values, dim1, dim2, dim3, "Data.IR" );
}

/************************************************************************************/
/*!
 *  @brief          Retrieves the Data.IR values
 *  @param[in]      values : the array is resized if needed
 *  @return         true on success
 *
 */
/************************************************************************************/
const bool SimpleHeadphoneIR::GetDataIR(std::vector< double > &values) const
{
    const long M = GetNumMeasurements();
    const long R = GetNumReceivers();
    const long N = GetNumDataSamples();
    
    SOFA_ASSERT( M > 0 );
    SOFA_ASSERT( R > 0 );
    SOFA_ASSERT( N > 0 );
    
    const std::size_t size_ = M * R * N;
    
    values.resize( size_ );
    
    SOFA_ASSERT( values.empty() == false );
    
    return GetDataIR( &values[0], M, R, N );
}

const bool SimpleHeadphoneIR::GetDataDelay(double *values, const unsigned long dim1, const unsigned long dim2) const
{
    return NetCDFFile::GetValues( values, dim1, dim2, "Data.Delay" );
}

