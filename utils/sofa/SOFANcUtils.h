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
 *   @file       SOFANcUtils.h
 *   @brief      Utility functions to manipulate NetCDF elements (NcAtt, NcVar, NcDim, ...)
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_NC_UTILS_H__
#define _SOFA_NC_UTILS_H__

#include "SOFAPlatform.h"
#include "netcdf.h"
#include "ncVar.h"
#include "ncDim.h"
#include "ncAtt.h"
#include "ncException.h"

namespace sofa
{
    
    namespace NcUtils
    {
                
        /************************************************************************************/
        /*!
         *  @brief          Checks if a NcVar or NcAtt or NcDim is valid
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/  
        template< typename NetCDFType >
        inline const bool IsValid(const NetCDFType &ncStuff)
        {
            return ( ncStuff.isNull() == false );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Checks if a NcVar or NcAtt is of a given type
         *  @param[in]      ncStuff : the stuff to query
         *  @param[in]      type_ : the Nc type to test
         *
         */
        /************************************************************************************/ 
        template< typename NetCDFType >
        inline const bool CheckType(const NetCDFType &ncStuff,
                                    const netCDF::NcType &type_)
        {
            return ( IsValid( ncStuff ) == true && ncStuff.getType() == type_ );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns true if a NcVar or NcAtt is of type nc_FLOAT
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/  
        template< typename NetCDFType >
        inline const bool IsFloat(const NetCDFType & ncStuff)
        {
            return CheckType( ncStuff, netCDF::NcType::nc_FLOAT );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns true if a NcVar or NcAtt is of type nc_DOUBLE
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/
        template< typename NetCDFType >
        inline const bool IsDouble(const NetCDFType & ncStuff)
        {
            return CheckType( ncStuff, netCDF::NcType::nc_DOUBLE );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns true if a NcVar or NcAtt is of type nc_BYTE
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/
        template< typename NetCDFType >
        inline const bool IsByte(const NetCDFType & ncStuff)
        {
            return CheckType( ncStuff, netCDF::NcType::nc_BYTE );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns true if a NcVar or NcAtt is of type nc_CHAR
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/
        template< typename NetCDFType >
        inline const bool IsChar(const NetCDFType & ncStuff)
        {
            return CheckType( ncStuff, netCDF::NcType::nc_CHAR );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns true if a NcVar or NcAtt is of type nc_SHORT
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/
        template< typename NetCDFType >
        inline const bool IsShort(const NetCDFType & ncStuff)
        {
            return CheckType( ncStuff, netCDF::NcType::nc_SHORT );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns true if a NcVar or NcAtt is of type nc_INT
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/
        template< typename NetCDFType >
        inline const bool IsInt(const NetCDFType & ncStuff)
        {
            return CheckType( ncStuff, netCDF::NcType::nc_INT );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns true if a NcVar or NcAtt is of type nc_INT64
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/
        template< typename NetCDFType >
        inline const bool IsInt64(const NetCDFType & ncStuff)
        {
            return CheckType( ncStuff, netCDF::NcType::nc_INT64 );
        }
        
            
        /************************************************************************************/
        /*!
         *  @brief          Returns the value an NcAtt, as a string;
         *  @param[in]      attr : the attribute to query
         *
         */
        /************************************************************************************/ 
        inline const std::string GetAttributeValueAsString(const netCDF::NcAtt & attr)
        {
            if( IsChar( attr ) == false )
            {
                return std::string();
            }
            else
            {
                std::string value;
                attr.getValues( value );
                return value;
            }
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns the name of an NcAtt or NcDim or NcVar.
         *                  Returns an empty string if an error occured
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/  
        template< typename NetCDFType >
        inline const std::string GetName(const NetCDFType & ncStuff)
        {
            if( IsValid( ncStuff ) == false )
            {
                return std::string();
            }
            else
            {
                return ncStuff.getName();
            }        
        }
                
        /************************************************************************************/
        /*!
         *  @brief          Returns the NcType of an NcVar or NcAtt.
         *                  Return a null object if an error occured
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/  
        template< typename NetCDFType >
        inline const netCDF::NcType GetType(const NetCDFType & ncStuff)
        {
            if( IsValid( ncStuff ) == false )
            {
                return netCDF::NcType();
            }
            else
            {
                return ncStuff.getType();
            }

        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns the dimensionality of an NcVar.
         *                  Return -1 if an error occured
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/  
        inline const int GetDimensionality(const netCDF::NcVar & ncStuff)
        {
            if( IsValid( ncStuff ) == false )
            {
                return -1;
            }
            else
            {
                return ncStuff.getDimCount();
            }
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns true if a given NcVar is a scalar (i.e. dimensionality = 1 and dimension = 1)
         *  @param[in]      ncStuff : the stuff to query
         *
         */
        /************************************************************************************/
        inline const bool IsScalar(const netCDF::NcVar & ncStuff)
        {
            if( GetDimensionality( ncStuff ) != 1 )
            {
                return false;
            }
            else
            {
                const netCDF::NcDim dim = ncStuff.getDim( 0 );
                const std::size_t size  = dim.getSize();
                
                return ( size == 1 );
            }
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Retrieves the value of a NcVar, as double.
         *                  This assumes the NcVar is scalar, of type double;
         *  @param[in]      ncStuff : the stuff to query
         *  @param[out]     value : the requested value
         *
         */
        /************************************************************************************/
        inline const bool GetValue(double &value, const netCDF::NcVar & ncStuff)
        {
            if( IsScalar( ncStuff ) == true && IsDouble( ncStuff ) == true )
            {
                ncStuff.getVar( &value );
                 
                return true;
            }
            else
            {
                return false;
            }
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns the dimensions of a NcVar
         *                  Returns an empty vector if an error occured
         *  @param[in]      var : the Nc variable to query
         *  @param[out]     dims :
         *
         *  @details        the vector dims is resized accordingly to the dimensionality of the variable
         */
        /************************************************************************************/
        inline void GetDimensions(std::vector< std::size_t > &dims, const netCDF::NcVar & var)
        {
            if( IsValid( var ) == false )
            {
                dims.clear();
                return;
            }
            
            const int numDimensions = var.getDimCount();
            
            SOFA_ASSERT( numDimensions > 0 );
            
            dims.resize( numDimensions );
            
            for( int i = 0; i < numDimensions; i++ )
            {
                const netCDF::NcDim dimension_ = var.getDim( i );
                
                SOFA_ASSERT( sofa::NcUtils::IsValid( dimension_ ) == true );
                
                dims[i] = dimension_.getSize();
            }
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Returns the dimensions names of a NcVar
         *                  Returns an empty vector if an error occured
         *  @param[in]      var : the Nc variable to query
         *  @param[out]     dims :
         *
         *  @details        the vector dims is resized accordingly to the dimensionality of the variable
         */
        /************************************************************************************/
        inline void GetDimensionsNames(std::vector< std::string > &dims, const netCDF::NcVar & var)
        {
            if( IsValid( var ) == false )
            {
                dims.clear();
                return;
            }
            
            const int numDimensions = var.getDimCount();
            
            SOFA_ASSERT( numDimensions > 0 );
            
            dims.resize( numDimensions );
            
            for( int i = 0; i < numDimensions; i++ )
            {
                const netCDF::NcDim dimension_ = var.getDim( i );
                
                SOFA_ASSERT( sofa::NcUtils::IsValid( dimension_ ) == true );
                
                dims[i] = dimension_.getName();
            }
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Retrieves the values of a NcVar, as double.
         *                  This assumes the NcVar has 'numValues' values, of type double;
         *  @param[in]      ncStuff : the stuff to query
         *  @param[in]      numValues : number of values to read
         *  @param[out]     values : the requested values
         *
         */
        /************************************************************************************/
        inline const bool GetValues(double * values,
                                    const std::size_t numValues,
                                    const netCDF::NcVar & ncStuff)
        {
            
            if( IsValid( ncStuff ) == true && IsDouble( ncStuff ) == true  )
            {
                /// dimensionality might be 2 for instance for a [I C] variable
                std::vector< std::size_t > dims;
                sofa::NcUtils::GetDimensions( dims, ncStuff );
                
                if( dims.size() == 1 )
                {
                    if( dims[0] == numValues )
                    {
                        ncStuff.getVar( values );
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                else if( dims.size() == 2 )
                {
                    if( ( dims[0] == numValues && dims[1] == 1 )
                       ||
                        ( dims[0] == 1 && dims[1] == numValues )
                       )
                    {
                        ncStuff.getVar( values );
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return false;
            }

        }
        
        /************************************************************************************/
        /*!
         *  @brief          Retrieves if a variable has a given attribute
         *  @param[in]      var : the Nc variable to query
         *  @param[in]      attributeName : the name of the attribute
         *
         */
        /************************************************************************************/
        inline const bool HasAttribute(const netCDF::NcVar & var,
                                       const std::string & attributeName)
        {
            if( IsValid( var ) == false )
            {
                return false;
            }
            else
            {
                const std::map< std::string, netCDF::NcVarAtt > attributes = var.getAtts();
                
                for( std::map< std::string, netCDF::NcVarAtt >::const_iterator it = attributes.begin();
                    it != attributes.end();
                    ++it )
                {
                    const std::string attrName      = (*it).first;
                    
                    if( attrName == attributeName )
                    {
                        return true;
                    }
                }
                
                return false;
            }
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Retrieves the attribute of a given variable, if it exists
         *                  Returns a null object otherwise
         *  @param[in]      var : the Nc variable to query
         *  @param[in]      attributeName : the name of the attribute we want to retrieve
         *
         */
        /************************************************************************************/ 
        inline const netCDF::NcVarAtt GetAttribute(const netCDF::NcVar & var,
                                                   const std::string & attributeName)
        {
            if( IsValid( var ) == false )
            {
                return netCDF::NcVarAtt();
            }
            else
            {
                if( sofa::NcUtils::HasAttribute( var, attributeName ) == true )
                {
                    return var.getAtt( attributeName );
                }
                else
                {
                    return netCDF::NcVarAtt();
                }
            }
        }
        

        
        /************************************************************************************/
        /*!
         *  @brief          Checks if a NcVar has one dimension and if this dimension is equal to dim
         *                  Returns false if an error occured, if the NcVar is not valid, if the variable has
         *                  not the appropriate dimensionality or if the dimension does not match
         *  @param[in]      var : the Nc variable to query
         *  @param[in]      dim : the dimension to check
         *
         */
        /************************************************************************************/
        inline const bool HasDimension(const std::size_t dim, const netCDF::NcVar & var)
        {
            if( GetDimensionality( var ) != 1 )
            {
                return false;
            }
            
            std::vector< std::size_t > dims;
            GetDimensions( dims, var );
            
            SOFA_ASSERT( dims.size() == 1 );
            
            return ( dims[0] == dim );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Checks if a NcVar has two dimensions and if these dimensions match (dim1, dim2)
         *                  Returns false if an error occured, if the NcVar is not valid, if the variable has
         *                  not the appropriate dimensionality or if the dimensions do not match
         *  @param[in]      var : the Nc variable to query
         *  @param[in]      dim1 : the first dimension to check
         *  @param[in]      dim2 : the second dimension to check
         *
         */
        /************************************************************************************/
        inline const bool HasDimensions(const std::size_t dim1,
                                        const std::size_t dim2,
                                        const netCDF::NcVar & var)
        {
            if( GetDimensionality( var ) != 2 )
            {
                return false;
            }
            
            std::vector< std::size_t > dims;
            GetDimensions( dims, var );
            
            SOFA_ASSERT( dims.size() == 2 );
            
            return ( dims[0] == dim1 && dims[1] == dim2 );
        }
        
        /************************************************************************************/
        /*!
         *  @brief          Checks if a NcVar has three dimensions and if these dimensions match (dim1, dim2, dim3)
         *                  Returns false if an error occured, if the NcVar is not valid, if the variable has
         *                  not the appropriate dimensionality or if the dimensions do not match
         *  @param[in]      var : the Nc variable to query
         *  @param[in]      dim1 : the first dimension to check
         *  @param[in]      dim2 : the second dimension to check
         *  @param[in]      dim3 : the third dimension to check
         *
         */
        /************************************************************************************/
        inline const bool HasDimensions(const std::size_t dim1,
                                        const std::size_t dim2,
                                        const std::size_t dim3,
                                        const netCDF::NcVar & var)
        {
            if( GetDimensionality( var ) != 3 )
            {
                return false;
            }
            
            std::vector< std::size_t > dims;
            GetDimensions( dims, var );
            
            SOFA_ASSERT( dims.size() == 3 );
            
            return ( dims[0] == dim1 && dims[1] == dim2 && dims[2] == dim3 );
        }
        
        
    }
}

#endif /* _SOFA_NC_UTILS_H__ */ 

