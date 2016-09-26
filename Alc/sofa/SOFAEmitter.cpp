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
 *   @file       SOFAEmitter.cpp
 *   @brief      Represents a sofa emitter
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAEmitter.h"

using namespace sofa;

/************************************************************************************/
/*!
 *  @brief          Class constructor
 *  @param[in]      varEmitterPosition : the 'EmitterPosition' variable
 *  @param[in]      varEmitterUp : the 'EmitterUp' variable
 *  @param[in]      varEmitterView : the 'EmitterView' variable
 *
 */
/************************************************************************************/
Emitter::Emitter(const netCDF::NcVar & varEmitterPosition,
                 const netCDF::NcVar & varEmitterUp,
                 const netCDF::NcVar & varEmitterView)
: EmitterPosition( varEmitterPosition )
, EmitterUp( varEmitterUp )
, EmitterView( varEmitterView )
, hasVarEmitterUp( (varEmitterUp.isNull() == false ) ? (true) : (false) )
, hasVarEmitterView( (varEmitterView.isNull() == false ) ? (true) : (false) )
{
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
Emitter::~Emitter()
{
}

/************************************************************************************/
/*!
 *  @brief          Checks if the NcVar corresponds to
 *                    EmitterPosition
 *                        EmitterPosition:Type
 *                        EmitterPosition:Unit
 *                    EmitterUp (optional)
 *                        EmitterUp:Type
 *                        EmitterUp:Units
 *                    EmitterView (optional)
 *                        EmitterView:Type
 *                        EmitterView:Units
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
const bool Emitter::IsValid() const
{
    /// EmitterPosition
    if( EmitterPosition.IsValid() == false )
    {
        return false;
    }
    
    /// EmitterUp
    if( hasVarEmitterUp == true )
    {
        const bool shouldHaveTypeAndUnits = false;
        /// EmitterUp do not require a EmitterUp::Type and EmitterUp::Unit.
        /// it will use the EmitterView::Type and EmitterView::Unit
        
        if( EmitterUp.IsValid( shouldHaveTypeAndUnits )  == false )
        {
            return false;
        }
        
        
        /// EmitterView
        if( hasVarEmitterView == true )
        {
            const bool shouldHaveTypeAndUnits = true;
            /// EmitterView:Type and EmitterView:Units shall be ‘required’ when EmitterView or EmitterUp are used.
            
            if( EmitterView.IsValid( shouldHaveTypeAndUnits ) == false )
            {
                return false;
            }
        }
        else
        {
            /// EmitterView shall be ‘required’ when  EmitterUp is used.
            return false;
        }
    }
    
    return true;
}


/************************************************************************************/
/*!
 *  @brief          Returns true if the EmitterUp attribute is here and valid
 *
 */
/************************************************************************************/
const bool Emitter::HasEmitterUpVariable() const
{
    if( hasVarEmitterUp == true )
    {
        const bool shouldHaveTypeAndUnits = false;
        
        return EmitterUp.IsValid( shouldHaveTypeAndUnits );
        
        /// EmitterUp do not require a EmitterUp::Type and EmitterUp::Unit.
        /// it will use the EmitterView::Type and EmitterView::Unit
    }
    else
    {
        return false;
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns true if the EmitterView attribute is here and valid
 *
 */
/************************************************************************************/
const bool Emitter::HasEmitterViewVariable() const
{
    if( hasVarEmitterView == true )
    {
        return EmitterView.IsValid();
    }
    else
    {
        return false;
    }
}

const bool Emitter::EmitterPositionHasDimensions(const unsigned long dim1,
                                                   const unsigned long dim2,
                                                   const unsigned long dim3) const
{
    return EmitterPosition.HasDimensions( dim1, dim2, dim3 );
}

const bool Emitter::EmitterUpHasDimensions(const unsigned long dim1,
                                             const unsigned long dim2,
                                             const unsigned long dim3) const
{
    return EmitterUp.HasDimensions( dim1, dim2, dim3 );
}

const bool Emitter::EmitterViewHasDimensions(const unsigned long dim1,
                                               const unsigned long dim2,
                                               const unsigned long dim3) const
{
    return EmitterView.HasDimensions( dim1, dim2, dim3 );
}
