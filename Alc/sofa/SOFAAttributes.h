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
 *   @file       SOFAAttributes.h
 *   @brief      General metadata are represented as global attributes in netCDF
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#ifndef _SOFA_ATTRIBUTES_H__
#define _SOFA_ATTRIBUTES_H__

#include "SOFAPlatform.h"
#include <iostream>

namespace sofa
{

    /************************************************************************************/
    /*!
     *  @class          Attribute 
     *  @brief          Static class to represent information about SOFA attributes
     *                  SOFA general metadata are represented as global attributes in netCDF
     *
     */
    /************************************************************************************/
    class SOFA_API Attributes
    {
    public:
        
        /**
         Global attributes according to SOFA Specifications
         */
        enum Type
        {
            kConventions            =    0,        ///< Specifies the netCDF file as a set of AES-X212 conventions.
            kVersion                =    1,        ///< Version of the AES-X212 specifications. The version is in the form x.y, where x is the version major and y the version minor
            kSOFAConventions        =    2,        ///< Name of the AES-X212 convention.
            kSOFAConventionsVersion =    3,        ///< Version of the AES-X212 convention. The version is in the form x.y, where x is the version major and y the version minor.
            kDataType               =    4,        ///< Specifies the data type
            kRoomType               =    5,        ///< Specifies the room type.
            kTitle                  =    6,        ///< A succinct description of what is in the file.
            kDateCreated            =    7,        ///< Date and time of the creation of the file in ISO 8601 format: ‘yyyy-mm-dd HH:MM:SS’. This field is updated each time a new file is created
            kDateModified           =    8,        ///< Date and time of the last file modification in ISO 8601 format: ‘yyyy-mm-dd HH:MM:SS’. This field is updated each time when saving a file
            kAPIName                =    9,        ///< Name of the API that created/edited the file
            kAPIVersion             =    10,       ///< Version of the API that created/edited the file. The version is in the form x.y, where x is the version major and y the version minor.
            kAuthorContact          =    11,       ///< Contact information (for example, email) of the author
            kOrganization           =    12,       ///< Legal name of the organization of the author. Use author’s name for private authors
            kLicense                =    13,       ///< Legal license under which the data are provided
            kApplicationName        =    14,       ///< Name of the application that created/edited the file
            kApplicationVersion     =    15,       ///< Version of the application that created/edited the file
            kComment                =    16,       ///< Miscellaneous information about the data or methods used to produce the date/file
            kHistory                =    17,       ///< Audit trail for modifications to the original data
            kReferences             =    18,       ///< Published or web-based references that describe the data or methods used to produce the date
            kOrigin                 =    19,       ///< The method used for creating the original data. In case of model-generated data, origin should name the model and its version. In case of observed/measured data, source should characterize the data.
            
            kRoomShortName          =    20,       ///< Short Name of the room
            kRoomDescription        =    21,       ///< Informal verbal description of the room
            kRoomLocation           =    22,       ///< Location of the room
            
            kListenerShortName      =    23,       ///< Short name of the listener
            kListenerDescription    =    24,       ///< Description of the listener

            kSourceShortName        =    25,       ///< Short name of the source
            kSourceDescription      =    26,       ///< Description of the source
            
            kReceiverShortName      =    27,       ///< Short name of the receiver
            kReceiverDescription    =    28,       ///< Description of the receiver

            kEmitterShortName       =    29,       ///< Short name of the emitter
            kEmitterDescription     =    30,       ///< Description of the emitter
            
            kNumAttributes          =    31
        };

        static const bool IsRequired(const sofa::Attributes::Type &type_);
        static const bool IsRequired(const std::string &name);
        
        static const bool IsReadOnly(const sofa::Attributes::Type &type_);
        static const bool IsReadOnly(const std::string &name);
        
        static const bool HasDefaultValue(const sofa::Attributes::Type &type_);
        static const bool HasDefaultValue(const std::string &name);
        
        static const std::string GetDefaultValue(const sofa::Attributes::Type &type_);
        static const std::string GetDefaultValue(const std::string &name);
        
        static const std::string GetName(const sofa::Attributes::Type &type_);
        static const sofa::Attributes::Type GetType(const std::string &name);
        
        
    public:
        Attributes();
        ~Attributes();
        
        void ResetToDefault();
        
        void Print(std::ostream & output = std::cout,
                   const bool withPadding = false) const;
        
        const std::string Get(const sofa::Attributes::Type &type_) const;
        void Set(const sofa::Attributes::Type &type_, const std::string &value);
        
    protected:        
        std::string Conventions;
        std::string Version;
        std::string SOFAConventions;
        std::string SOFAConventionsVersion;
        std::string APIName;
        std::string APIVersion;
        std::string ApplicationName;
        std::string ApplicationVersion;
        std::string AuthorContact;
        std::string Organization;
        std::string License;
        std::string Comment;
        std::string History;
        std::string References;
        std::string DataType;
        std::string RoomType;
        std::string Origin;
        std::string DateCreated;
        std::string DateModified;
        std::string Title;
        
        std::string RoomShortName;
        std::string RoomDescription;
        std::string RoomLocation;
        
        std::string ListenerShortName;
        std::string ListenerDescription;
        
        std::string SourceShortName;
        std::string SourceDescription;
        
        std::string ReceiverShortName;
        std::string ReceiverDescription;

        std::string EmitterShortName;
        std::string EmitterDescription;
    };
    
}

#endif /* _SOFA_ATTRIBUTES_H__ */ 

