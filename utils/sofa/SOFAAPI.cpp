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
 *   @file       SOFAAPI.cpp
 *   @brief      Informations about this API
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFAAPI.h"
#include "SOFASimpleFreeFieldHRIR.h"
#include "SOFASimpleFreeFieldSOS.h"
#include "SOFASimpleHeadphoneIR.h"
#include <sstream>

using namespace sofa;

#ifndef SOFA_VERSION_MAJOR
    #error "Macro SOFA_VERSION_MAJOR not defined"
#endif

#ifndef SOFA_VERSION_MINOR
    #error "Macro SOFA_VERSION_MINOR not defined"
#endif

#ifndef SOFA_VERSION_RELEASE
    #error "Macro SOFA_VERSION_RELEASE not defined"
#endif

#ifndef SOFA_SPECIFICATIONS_MAJOR
    #error "Macro SOFA_SPECIFICATIONS_MAJOR not defined"
#endif

#ifndef SOFA_SPECIFICATIONS_MINOR
    #error "Macro SOFA_SPECIFICATIONS_MINOR not defined"
#endif

/************************************************************************************/
/*!
 *  @brief          Returns the name of this API
 *
 */
/************************************************************************************/
const std::string sofa::ApiInfos::GetAPIName()
{
    return "SOFA C++ API";
}

/************************************************************************************/
/*!
 *  @brief          Returns a std::string like : x.y.z 
 *                    where x is the API version major
 *                    y is the API version minor
 *                    z is the API version release
 *
 */
/************************************************************************************/
const std::string sofa::ApiInfos::GetAPIVersion()
{    
    const int major     = (int) GetAPIVersionMajor();
    const int minor     = (int) GetAPIVersionMinor();
    const int release   = (int) GetAPIVersionRelease();
    
    SOFA_ASSERT( major >= 0 );
    SOFA_ASSERT( minor >= 0 );
    SOFA_ASSERT( release >= 0 );
    
    std::ostringstream version;
    version << major;
    version << ".";
    version << minor;
    version << ".";
    version << release;
    
    return version.str();
}

/************************************************************************************/
/*!
 *  @brief          Returns the API version minor
 *
 */
/************************************************************************************/
const unsigned int sofa::ApiInfos::GetAPIVersionMinor()
{
    const int minor     = (int) SOFA_VERSION_MINOR;
    
    SOFA_ASSERT( minor >= 0 );
    
    return (unsigned int) minor;
}

/************************************************************************************/
/*!
 *  @brief          Returns the API version major
 *
 */
/************************************************************************************/
const unsigned int sofa::ApiInfos::GetAPIVersionMajor()
{
    const int major     = (int) SOFA_VERSION_MAJOR;
    
    SOFA_ASSERT( major >= 0 );
    
    return (unsigned int) major;
}

/************************************************************************************/
/*!
 *  @brief          Returns the API version release
 *
 */
/************************************************************************************/
const unsigned int sofa::ApiInfos::GetAPIVersionRelease()
{
    const int release   = (int) SOFA_VERSION_RELEASE;
    
    SOFA_ASSERT( release >= 0 );
    
    return (unsigned int) release;
}

/************************************************************************************/
/*!
 *  @brief          Returns a std::string like : x.y
 *                    where x is the  SOFA specifications version major
 *                    y is the  SOFA specifications version minor
 *
 */
/************************************************************************************/
const std::string sofa::ApiInfos::GetSpecificationsVersion()
{
    const int major     = (int) GetSpecificationsVersionMajor();
    const int minor     = (int) GetSpecificationsVersionMinor();
    
    SOFA_ASSERT( major >= 0 );
    SOFA_ASSERT( minor >= 0 );
    
    std::ostringstream version;
    version << major;
    version << ".";
    version << minor;
    
    return version.str();
}

/************************************************************************************/
/*!
 *  @brief          Returns the version major of the SOFA specifications
 *
 */
/************************************************************************************/
const unsigned int sofa::ApiInfos::GetSpecificationsVersionMinor()
{
    const int major     = (int) SOFA_SPECIFICATIONS_MINOR;
    
    SOFA_ASSERT( major >= 0 );
    
    return (unsigned int) major;
}

/************************************************************************************/
/*!
 *  @brief          Returns the version minor of the SOFA specifications
 *
 */
/************************************************************************************/
const unsigned int sofa::ApiInfos::GetSpecificationsVersionMajor()
{
    const int major     = (int) SOFA_SPECIFICATIONS_MAJOR;
    
    SOFA_ASSERT( major >= 0 );
    
    return (unsigned int) major;
}

const std::string sofa::ApiInfos::GetSimpleFreeFieldHRIRConventionVersion()
{
    return sofa::SimpleFreeFieldHRIR::GetConventionVersion();
}

const unsigned int sofa::ApiInfos::GetSimpleFreeFieldHRIRConventionVersionMajor()
{
    return sofa::SimpleFreeFieldHRIR::ConventionVersionMajor;
}

const unsigned int sofa::ApiInfos::GetSimpleFreeFieldHRIRConventionVersionMinor()
{
    return sofa::SimpleFreeFieldHRIR::ConventionVersionMinor;
}


const std::string sofa::ApiInfos::GetSimpleFreeFieldSOSConventionVersion()
{
    return sofa::SimpleFreeFieldSOS::GetConventionVersion();
}

const unsigned int sofa::ApiInfos::GetSimpleFreeFieldSOSConventionVersionMajor()
{
    return sofa::SimpleFreeFieldSOS::ConventionVersionMajor;
}

const unsigned int sofa::ApiInfos::GetSimpleFreeFieldSOSConventionVersionMinor()
{
    return sofa::SimpleFreeFieldSOS::ConventionVersionMinor;
}


const std::string sofa::ApiInfos::GetSimpleHeadphoneIRConventionVersion()
{
    return sofa::SimpleHeadphoneIR::GetConventionVersion();
}

const unsigned int sofa::ApiInfos::GetSimpleHeadphoneIRConventionVersionMajor()
{
    return sofa::SimpleHeadphoneIR::ConventionVersionMajor;
}

const unsigned int sofa::ApiInfos::GetSimpleHeadphoneIRConventionVersionMinor()
{
    return sofa::SimpleHeadphoneIR::ConventionVersionMinor;
}

/************************************************************************************/
/*!
 *  @brief          Returns the copyright text of this API
 *
 */
/************************************************************************************/
const std::string sofa::ApiInfos::GetAPICopyright()
{
    const std::string copyright =
    sofa::ApiInfos::GetAPIName() + " version " + sofa::ApiInfos::GetAPIVersion()
    + " (implementing SOFA specifications version " + sofa::ApiInfos::GetSpecificationsVersion() + ")\n"
    + "\n" +
    "Copyright (c) 2013-2014, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC\n"
    "All rights reserved.\n"
    "\n"
    "Redistribution and use in source and binary forms, with or without\n"
    "modification, are permitted provided that the following conditions are met:\n"
    "* Redistributions of source code must retain the above copyright\n"
    "notice, this list of conditions and the following disclaimer.\n"
    "* Redistributions in binary form must reproduce the above copyright\n"
    "notice, this list of conditions and the following disclaimer in the\n"
    "documentation and/or other materials provided with the distribution.\n"
    "* Neither the name of the <organization> nor the\n"
    "names of its contributors may be used to endorse or promote products\n"
    "derived from this software without specific prior written permission.\n"
    "\n"
    "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND\n"
    "ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\n"
    "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE\n"
    "DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY\n"
    "DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES\n"
    "(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;\n"
    " LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND\n"
    "ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
    "(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\n"
    "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n";
    
    return copyright;
}

