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
 *   @file       SOFAListener.h
 *   @brief      Represents a sofa listener
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_LISTENER_H__
#define _SOFA_LISTENER_H__

#include "SOFAPosition.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @class          Listener 
     *  @brief          Represents a sofa listener
     *
     *  @details        Source and listener are defined in the coordinate system of the room, called global coordinate system.
     */
    /************************************************************************************/
    class SOFA_API Listener
    {
    public:
        Listener(const netCDF::NcVar &varListenerPosition,
                 const netCDF::NcVar &varListenerUp,
                 const netCDF::NcVar &varListenerView);
        
        ~Listener();
                
        const bool IsValid() const;
        
        const bool HasListenerUp() const;
        const bool HasListenerView() const;
        
        const bool ListenerPositionHasDimensions(const unsigned long dim1, const unsigned long dim2) const;        
        const bool ListenerUpHasDimensions(const unsigned long dim1, const unsigned long dim2) const;        
        const bool ListenerViewHasDimensions(const unsigned long dim1, const unsigned long dim2) const;        
        
    protected:    
        const sofa::PositionVariable ListenerPosition;
        const sofa::PositionVariable ListenerUp;
        const sofa::PositionVariable ListenerView;
        
        const bool hasVarListenerUp;    ///< flag to check if a NcVar was provided for ListenerUp
        const bool hasVarListenerView;  ///< flag to check if a NcVar was provided for ListenerView
        
    private:
        //==============================================================================
        /// avoid shallow and copy constructor
        Listener( const Listener &other );                    
        const Listener & operator= ( const Listener &other );

    };
    
}

#endif /* _SOFA_LISTENER_H__ */ 

