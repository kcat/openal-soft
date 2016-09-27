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
 *   @file       SOFAReceiver.h
 *   @brief      Represents a sofa receiver
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_RECEIVER_H__
#define _SOFA_RECEIVER_H__

#include "SOFAPosition.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @class          Receiver 
     *  @brief          Represents a sofa receiver
     *
     *  @details        Receiver is any acoustic sensor like the ear or a microphone.
     *                  The number of receivers in not limited in SOFA and defines the size of the data matrix.
     *
     *  @details        Emitters and receivers have both their own coordinate system called local coordinate system.
     *                  The local coordinate system of emitter and receiver are defined relatively to the coordinate system
     *                  of the source and listener, respectively.
     *                  With the source and listener in the origin and at default orientation,
     *                  the local coordinate systems correspond to the global coordinate system.
     */
    /************************************************************************************/
    class SOFA_API Receiver
    {
    public:
        Receiver(const netCDF::NcVar & varReceiverPosition,
                 const netCDF::NcVar & varReceiverUp,
                 const netCDF::NcVar & varReceiverView);
        
        ~Receiver();
        
        const bool IsValid() const;
        
        const bool HasReceiverUpVariable() const;
        const bool HasReceiverViewVariable() const;
        
        const bool ReceiverPositionHasDimensions(const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;        
        const bool ReceiverUpHasDimensions(const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;        
        const bool ReceiverViewHasDimensions(const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;    
        
    protected:
        const sofa::PositionVariable ReceiverPosition;
        const sofa::PositionVariable ReceiverUp;
        const sofa::PositionVariable ReceiverView;
        const bool hasVarReceiverUp;
        const bool hasVarReceiverView;
        
    private:
        //==============================================================================
        /// avoid shallow and copy constructor
        Receiver( const Receiver &other );                    
        const Receiver & operator= ( const Receiver &other );
        
    };
    
}

#endif /* _SOFA_RECEIVER_H__ */ 

