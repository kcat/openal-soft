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
 *   @file       SOFAHelper.h
 *   @brief      Helper functions
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       20/10/2014
 *
 */
/************************************************************************************/
#ifndef _SOFA_HELPER_H__
#define _SOFA_HELPER_H__

#include "SOFAPlatform.h"

namespace sofa
{
    
    /************************************************************************************/
    /*!
     *  @brief          Returns true if the file is a valid netCDF file
     *  @param[in]      filename : full path to a local file, or an OpenDAP URL
     *                  (e.g. http://bili1.ircam.fr/opendap/hyrax/listen/irc_1002.sofa)
     *
     *  @details        This method wont raise any exception
     *
     */
    /************************************************************************************/
    const bool IsValidNetCDFFile(const std::string &filename) SOFA_NOEXCEPT;
    
    /************************************************************************************/
    /*!
     *  @brief          Returns true if the file is a valid SOFA file
     *  @param[in]      filename : full path to a local file, or an OpenDAP URL
     *                  (e.g. http://bili1.ircam.fr/opendap/hyrax/listen/irc_1002.sofa)
     *
     *  @details        This method wont raise any exception
     *
     */
    /************************************************************************************/
    const bool IsValidSOFAFile(const std::string &filename) SOFA_NOEXCEPT;
    
    /************************************************************************************/
    /*!
     *  @brief          Returns true if the file is a valid SimpleFreeFieldHRIR file
     *  @param[in]      filename : full path to a local file, or an OpenDAP URL
     *                  (e.g. http://bili1.ircam.fr/opendap/hyrax/listen/irc_1002.sofa)
     *
     *  @details        This method wont raise any exception
     *
     */
    /************************************************************************************/
    const bool IsValidSimpleFreeFieldHRIRFile(const std::string &filename) SOFA_NOEXCEPT;
    
    /************************************************************************************/
    /*!
     *  @brief          Returns true if the file is a valid SimpleFreeFieldSOS file
     *  @param[in]      filename : full path to a local file, or an OpenDAP URL
     *                  (e.g. http://bili1.ircam.fr/opendap/hyrax/listen/irc_1002.sofa)
     *
     *  @details        This method wont raise any exception
     *
     */
    /************************************************************************************/
    const bool IsValidSimpleFreeFieldSOSFile(const std::string &filename) SOFA_NOEXCEPT;
    
    /************************************************************************************/
    /*!
     *  @brief          Returns true if the file is a valid SimpleHeadphoneIR file
     *  @param[in]      filename : full path to a local file, or an OpenDAP URL
     *                  (e.g. http://bili1.ircam.fr/opendap/hyrax/listen/irc_1002.sofa)
     *
     *  @details        This method wont raise any exception
     *
     */
    /************************************************************************************/
    const bool IsValidSimpleHeadphoneIRFile(const std::string &filename) SOFA_NOEXCEPT;
    
    /************************************************************************************/
    /*!
     *  @brief          Returns true if the file is a valid GeneralFIR file
     *  @param[in]      filename : full path to a local file, or an OpenDAP URL
     *                  (e.g. http://bili1.ircam.fr/opendap/hyrax/listen/irc_1002.sofa)
     *
     *  @details        This method wont raise any exception
     *
     */
    /************************************************************************************/
    const bool IsValidGeneralFIRFile(const std::string &filename) SOFA_NOEXCEPT;
    
    /************************************************************************************/
    /*!
     *  @brief          Returns true if the file is a valid GeneralTF file
     *  @param[in]      filename : full path to a local file, or an OpenDAP URL
     *                  (e.g. http://bili1.ircam.fr/opendap/hyrax/listen/irc_1002.sofa)
     *
     *  @details        This method wont raise any exception
     *
     */
    /************************************************************************************/
    const bool IsValidGeneralTFFile(const std::string &filename) SOFA_NOEXCEPT;
    
}

#endif /* _SOFA_HELPER_H__ */



