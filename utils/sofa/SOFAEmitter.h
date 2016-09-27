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
 *   @file       SOFAEmitter.h
 *   @brief      Represents a sofa emitter
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_EMITTER_H__
#define _SOFA_EMITTER_H__

#include "SOFAPosition.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @class          Emitter 
     *  @brief          Represents a sofa emitter
     *
     *  @details        Emitter is any acoustic excitation used for the measurement.
     *                  The number of emitters is not limited in SOFA.
     *                  The contribution of the particular emitter is described by the metadata (see later).
     *
     *  @details        Emitters and receivers have both their own coordinate system called local coordinate system.
     *                  The local coordinate system of emitter and receiver are defined relatively to the coordinate system
     *                  of the source and listener, respectively.
     *                  With the source and listener in the origin and at default orientation,
     *                  the local coordinate systems correspond to the global coordinate system.
     */
    /************************************************************************************/
    class SOFA_API Emitter
    {
    public:
        Emitter(const netCDF::NcVar & varEmitterPosition,
                const netCDF::NcVar & varEmitterUp,
                const netCDF::NcVar & varEmitterView);
        
        ~Emitter();
        
        const bool IsValid() const;
        
        const bool HasEmitterUpVariable() const;
        const bool HasEmitterViewVariable() const;
        
        const bool EmitterPositionHasDimensions(const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        const bool EmitterUpHasDimensions(const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        const bool EmitterViewHasDimensions(const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        
    protected:
        const sofa::PositionVariable EmitterPosition;
        const sofa::PositionVariable EmitterUp;
        const sofa::PositionVariable EmitterView;
        const bool hasVarEmitterUp;
        const bool hasVarEmitterView;
        
    private:
        //==============================================================================
        /// avoid shallow and copy constructor
        Emitter( const Emitter &other );
        const Emitter & operator= ( const Emitter &other );
        
    };
    
}

#endif /* _SOFA_EMITTER_H__ */ 

