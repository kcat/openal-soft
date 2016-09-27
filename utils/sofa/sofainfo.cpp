/************************************************************************************/
/*  FILE DESCRIPTION                                                                */
/*----------------------------------------------------------------------------------*/
/*!
 *   @file       sofainfo.cpp
 *   @brief      Prints informations for SimpleFreeFieldHRIR file
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       10/05/2013
 * 
 */
/************************************************************************************/
#include "SOFA.h"

static void DisplayHelp(std::ostream & output = std::cout)
{
    output << "sofainfo prints info about SOFA files" << std::endl;
    output << "    syntax : ./sofainfo [filename]" << std::endl;
}

/************************************************************************************/
/*!
 *  @brief          Helper function to  access element [i][j][k] of a "3D array" of dimensions [dim1][dim2][dim3]
 *                  stored in a 1D data array
 *
 */
/************************************************************************************/
static inline const std::size_t array3DIndex(const unsigned long i,
                                             const unsigned long j,
                                             const unsigned long k,
                                             const unsigned long dim1,
                                             const unsigned long dim2,
                                             const unsigned long dim3)
{
    return dim2 * dim3 * i + dim3 * j + k;
}

/************************************************************************************/
/*!
 *  @brief          Helper function to  access element [i][j] of a "2D array" of dimensions [dim1][dim2]
 *                  stored in a 1D data array
 *
 */
/************************************************************************************/
static inline const std::size_t array2DIndex(const unsigned long i,
                                             const unsigned long j,
                                             const unsigned long dim1,
                                             const unsigned long dim2)
{
    return dim2 * i + j;
}

/************************************************************************************/
/*!
 *  @brief          Prints Emitter informations
 *
 */
/************************************************************************************/
static void PrintEmitter(const sofa::File &theFile,
                          std::ostream & output)
{
    sofa::Coordinates::Type coordinates;
    sofa::Units::Type units;
    const bool ok = theFile.GetEmitterPosition( coordinates, units );
    
    output << sofa::String::PadWith( "EmitterPosition:Type" ) << " = " << sofa::Coordinates::GetName( coordinates ) << std::endl;
    output << sofa::String::PadWith( "EmitterPosition:Units" ) << " = " << sofa::Units::GetName( units ) << std::endl;
    
    std::vector< std::size_t > dims;
    theFile.GetVariableDimensions( dims, "EmitterPosition" );
    
    SOFA_ASSERT( dims.size() == 3 );
    
    std::vector< double > pos;
    pos.resize( dims[0] * dims[1] * dims[2] );
    
    theFile.GetEmitterPosition( &pos[0], dims[0], dims[1], dims[2] );
    
    output << sofa::String::PadWith( "EmitterPosition" ) << " = " ;
    
    for( std::size_t i = 0; i < dims[0]; i++ )
    {
        for( std::size_t j = 0; j < dims[1]; j++ )
        {
            for( std::size_t k = 0; k < dims[2]; k++ )
            {
                const std::size_t index = array3DIndex( i, j, k, dims[0], dims[1], dims[2] );
                output << pos[ index ] << " ";
            }
        }
    }
    output << std::endl;
}

/************************************************************************************/
/*!
 *  @brief          Prints Receiver informations
 *
 */
/************************************************************************************/
static void PrintReceiver(const sofa::File &theFile,
                          std::ostream & output)
{
    sofa::Coordinates::Type coordinates;
    sofa::Units::Type units;
    const bool ok = theFile.GetReceiverPosition( coordinates, units );
    
    output << sofa::String::PadWith( "ReceiverPosition:Type" ) << " = " << sofa::Coordinates::GetName( coordinates ) << std::endl;
    output << sofa::String::PadWith( "ReceiverPosition:Units" ) << " = " << sofa::Units::GetName( units ) << std::endl;
    
    std::vector< std::size_t > dims;
    theFile.GetVariableDimensions( dims, "ReceiverPosition" );
    
    SOFA_ASSERT( dims.size() == 3 );
    
    std::vector< double > pos;
    pos.resize( dims[0] * dims[1] * dims[2] );
    
    theFile.GetReceiverPosition( &pos[0], dims[0], dims[1], dims[2] );
    
    output << sofa::String::PadWith( "ReceiverPosition" ) << " = " ;
    
    for( std::size_t i = 0; i < dims[0]; i++ )
    {
        for( std::size_t j = 0; j < dims[1]; j++ )
        {
            for( std::size_t k = 0; k < dims[2]; k++ )
            {
                const std::size_t index = array3DIndex( i, j, k, dims[0], dims[1], dims[2] );
                output << pos[ index ] << " ";
            }
        }
    }
    output << std::endl;
}

/************************************************************************************/
/*!
 *  @brief          Prints Listener informations
 *
 */
/************************************************************************************/
static void PrintListener(const sofa::File &theFile,
                          std::ostream & output)
{
    {
        sofa::Coordinates::Type coordinates;
        sofa::Units::Type units;
        const bool ok = theFile.GetListenerPosition( coordinates, units );
        
        output << sofa::String::PadWith( "ListenerPosition:Type" ) << " = " << sofa::Coordinates::GetName( coordinates ) << std::endl;
        output << sofa::String::PadWith( "ListenerPosition:Units" ) << " = " << sofa::Units::GetName( units ) << std::endl;
        
        std::vector< std::size_t > dims;
        theFile.GetVariableDimensions( dims, "ListenerPosition" );
        
        SOFA_ASSERT( dims.size() == 2 );
        
        std::vector< double > pos;
        pos.resize( dims[0] * dims[1] );
        
        theFile.GetListenerPosition( &pos[0], dims[0], dims[1] );
        
        output << sofa::String::PadWith( "ListenerPosition" ) << " = " ;
        
        for( std::size_t i = 0; i < dims[0]; i++ )
        {
            for( std::size_t j = 0; j < dims[1]; j++ )
            {
                const std::size_t index = array2DIndex(i, j, dims[0], dims[1] );
                output << pos[ index ] << " ";
            }
        }
        output << std::endl;
    }
    
    output << std::endl;
    
    ///
    {
        sofa::Coordinates::Type coordinates;
        sofa::Units::Type units;
        const bool ok = theFile.GetListenerView( coordinates, units );
        
        output << sofa::String::PadWith( "ListenerView:Type" ) << " = " << sofa::Coordinates::GetName( coordinates ) << std::endl;
        output << sofa::String::PadWith( "ListenerView:Units" ) << " = " << sofa::Units::GetName( units ) << std::endl;
        
        std::vector< std::size_t > dims;
        theFile.GetVariableDimensions( dims, "ListenerView" );
        
        SOFA_ASSERT( dims.size() == 2 );
        
        std::vector< double > pos;
        pos.resize( dims[0] * dims[1] );
        
        theFile.GetListenerView( &pos[0], dims[0], dims[1] );
        
        output << sofa::String::PadWith( "ListenerView" ) << " = " ;
        
        for( std::size_t i = 0; i < dims[0]; i++ )
        {
            for( std::size_t j = 0; j < dims[1]; j++ )
            {
                const std::size_t index = array2DIndex( i, j, dims[0], dims[1] );
                output << pos[ index ] << " ";
            }
        }
        output << std::endl;
    }
    
    output << std::endl;
    
    ///
    {
        sofa::Coordinates::Type coordinates;
        sofa::Units::Type units;
        const bool ok = theFile.GetListenerUp( coordinates, units );
        
        output << sofa::String::PadWith( "ListenerUp:Type" ) << " = " << sofa::Coordinates::GetName( coordinates ) << std::endl;
        output << sofa::String::PadWith( "ListenerUp:Units" ) << " = " << sofa::Units::GetName( units ) << std::endl;
        
        std::vector< std::size_t > dims;
        theFile.GetVariableDimensions( dims, "ListenerUp" );
        
        SOFA_ASSERT( dims.size() == 2 );
        
        std::vector< double > pos;
        pos.resize( dims[0] * dims[1] );
        
        theFile.GetListenerUp( &pos[0], dims[0], dims[1] );
        
        output << sofa::String::PadWith( "ListenerUp" ) << " = " ;
        
        for( std::size_t i = 0; i < dims[0]; i++ )
        {
            for( std::size_t j = 0; j < dims[1]; j++ )
            {
                const std::size_t index = array2DIndex(i,j,dims[0], dims[1]);
                output << pos[ index ] << " ";
            }
        }
        output << std::endl;
    }
}

/************************************************************************************/
/*!
 *  @brief          Prints source informations
 *
 */
/************************************************************************************/
static void PrintSource(const sofa::File &theFile,
                        std::ostream & output)
{
    
    sofa::Coordinates::Type coordinates;
    sofa::Units::Type units;
    const bool ok = theFile.GetSourcePosition( coordinates, units );
    
    output << sofa::String::PadWith( "SourcePosition:Type" ) << " = " << sofa::Coordinates::GetName( coordinates ) << std::endl;
    output << sofa::String::PadWith( "SourcePosition:Units" ) << " = " << sofa::Units::GetName( units ) << std::endl;
    
    std::vector< std::size_t > dims;
    theFile.GetVariableDimensions( dims, "SourcePosition" );
    
    SOFA_ASSERT( dims.size() == 2 );
    
    std::vector< double > pos;
    pos.resize( dims[0] * dims[1] );
    
    theFile.GetSourcePosition( &pos[0], dims[0], dims[1] );
    
    output << sofa::String::PadWith( "SourcePosition" ) << " = " ;
    
    for( std::size_t i = 0; i < dims[0]; i++ )
    {
        for( std::size_t j = 0; j < dims[1]; j++ )
        {
            const std::size_t index = array2DIndex( i, j, dims[0], dims[1] );
            output << pos[ index ] << " ";
        }
    }
    output << std::endl;
}

/************************************************************************************/
/*!
 *  @brief          Main entry point
 *
 */
/************************************************************************************/
int main(int argc, char *argv[])
{

    std::ostream & output = std::cout;
    std::string in;
    
    //==============================================================================
    // Parsing arguments
    //==============================================================================
    if( argc == 2 )
    {
        in = argv[1];
                
        if( in == "h" || in == "-h" || in == "--h" || in == "--help" || in == "-help" )
        {
            DisplayHelp( output );
            return 0;
        }
    }
    else
    {
        DisplayHelp( output );
        return 0;
    }
     
    const std::string filename = in;
    
    try
    {
         
        const sofa::File theFile( filename );
            
        const bool isSOFA = theFile.IsValid();
        
        if( isSOFA == true )
        {
            output << filename << " is a valid SOFA file" << std::endl;
        }
        else
        {
            output << filename << " is not a valid SOFA file" << std::endl;            
            return 0;
        }
                
        const bool paddingForDisplay = true;
        
        sofa::String::PrintSeparationLine( output );
        
        theFile.PrintAllAttributes( output , paddingForDisplay  );
        
        output << std::endl;
        
        sofa::String::PrintSeparationLine( output );
        
        theFile.PrintSOFADimensions( output , paddingForDisplay );

        output << std::endl << std::endl;
        output << std::endl << std::endl;
        output << std::endl << std::endl;
        output << std::endl << std::endl;
        
        const sofa::SimpleFreeFieldHRIR hrir( filename );
        
        const bool isHRIR = hrir.IsValid();
        
        if( isHRIR == true )
        {
            output << filename << " is a valid 'SimpleFreeFieldHRIR' file" << std::endl;
        }
        else
        {
            output << filename << " is not a valid 'SimpleFreeFieldHRIR' file" << std::endl;
            return 0;
        }
        
        double sr = 0.0;
        const bool ok = hrir.GetSamplingRate( sr );
        
        SOFA_ASSERT( ok == true );
        
        sofa::Units::Type units;
        hrir.GetSamplingRateUnits( units );
        
        output << sofa::String::PadWith( "Data.SamplingRate" ) << " = " << sr << std::endl;
        output << sofa::String::PadWith( "Data.SamplingRate:Units" ) << " = " << sofa::Units::GetName( units ) << std::endl;
        
        const unsigned int M = (unsigned int) hrir.GetNumMeasurements();
        const unsigned int R = (unsigned int) hrir.GetNumReceivers();
        const unsigned int N = (unsigned int) hrir.GetNumDataSamples();
        
        /// change this according to your needs
        const bool printListenerInfos   = true;
        const bool printReceiverInfos   = true;
        const bool printSourceInfos     = true;
        const bool printEmitterInfos    = true;
        const bool printData            = false;
        
        if( printListenerInfos == true )
        {
            output << std::endl;
            PrintListener( theFile, output );
        }
        
        if( printReceiverInfos == true )
        {
            output << std::endl;
            PrintReceiver( theFile, output );
        }
        
        if( printSourceInfos == true )
        {
            output << std::endl;
            PrintSource( theFile, output );
        }
        
        if( printEmitterInfos == true )
        {
            output << std::endl;
            PrintEmitter( theFile, output );
        }
        
        if( printData == true )
        {
            std::vector< double > data;
             
            hrir.GetDataIR( data );
             
            for( std::size_t i = 0; i < M; i++ )
            {
                for( std::size_t j = 0; j < R; j++ )
                {
                    for( std::size_t k = 0; k < N; k++ )
                    {
                        const std::size_t index = array3DIndex( i, j, k, M, R, N );
                        output << data[ index ] << std::endl;
                    }
                }
            }
        }
        
    }
    catch( sofa::Exception &e )
    {
        /// the description of the exception will be printed when raised
        exit(1);
    }
    catch( std::exception &e )
    {
        std::cerr << "unknown exception occured : " << e.what() << std::endl;
        exit(1);
    }
    catch( ... )
    {
        std::cerr << "unknown exception occured" << std::endl;
        exit(1);
    }

    return 0;
}

