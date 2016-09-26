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
 *   @file       SOFASource.cpp
 *   @brief      Represents a sofa source
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFASource.h"

using namespace sofa;

/************************************************************************************/
/*!
 *  @brief          Class constructor
 *  @param[in]      varSourcePosition : the 'SourcePosition' variable
 *  @param[in]      varSourceUp : the 'SourceUp' variable
 *  @param[in]      varSourceView : the 'SourceView' variable
 *
 */
/************************************************************************************/
Source::Source(const netCDF::NcVar & varSourcePosition,
               const netCDF::NcVar & varSourceUp,
               const netCDF::NcVar & varSourceView)
: SourcePosition( varSourcePosition )
, SourceUp( varSourceUp )
, SourceView( varSourceView )
, hasVarSourceUp( ( varSourceUp.isNull() == false ) ? (true) : (false) )
, hasVarSourceView( ( varSourceView.isNull() == false ) ? (true) : (false) )
{
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
Source::~Source()
{
}

/************************************************************************************/
/*!
 *  @brief          Checks if the NcVar corresponds to
 *                    SourcePosition
 *                        SourcePosition:Type
 *                        SourcePosition:Unit
 *                    SourceUp
 *                    SourceView
 *                        SourceView:Type
 *                        SourceView:Units
 *
 *                    Returns true if everything is conform to the specifications
 *                    False otherwise or if any error occured
 *  @param[in]      -
 *  @param[out]     -
 *  @param[in, out] -
 *  @return         -
 *
 *  @details
 *  @n                some of the tests are redundant, but anyway they should be rather fast
 */
/************************************************************************************/
const bool Source::IsValid() const
{
    /// SourcePosition
    if( SourcePosition.IsValid() == false )
    {
        return false;
    }
    
    
    /// SourceUp
    if( hasVarSourceUp == true )
    {
        const bool shouldHaveTypeAndUnits = false;
        /// SourceUp do not require a SourceUp::Type and SourceUp::Unit.
        /// it will use the SourceView::Type and SourceView::Unit
        
        if( SourceUp.IsValid( shouldHaveTypeAndUnits )  == false )
        {
            return false;
        }
        
        
        /// SourceView
        if( hasVarSourceView == true )
        {
            const bool shouldHaveTypeAndUnits = true;
            /// SourceView:Type and SourceView:Units shall be ‘required’ when SourceView or SourceUp are used.
            
            if( SourceView.IsValid( shouldHaveTypeAndUnits ) == false )
            {
                return false;
            }
        }
        else
        {
            /// SourceView shall be ‘required’ when  SourceUp is used.
            return false;
        }
    }
    
    return true;
}


/************************************************************************************/
/*!
 *  @brief          Returns true if the SourceUp attribute is here and valid
 *
 */
/************************************************************************************/
const bool Source::HasSourceUp() const
{
    if( hasVarSourceUp == true )
    {
        const bool shouldHaveTypeAndUnits = false;
        
        return SourceUp.IsValid( shouldHaveTypeAndUnits );
        
        /// SourceUp do not require a SourceUp::Type and SourceUp::Unit.
        /// it will use the SourceView::Type and SourceView::Unit
    }
    else
    {
        return false;
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns true if the SourceView attribute is here and valid
 *
 */
/************************************************************************************/
const bool Source::HasSourceView() const
{
    if( hasVarSourceView == true )
    {
        return SourceView.IsValid();
    }
    else
    {
        return false;
    }
}

const bool Source::SourcePositionHasDimensions(const unsigned long dim1,
                                                   const unsigned long dim2) const
{
    return SourcePosition.HasDimensions( dim1, dim2 );
}

const bool Source::SourceUpHasDimensions(const unsigned long dim1,
                                             const unsigned long dim2) const
{
    return SourceUp.HasDimensions( dim1, dim2 );
}

const bool Source::SourceViewHasDimensions(const unsigned long dim1,
                                               const unsigned long dim2) const
{
    return SourceView.HasDimensions( dim1, dim2 );
}

