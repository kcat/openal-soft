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
 *   @file       SOFAAttributes.cpp
 *   @brief      General metadata are represented as global attributes in netCDF
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAAttributes.h"
#include "SOFAAPI.h"
#include "SOFAString.h"
#include "SOFASimpleFreeFieldHRIR.h"
#include <map>

using namespace sofa;

namespace AttributesHelper
{
    static std::map< std::string, sofa::Attributes::Type > typeMap;
    
    /************************************************************************************/
    /*!
     *  @brief          Creates a mapping between Attributes type and their names
     *
     */
    /************************************************************************************/
    static void initTypeMap()
    {
        if( typeMap.empty() == true )
        {
            typeMap["Conventions"]                  = sofa::Attributes::kConventions;
            typeMap["Version"]                      = sofa::Attributes::kVersion;
            typeMap["SOFAConventions"]              = sofa::Attributes::kSOFAConventions;
            typeMap["SOFAConventionsVersion"]       = sofa::Attributes::kSOFAConventionsVersion;
            typeMap["APIName"]                      = sofa::Attributes::kAPIName;
            typeMap["APIVersion"]                   = sofa::Attributes::kAPIVersion;
            typeMap["ApplicationName"]              = sofa::Attributes::kApplicationName;
            typeMap["ApplicationVersion"]           = sofa::Attributes::kApplicationVersion;
            typeMap["AuthorContact"]                = sofa::Attributes::kAuthorContact;
            typeMap["Organization"]                 = sofa::Attributes::kOrganization;
            typeMap["License"]                      = sofa::Attributes::kLicense;
            typeMap["Comment"]                      = sofa::Attributes::kComment;
            typeMap["History"]                      = sofa::Attributes::kHistory;
            typeMap["References"]                   = sofa::Attributes::kReferences;
            typeMap["DataType"]                     = sofa::Attributes::kDataType;
            typeMap["RoomType"]                     = sofa::Attributes::kRoomType;
            typeMap["Origin"]                       = sofa::Attributes::kOrigin;
            typeMap["DateCreated"]                  = sofa::Attributes::kDateCreated;
            typeMap["DateModified"]                 = sofa::Attributes::kDateModified;
            typeMap["Title"]                        = sofa::Attributes::kTitle;
            typeMap["RoomShortName"]                = sofa::Attributes::kRoomShortName;
            typeMap["RoomDescription"]              = sofa::Attributes::kRoomDescription;
            typeMap["RoomLocation"]                 = sofa::Attributes::kRoomLocation;
            typeMap["ListenerShortName"]            = sofa::Attributes::kListenerShortName;
            typeMap["ListenerDescription"]          = sofa::Attributes::kListenerDescription;
            typeMap["SourceShortName"]              = sofa::Attributes::kSourceShortName;
            typeMap["SourceDescription"]            = sofa::Attributes::kSourceDescription;
            typeMap["ReceiverShortName"]            = sofa::Attributes::kReceiverShortName;
            typeMap["ReceiverDescription"]          = sofa::Attributes::kReceiverDescription;
            typeMap["EmitterShortName"]             = sofa::Attributes::kEmitterShortName;
            typeMap["EmitterDescription"]           = sofa::Attributes::kEmitterDescription;

        }
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns true if a given Attributes is required, according to the specifications
 *  @param[in]      type_ : a given sofa global attribute
 *
 */
/************************************************************************************/
const bool sofa::Attributes::IsRequired(const sofa::Attributes::Type &type_)
{
    switch( type_ )
    {            
        case sofa::Attributes::kConventions             : return true;
        case sofa::Attributes::kVersion                 : return true;
        case sofa::Attributes::kSOFAConventions         : return true;
        case sofa::Attributes::kSOFAConventionsVersion  : return true;
        case sofa::Attributes::kAPIName                 : return true;
        case sofa::Attributes::kAPIVersion              : return true;
        case sofa::Attributes::kApplicationName         : return false;
        case sofa::Attributes::kApplicationVersion      : return false;
        case sofa::Attributes::kAuthorContact           : return true;
        case sofa::Attributes::kOrganization            : return true;
        case sofa::Attributes::kLicense                 : return true;
        case sofa::Attributes::kComment                 : return false;
        case sofa::Attributes::kHistory                 : return false;
        case sofa::Attributes::kReferences              : return false;
        case sofa::Attributes::kDataType                : return true;
        case sofa::Attributes::kRoomType                : return true;
        case sofa::Attributes::kOrigin                  : return false;
        case sofa::Attributes::kDateCreated             : return true;
        case sofa::Attributes::kDateModified            : return true;
        case sofa::Attributes::kTitle                   : return true;
        case sofa::Attributes::kRoomShortName           : return false;
        case sofa::Attributes::kRoomDescription         : return false;
        case sofa::Attributes::kRoomLocation            : return false;
        case sofa::Attributes::kListenerShortName       : return false;
        case sofa::Attributes::kListenerDescription     : return false;
        case sofa::Attributes::kSourceShortName         : return false;
        case sofa::Attributes::kSourceDescription       : return false;
        case sofa::Attributes::kReceiverShortName       : return false;
        case sofa::Attributes::kReceiverDescription     : return false;
        case sofa::Attributes::kEmitterShortName        : return false;
        case sofa::Attributes::kEmitterDescription      : return false;
            
        default                                         : SOFA_ASSERT( false ); return false;
        case sofa::Attributes::kNumAttributes           : SOFA_ASSERT( false ); return false;
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns true if a given Attributes is read-only, according to the specifications
 *  @param[in]      type_ : a given sofa global attribute
 *
 */
/************************************************************************************/
const bool sofa::Attributes::IsReadOnly(const sofa::Attributes::Type &type_)
{
    switch( type_ )
    {            
        case sofa::Attributes::kConventions             : return true;
        case sofa::Attributes::kVersion                 : return true;
        case sofa::Attributes::kSOFAConventions         : return true;
        case sofa::Attributes::kSOFAConventionsVersion  : return true;
        case sofa::Attributes::kAPIName                 : return true;
        case sofa::Attributes::kAPIVersion              : return true;
        case sofa::Attributes::kApplicationName         : return false;
        case sofa::Attributes::kApplicationVersion      : return false;
        case sofa::Attributes::kAuthorContact           : return false;
        case sofa::Attributes::kOrganization            : return false;
        case sofa::Attributes::kLicense                 : return false;
        case sofa::Attributes::kComment                 : return false;
        case sofa::Attributes::kHistory                 : return false;
        case sofa::Attributes::kReferences              : return false;
        case sofa::Attributes::kDataType                : return false;
        case sofa::Attributes::kRoomType                : return false;
        case sofa::Attributes::kOrigin                  : return false;
        case sofa::Attributes::kDateCreated             : return false;
        case sofa::Attributes::kDateModified            : return false;
        case sofa::Attributes::kTitle                   : return false;
        case sofa::Attributes::kRoomShortName           : return false;
        case sofa::Attributes::kRoomDescription         : return false;
        case sofa::Attributes::kRoomLocation            : return false;
        case sofa::Attributes::kListenerShortName       : return false;
        case sofa::Attributes::kListenerDescription     : return false;
        case sofa::Attributes::kSourceShortName         : return false;
        case sofa::Attributes::kSourceDescription       : return false;
        case sofa::Attributes::kReceiverShortName       : return false;
        case sofa::Attributes::kReceiverDescription     : return false;
        case sofa::Attributes::kEmitterShortName        : return false;
        case sofa::Attributes::kEmitterDescription      : return false;
            
        default                                         : SOFA_ASSERT( false ); return false;
        case sofa::Attributes::kNumAttributes           : SOFA_ASSERT( false ); return false;
    }
}

/************************************************************************************/
/*!
 *  @brief          Retrieves the name of a global attribute, given its type
 *  @param[in]      type_ : a given sofa global attribute
 *
 */
/************************************************************************************/
const std::string sofa::Attributes::GetName(const sofa::Attributes::Type &type_)
{
    switch( type_ )
    {            
        case sofa::Attributes::kConventions             : return "Conventions";
        case sofa::Attributes::kVersion                 : return "Version";
        case sofa::Attributes::kSOFAConventions         : return "SOFAConventions";
        case sofa::Attributes::kSOFAConventionsVersion  : return "SOFAConventionsVersion";
        case sofa::Attributes::kAPIName                 : return "APIName";
        case sofa::Attributes::kAPIVersion              : return "APIVersion";
        case sofa::Attributes::kApplicationName         : return "ApplicationName";
        case sofa::Attributes::kApplicationVersion      : return "ApplicationVersion";
        case sofa::Attributes::kAuthorContact           : return "AuthorContact";
        case sofa::Attributes::kOrganization            : return "Organization";
        case sofa::Attributes::kLicense                 : return "License";
        case sofa::Attributes::kComment                 : return "Comment";
        case sofa::Attributes::kHistory                 : return "History";
        case sofa::Attributes::kReferences              : return "References";
        case sofa::Attributes::kDataType                : return "DataType";
        case sofa::Attributes::kRoomType                : return "RoomType";
        case sofa::Attributes::kOrigin                  : return "Origin";
        case sofa::Attributes::kDateCreated             : return "DateCreated";
        case sofa::Attributes::kDateModified            : return "DateModified";
        case sofa::Attributes::kTitle                   : return "Title";
        case sofa::Attributes::kRoomShortName           : return "RoomShortName";
        case sofa::Attributes::kRoomDescription         : return "RoomDescription";
        case sofa::Attributes::kRoomLocation            : return "RoomLocation";
        case sofa::Attributes::kListenerShortName       : return "ListenerShortName";
        case sofa::Attributes::kListenerDescription     : return "ListenerDescription";
        case sofa::Attributes::kSourceShortName         : return "SourceShortName";
        case sofa::Attributes::kSourceDescription       : return "SourceDescription";
        case sofa::Attributes::kReceiverShortName       : return "ReceiverShortName";
        case sofa::Attributes::kReceiverDescription     : return "ReceiverDescription";
        case sofa::Attributes::kEmitterShortName        : return "EmitterShortName";
        case sofa::Attributes::kEmitterDescription      : return "EmitterDescription";
            
        default                                         : SOFA_ASSERT( false ); return "";
        case sofa::Attributes::kNumAttributes           : SOFA_ASSERT( false ); return "";
    }
}

/************************************************************************************/
/*!
 *  @brief          Retrieves the type of a global attribute, given its name
 *  @param[in]      name : name of the attribute to query
 *
 */
/************************************************************************************/
const sofa::Attributes::Type sofa::Attributes::GetType(const std::string &name)
{
    AttributesHelper::initTypeMap();
    
    if( AttributesHelper::typeMap.count( name ) == 0 )
    {        
        SOFA_ASSERT( false );
        
        return sofa::Attributes::kNumAttributes;
    }
    else
    {
        return AttributesHelper::typeMap[ name ];
    }
    
}

/************************************************************************************/
/*!
 *  @brief          Returns true if a given attribute has a default value, according to the specifications
 *  @param[in]      type_ : a given sofa global attribute
 *
 */
/************************************************************************************/
const bool sofa::Attributes::HasDefaultValue(const sofa::Attributes::Type &type_)
{
    switch( type_ )
    {            
        case sofa::Attributes::kConventions             : return true;
        case sofa::Attributes::kVersion                 : return true;
        case sofa::Attributes::kSOFAConventions         : return true;
        case sofa::Attributes::kSOFAConventionsVersion  : return true;
        case sofa::Attributes::kAPIName                 : return true;
        case sofa::Attributes::kAPIVersion              : return true;
        case sofa::Attributes::kApplicationName         : return false;
        case sofa::Attributes::kApplicationVersion      : return false;
        case sofa::Attributes::kAuthorContact           : return false;
        case sofa::Attributes::kOrganization            : return false;
        case sofa::Attributes::kLicense                 : return true;
        case sofa::Attributes::kComment                 : return false;
        case sofa::Attributes::kHistory                 : return false;
        case sofa::Attributes::kReferences              : return false;
        case sofa::Attributes::kDataType                : return true;
        case sofa::Attributes::kRoomType                : return true;
        case sofa::Attributes::kOrigin                  : return false;
        case sofa::Attributes::kDateCreated             : return false;
        case sofa::Attributes::kDateModified            : return false;
        case sofa::Attributes::kTitle                   : return false;
        case sofa::Attributes::kRoomShortName           : return false;
        case sofa::Attributes::kRoomDescription         : return false;
        case sofa::Attributes::kRoomLocation            : return false;
        case sofa::Attributes::kListenerShortName       : return false;
        case sofa::Attributes::kListenerDescription     : return false;
        case sofa::Attributes::kSourceShortName         : return false;
        case sofa::Attributes::kSourceDescription       : return false;
        case sofa::Attributes::kReceiverShortName       : return false;
        case sofa::Attributes::kReceiverDescription     : return false;
        case sofa::Attributes::kEmitterShortName        : return false;
        case sofa::Attributes::kEmitterDescription      : return false;
            
        default                                         : SOFA_ASSERT( false ); return false;
        case sofa::Attributes::kNumAttributes           : SOFA_ASSERT( false ); return false;
    }
}

const bool sofa::Attributes::IsRequired(const std::string &name)
{
    const sofa::Attributes::Type type_ = sofa::Attributes::GetType( name );
    return sofa::Attributes::IsRequired( type_ );
}

const bool sofa::Attributes::IsReadOnly(const std::string &name)
{
    const sofa::Attributes::Type type_ = sofa::Attributes::GetType( name );
    return sofa::Attributes::IsReadOnly( type_ );
}

/************************************************************************************/
/*!
 *  @brief          Returns true if a given attribute has a default value, according to the specifications
 *  @param[in]      name : name of the attribute to query
 *
 */
/************************************************************************************/
const bool sofa::Attributes::HasDefaultValue(const std::string &name)
{
    const sofa::Attributes::Type type_ = sofa::Attributes::GetType( name );
    return sofa::Attributes::HasDefaultValue( type_ );
}

const std::string sofa::Attributes::GetDefaultValue(const sofa::Attributes::Type &type_)
{
    if( HasDefaultValue( type_ ) == true )
    {
        switch( type_ )
        {            
            case sofa::Attributes::kConventions             : return "SOFA";
            case sofa::Attributes::kVersion                 : return sofa::ApiInfos::GetSpecificationsVersion();
            case sofa::Attributes::kDataType                : return "FIR";
            case sofa::Attributes::kSOFAConventions         : return "SimpleFreeFieldHRIR";
            case sofa::Attributes::kSOFAConventionsVersion  : return SimpleFreeFieldHRIR::GetConventionVersion();
            case sofa::Attributes::kAPIName                 : return sofa::ApiInfos::GetAPIName();
            case sofa::Attributes::kAPIVersion              : return sofa::ApiInfos::GetAPIVersion();
            case sofa::Attributes::kLicense                 : return "No license provided, ask the author for permission.";
            case sofa::Attributes::kRoomType                : return "free field";
                
            default                                         : SOFA_ASSERT( false ); return std::string("");
        }
    }
    else 
    {
        return std::string("");
    }
}

/************************************************************************************/
/*!
 *  @brief          Returns the default value of an attribute (as a string), or an empty string
 *                  if there is no default value
 *  @param[in]      name : name of the attribute to query
 *
 */
/************************************************************************************/
const std::string sofa::Attributes::GetDefaultValue(const std::string &name)
{
    const sofa::Attributes::Type type_ = sofa::Attributes::GetType( name );
    return sofa::Attributes::GetDefaultValue( type_ );
}



/************************************************************************************/
/*!
 *  @brief          Class constructor
 *  @details        Initialize the global attributes of a sofa file, with the default values
 *                  according to the specifications
 *
 */
/************************************************************************************/
sofa::Attributes::Attributes()
: Conventions()
, Version()
, SOFAConventions()
, SOFAConventionsVersion()
, APIName()
, APIVersion()
, ApplicationName()
, ApplicationVersion()
, AuthorContact()
, Organization()
, License()
, Comment()
, History()
, References()
, DataType()
, RoomType()
, Origin()
, DateCreated()
, DateModified()
, Title()
, RoomShortName()
, RoomDescription()
, RoomLocation()
, ListenerShortName()
, ListenerDescription()
, SourceShortName()
, SourceDescription()
, ReceiverShortName()
, ReceiverDescription()
, EmitterShortName()
, EmitterDescription()
{
    ResetToDefault();
}

/************************************************************************************/
/*!
 *  @brief          Class destructor
 *
 */
/************************************************************************************/
sofa::Attributes::~Attributes()
{
}

/************************************************************************************/
/*!
 *  @brief          Reset to default values
 *
 */
/************************************************************************************/
void sofa::Attributes::ResetToDefault()
{
    for( unsigned int i = 0; i < sofa::Attributes::kNumAttributes; i++ )
    {
        const sofa::Attributes::Type type_ = static_cast< const sofa::Attributes::Type >( i );
        
        if( sofa::Attributes::HasDefaultValue( type_ ) == true )
        {
            const std::string def = sofa::Attributes::GetDefaultValue( type_ );
            
            Set( type_, def );
        }
        else
        {
            Set( type_, "" );
        }
    }
}

/************************************************************************************/
/*!
 *  @brief          Prints all attributes
 *
 */
/************************************************************************************/
void sofa::Attributes::Print(std::ostream & output, const bool withPadding) const
{
    for( unsigned int i = 0; i < sofa::Attributes::kNumAttributes; i++ )
    {
        const sofa::Attributes::Type type_ = static_cast< const sofa::Attributes::Type >( i );
        
        std::string name  = sofa::Attributes::GetName( type_ );
        
        std::string value = Get( type_ );
        
        if( withPadding == true )
        {
            name  = sofa::String::PadWith( name );
            value = sofa::String::PadWith( value );
        }
        
        output << name << " = " << value << std::endl;
    }
}

/************************************************************************************/
/*!
 *  @brief          Sets the value of a given attribute
 *
 */
/************************************************************************************/
void sofa::Attributes::Set(const sofa::Attributes::Type &type_, const std::string &value)
{
    switch( type_ )
    {
        case sofa::Attributes::kConventions             : Conventions = value; break;
        case sofa::Attributes::kVersion                 : Version = value; break;
        case sofa::Attributes::kSOFAConventions         : SOFAConventions = value; break;
        case sofa::Attributes::kSOFAConventionsVersion  : SOFAConventionsVersion = value; break;
        case sofa::Attributes::kAPIName                 : APIName = value; break;
        case sofa::Attributes::kAPIVersion              : APIVersion = value; break;
        case sofa::Attributes::kApplicationName         : ApplicationName = value; break;
        case sofa::Attributes::kApplicationVersion      : ApplicationVersion = value; break;
        case sofa::Attributes::kAuthorContact           : AuthorContact = value; break;
        case sofa::Attributes::kOrganization            : Organization = value; break;
        case sofa::Attributes::kLicense                 : License = value; break;
        case sofa::Attributes::kComment                 : Comment = value; break;
        case sofa::Attributes::kHistory                 : History = value; break;
        case sofa::Attributes::kReferences              : References = value; break;
        case sofa::Attributes::kDataType                : DataType = value; break;
        case sofa::Attributes::kRoomType                : RoomType = value; break;
        case sofa::Attributes::kOrigin                  : Origin = value; break;
        case sofa::Attributes::kDateCreated             : DateCreated = value; break;
        case sofa::Attributes::kDateModified            : DateModified = value; break;
        case sofa::Attributes::kTitle                   : Title = value; break;
        case sofa::Attributes::kRoomShortName           : RoomShortName = value; break;
        case sofa::Attributes::kRoomDescription         : RoomDescription = value; break;
        case sofa::Attributes::kRoomLocation            : RoomLocation = value; break;
        case sofa::Attributes::kListenerShortName       : ListenerShortName = value; break;
        case sofa::Attributes::kListenerDescription     : ListenerDescription = value; break;
        case sofa::Attributes::kSourceShortName         : SourceShortName = value; break;
        case sofa::Attributes::kSourceDescription       : SourceDescription = value; break;
        case sofa::Attributes::kReceiverShortName       : ReceiverShortName = value; break;
        case sofa::Attributes::kReceiverDescription     : ReceiverDescription = value; break;
        case sofa::Attributes::kEmitterShortName        : EmitterShortName = value; break;
        case sofa::Attributes::kEmitterDescription      : EmitterDescription = value; break;
            
        default                                         : SOFA_ASSERT( false ); break;
        case sofa::Attributes::kNumAttributes           : SOFA_ASSERT( false ); break;
    }
}

/************************************************************************************/
/*!
 *  @brief          Retrieves the value of a given attribute, as a string
 *
 */
/************************************************************************************/
const std::string sofa::Attributes::Get(const sofa::Attributes::Type &type_) const
{
    switch( type_ )
    {            
        case sofa::Attributes::kConventions             : return Conventions;
        case sofa::Attributes::kVersion                 : return Version;
        case sofa::Attributes::kSOFAConventions         : return SOFAConventions;
        case sofa::Attributes::kSOFAConventionsVersion  : return SOFAConventionsVersion;
        case sofa::Attributes::kAPIName                 : return APIName;
        case sofa::Attributes::kAPIVersion              : return APIVersion;
        case sofa::Attributes::kApplicationName         : return ApplicationName;
        case sofa::Attributes::kApplicationVersion      : return ApplicationVersion;
        case sofa::Attributes::kAuthorContact           : return AuthorContact;
        case sofa::Attributes::kOrganization            : return Organization;
        case sofa::Attributes::kLicense                 : return License;
        case sofa::Attributes::kComment                 : return Comment;
        case sofa::Attributes::kHistory                 : return History;
        case sofa::Attributes::kReferences              : return References;
        case sofa::Attributes::kDataType                : return DataType;
        case sofa::Attributes::kRoomType                : return RoomType;
        case sofa::Attributes::kOrigin                  : return Origin;
        case sofa::Attributes::kDateCreated             : return DateCreated;
        case sofa::Attributes::kDateModified            : return DateModified;
        case sofa::Attributes::kTitle                   : return Title;
        case sofa::Attributes::kRoomShortName           : return RoomShortName;
        case sofa::Attributes::kRoomDescription         : return RoomDescription;
        case sofa::Attributes::kRoomLocation            : return RoomLocation;
        case sofa::Attributes::kListenerShortName       : return ListenerShortName;
        case sofa::Attributes::kListenerDescription     : return ListenerDescription;
        case sofa::Attributes::kSourceShortName         : return SourceShortName;
        case sofa::Attributes::kSourceDescription       : return SourceDescription;
        case sofa::Attributes::kReceiverShortName       : return ReceiverShortName;
        case sofa::Attributes::kReceiverDescription     : return ReceiverDescription;
        case sofa::Attributes::kEmitterShortName        : return EmitterShortName;
        case sofa::Attributes::kEmitterDescription      : return EmitterDescription;
            
        default                                         : SOFA_ASSERT( false ); return "";
        case sofa::Attributes::kNumAttributes           : SOFA_ASSERT( false ); return "";
    }
}


