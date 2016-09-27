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
 *   @file       SOFAListener.cpp
 *   @brief      Represents a sofa listener
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAListener.h"

using namespace sofa;

/************************************************************************************/
/*!
 *  @brief          Class constructor
 *  @param[in]      varListenerPosition : the 'ListenerPosition' variable
 *  @param[in]      varListenerUp : the 'ListenerUp' variable
 *  @param[in]      varListenerView : the 'ListenerView' variable
 *
 */
/************************************************************************************/
Listener::Listener(const netCDF::NcVar & varListenerPosition,
                   const netCDF::NcVar & varListenerUp,
                   const netCDF::NcVar & varListenerView)
: ListenerPosition( varListenerPosition )
, ListenerUp( varListenerUp )
, ListenerView( varListenerView )
, hasVarListenerUp( ( varListenerUp.isNull() == false ) ? (true) : (false) )
, hasVarListenerView( ( varListenerView.isNull() == false ) ? (true) : (false) )
{
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
Listener::~Listener()
{
}

/************************************************************************************/
/*!
 *  @brief          Checks if the NcVar corresponds to 
 *                    ListenerPosition 
 *                        ListenerPosition:Type
 *                        ListenerPosition:Unit
 *                    ListenerUp 
 *                    ListenerView
 *                        ListenerView:Type
 *                        ListenerView:Units
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
const bool Listener::IsValid() const
{
    /// ListenerPosition    
    if( ListenerPosition.IsValid() == false )
    {
        return false;
    }
    
    
    /// ListenerUp
    if( hasVarListenerUp == true )
    {
        const bool shouldHaveTypeAndUnits = false;
        /// ListenerUp do not require a ListenerUp::Type and ListenerUp::Unit.
        /// it will use the ListenerView::Type and ListenerView::Unit
        
        if( ListenerUp.IsValid( shouldHaveTypeAndUnits )  == false )
        {
            return false;
        }
        
        
        /// ListenerView
        if( hasVarListenerView == true )
        {
            const bool shouldHaveTypeAndUnits = true;
            /// ListenerView:Type and ListenerView:Units shall be ‘required’ when ListenerView or ListenerUp are used.
            
            if( ListenerView.IsValid( shouldHaveTypeAndUnits ) == false )
            {
                return false;
            }
        }
        else
        {
            /// ListenerView shall be ‘required’ when  ListenerUp is used.
            return false;
        }
    }

    return true;
}


/************************************************************************************/
/*!
 *  @brief          Returns true if the ListenerUp attribute is here and valid
 *
 */
/************************************************************************************/
const bool Listener::HasListenerUp() const
{
    if( hasVarListenerUp == true )
    {
        const bool shouldHaveTypeAndUnits = false;
        
        return ListenerUp.IsValid( shouldHaveTypeAndUnits );
        
        /// ListenerUp do not require a ListenerUp::Type and ListenerUp::Unit.
        /// it will use the ListenerView::Type and ListenerView::Unit
    }
    else
    {
        return false;
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns true if the ListenerView attribute is here and valid
 *
 */
/************************************************************************************/
const bool Listener::HasListenerView() const
{
    if( hasVarListenerView == true )
    {
        return ListenerView.IsValid();
    }
    else
    {
        return false;
    }
}

const bool Listener::ListenerPositionHasDimensions(const unsigned long dim1,
                                                   const unsigned long dim2) const
{
    return ListenerPosition.HasDimensions( dim1, dim2 );
}

const bool Listener::ListenerUpHasDimensions(const unsigned long dim1,
                                             const unsigned long dim2) const
{
    return ListenerUp.HasDimensions( dim1, dim2 );
}

const bool Listener::ListenerViewHasDimensions(const unsigned long dim1,
                                               const unsigned long dim2) const
{
    return ListenerView.HasDimensions( dim1, dim2 );
}


