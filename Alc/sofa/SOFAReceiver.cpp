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
 *   @file       SOFAReceiver.cpp
 *   @brief      
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAReceiver.h"

using namespace sofa;

/************************************************************************************/
/*!
 *  @brief          Class constructor
 *  @param[in]      varReceiverPosition : the 'ReceiverPosition' variable
 *  @param[in]      varReceiverUp : the 'ReceiverUp' variable
 *  @param[in]      varReceiverView : the 'ReceiverView' variable
 *
 */
/************************************************************************************/
Receiver::Receiver(const netCDF::NcVar & varReceiverPosition,
                   const netCDF::NcVar & varReceiverUp,
                   const netCDF::NcVar & varReceiverView)
: ReceiverPosition( varReceiverPosition )
, ReceiverUp( varReceiverUp )
, ReceiverView( varReceiverView )
, hasVarReceiverUp( (varReceiverUp.isNull() == false ) ? (true) : (false) )
, hasVarReceiverView( (varReceiverView.isNull() == false  ) ? (true) : (false) )
{
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
Receiver::~Receiver()
{
}

/************************************************************************************/
/*!
 *  @brief          Checks if the NcVar corresponds to 
 *                    ReceiverPosition 
 *                        ReceiverPosition:Type
 *                        ReceiverPosition:Unit
 *                    ReceiverUp (optional)
 *                        ReceiverUp:Type
 *                        ReceiverUp:Units
 *                    ReceiverView (optional)
 *                        ReceiverView:Type
 *                        ReceiverView:Units
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
const bool Receiver::IsValid() const
{
    /// ReceiverPosition
    if( ReceiverPosition.IsValid() == false )
    {
        return false;
    }
    
    /// ReceiverUp
    if( hasVarReceiverUp == true )
    {
        const bool shouldHaveTypeAndUnits = false;
        /// ReceiverUp do not require a ReceiverUp::Type and ReceiverUp::Unit.
        /// it will use the ReceiverView::Type and ReceiverView::Unit
        
        if( ReceiverUp.IsValid( shouldHaveTypeAndUnits )  == false )
        {
            return false;
        }
        
        
        /// ReceiverView
        if( hasVarReceiverView == true )
        {
            const bool shouldHaveTypeAndUnits = true;
            /// ReceiverView:Type and ReceiverView:Units shall be ‘required’ when ReceiverView or ReceiverUp are used.
            
            if( ReceiverView.IsValid( shouldHaveTypeAndUnits ) == false )
            {
                return false;
            }
        }
        else
        {
            /// ReceiverView shall be ‘required’ when  ReceiverUp is used.
            return false;
        }
    }
    
    return true;
}


/************************************************************************************/
/*!
 *  @brief          Returns true if the ReceiverUp attribute is here and valid
 *
 */
/************************************************************************************/
const bool Receiver::HasReceiverUpVariable() const
{
    if( hasVarReceiverUp == true )
    {
        const bool shouldHaveTypeAndUnits = false;
        
        return ReceiverUp.IsValid( shouldHaveTypeAndUnits );
        
        /// ReceiverUp do not require a ReceiverUp::Type and ReceiverUp::Unit.
        /// it will use the ReceiverView::Type and ReceiverView::Unit
    }
    else
    {
        return false;
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns true if the ReceiverView attribute is here and valid
 *
 */
/************************************************************************************/
const bool Receiver::HasReceiverViewVariable() const
{
    if( hasVarReceiverView == true )
    {
        return ReceiverView.IsValid();
    }
    else
    {
        return false;
    }
}

const bool Receiver::ReceiverPositionHasDimensions(const unsigned long dim1,
                                                   const unsigned long dim2,
                                                   const unsigned long dim3) const
{
    return ReceiverPosition.HasDimensions( dim1, dim2, dim3 );
}

const bool Receiver::ReceiverUpHasDimensions(const unsigned long dim1,
                                             const unsigned long dim2,
                                             const unsigned long dim3) const
{
    return ReceiverUp.HasDimensions( dim1, dim2, dim3 );
}

const bool Receiver::ReceiverViewHasDimensions(const unsigned long dim1,
                                               const unsigned long dim2,
                                               const unsigned long dim3) const
{
    return ReceiverView.HasDimensions( dim1, dim2, dim3 );
}
