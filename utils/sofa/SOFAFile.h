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
 *   @file       SOFAFile.h
 *   @brief      Class for SOFA files
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_FILE_H__
#define _SOFA_FILE_H__

#include "SOFANcFile.h"
#include "SOFAAttributes.h"
#include "SOFACoordinates.h"
#include "SOFAUnits.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @class          File 
     *  @brief          Represents a sofa file
     *
     *  @details        Provides methods specific to SOFA files
     */
    /************************************************************************************/
    class SOFA_API File : public sofa::NetCDFFile
    {
    public:
        File(const std::string &path,
             const netCDF::NcFile::FileMode &mode = netCDF::NcFile::read);
        
        virtual ~File();
        
        virtual const bool IsValid() const SOFA_OVERRIDE;
                
        //==============================================================================
        // SOFA Attributes
        //==============================================================================
        const bool HasAttribute(const sofa::Attributes::Type &type_) const;
                
        void GetGlobalAttributes(sofa::Attributes &attributes) const;
        
        void PrintSOFAGlobalAttributes(std::ostream & output = std::cout,
                                       const bool withPadding = false) const;
        
        const std::string GetSOFAConventions() const;
        
        const bool IsFIRDataType() const;
        const bool IsTFDataType() const;
        const bool IsSOSDataType() const;
        
        //==============================================================================
        // SOFA Dimensions
        //==============================================================================
        const long GetNumMeasurements() const;
        const long GetNumReceivers() const;
        const long GetNumEmitters() const;
        const long GetNumDataSamples() const;
        
        void PrintSOFADimensions(std::ostream & output = std::cout,
                                 const bool withPadding = false) const;

        //==============================================================================
        // SOFA Variables
        //==============================================================================
        const bool GetListenerPosition(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool GetListenerUp(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool GetListenerView(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;

        const bool GetReceiverPosition(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool GetReceiverUp(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool GetReceiverView(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool HasReceiverUp() const;
        const bool HasReceiverView() const;

        const bool GetSourcePosition(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool GetSourceUp(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool GetSourceView(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool HasSourceUp() const;
        const bool HasSourceView() const;
        
        const bool GetEmitterPosition(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool GetEmitterUp(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool GetEmitterView(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units) const;
        const bool HasEmitterUp() const;
        const bool HasEmitterView() const;
        
        const bool GetListenerPosition(double *values, const unsigned long dim1, const unsigned long dim2) const;
        const bool GetListenerUp(double *values, const unsigned long dim1, const unsigned long dim2) const;
        const bool GetListenerView(double *values, const unsigned long dim1, const unsigned long dim2) const;
        
        const bool GetSourcePosition(double *values, const unsigned long dim1, const unsigned long dim2) const;
        const bool GetSourceUp(double *values, const unsigned long dim1, const unsigned long dim2) const;
        const bool GetSourceView(double *values, const unsigned long dim1, const unsigned long dim2) const;
        
        const bool GetReceiverPosition(double *values, const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        const bool GetReceiverUp(double *values, const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        const bool GetReceiverView(double *values, const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        
        const bool GetEmitterPosition(double *values, const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        const bool GetEmitterUp(double *values, const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        const bool GetEmitterView(double *values, const unsigned long dim1, const unsigned long dim2, const unsigned long dim3) const;
        
        //==============================================================================
        const bool GetListenerPosition(std::vector< double > &values) const;
        const bool GetListenerUp(std::vector< double > &values) const;
        const bool GetListenerView(std::vector< double > &values) const;
        
        const bool GetSourcePosition(std::vector< double > &values) const;
        const bool GetSourceUp(std::vector< double > &values) const;
        const bool GetSourceView(std::vector< double > &values) const;
        
        const bool GetReceiverPosition(std::vector< double > &values) const;
        const bool GetReceiverUp(std::vector< double > &values) const;
        const bool GetReceiverView(std::vector< double > &values) const;
        
        const bool GetEmitterPosition(std::vector< double > &values) const;
        const bool GetEmitterUp(std::vector< double > &values) const;
        const bool GetEmitterView(std::vector< double > &values) const;
        
    protected:
        const bool hasSOFAConvention() const;
        const bool hasSOFARequiredAttributes() const;
        const bool hasSOFARequiredDimensions() const;
        const bool SOFADimensionsAreValid() const;
        const bool checkListenerVariables() const;
        const bool checkSourceVariables() const;
        const bool checkReceiverVariables() const;
        const bool checkEmitterVariables() const;
        const bool checkDimensions() const;
        const bool checkDataVariable() const;
        const bool checkFirDataType() const;
        const bool checkTFDataType() const;
        const bool checkSOSDataType() const;
        
        const bool getCoordinates(sofa::Coordinates::Type &coordinates, const std::string &variableName) const;
        const bool getUnits(sofa::Units::Type &units, const std::string &variableName) const;
        const bool get(sofa::Coordinates::Type &coordinates, sofa::Units::Type &units, const std::string &variableName) const;
        
    private:
        //==============================================================================
        /// avoid shallow and copy constructor
        File( const File &other );                    
        const File & operator= ( const File &other ); 
    };
    
}

#endif /* _SOFA_FILE_H__ */

