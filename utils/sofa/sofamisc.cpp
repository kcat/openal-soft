/************************************************************************************/
/*  FILE DESCRIPTION                                                                */
/*----------------------------------------------------------------------------------*/
/*!
 *   @file       sofainfo.cpp
 *   @brief      Prints some informations about the current API and the related SOFA convention/specifications
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFA.h"

static const std::string verticalSeparator       = " ";
const std::string horizontalSeparator            = "_";

/************************************************************************************/
/*!
 *  @brief          Main entry point
 *
 */
/************************************************************************************/
int main(int argc, char *argv[])
{
    std::ostream & output = std::cout;
    
    sofa::String::PrintSeparationLine( output );
    output << sofa::ApiInfos::GetAPICopyright();
    sofa::String::PrintSeparationLine( output );
        
    output << std::endl;
    output << std::endl;
    output << "SOFA Global attributes : " << std::endl;
    
    sofa::String::PrintSeparationLine( output );
    
    output << sofa::String::PadWith( "name " );
    output << verticalSeparator;
    output << sofa::String::PadWith( "required" );
    output << verticalSeparator ;
    output << sofa::String::PadWith( "read only" );
    output << verticalSeparator ;
    output << sofa::String::PadWith( "default" ) ;
    output << std::endl;
    
    sofa::String::PrintSeparationLine( output );
    
    for( unsigned int i = 0; i < sofa::Attributes::kNumAttributes; i++ )
    {
        const sofa::Attributes::Type type_ = static_cast< const sofa::Attributes::Type >( i );
        const std::string name = sofa::Attributes::GetName( type_ );
        const bool required   = sofa::Attributes::IsRequired( type_ );
        const bool readonly    = sofa::Attributes::IsReadOnly( type_ );
        //const bool hasDefault  = sofa::Attributes::HasDefaultValue( type_ );
        const std::string def  = sofa::Attributes::GetDefaultValue( type_ );
        
        output << sofa::String::PadWith( name );
        output << verticalSeparator;
        output << sofa::String::PadWith( sofa::String::bool2yesorno( required ) );
        output << verticalSeparator;
        output << sofa::String::PadWith( sofa::String::bool2yesorno( readonly ) );
        output << verticalSeparator;
        output << sofa::String::PadWith( def );
        output << std::endl;
    }
    
    sofa::String::PrintSeparationLine( output );
    
    return 0;
}

