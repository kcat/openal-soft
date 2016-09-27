/************************************************************************************/
/*  FILE DESCRIPTION                                                                */
/*----------------------------------------------------------------------------------*/
/*!
 *   @file       sofawrite.cpp
 *   @brief      Various code examples... Just adapt these pieces of code to your needs...
 *   @author     Thibaut Carpentier, UMR STMS 9912 - Ircam-Centre Pompidou / CNRS / UPMC
 *
 *   @date       15/10/2014
 *
 */
/************************************************************************************/
#include "SOFA.h"
#include "ncDim.h"
#include "ncVar.h"

#include "../include/AL/alext.h"
#include "../Alc/hrtf.h"
#include "../makehrtf.h"
#include "SOFAtoOpenAL.h"





/************************************************************************************/
/*!
 *  @brief          Example for testing whether a file match a SOFA convention or not
 *                  without raising any exception
 *
 */
/************************************************************************************/
static void TestFileConvention(const std::string & filename,
                               std::ostream & output = std::cout)
{
    
    const bool validnetCDF                  = sofa::IsValidNetCDFFile( filename );
    const bool validSOFA                    = sofa::IsValidSOFAFile( filename );
    const bool validSimpleFreeFieldHRIR     = sofa::IsValidSimpleFreeFieldHRIRFile( filename );
    const bool validSimpleFreeFieldSOS      = sofa::IsValidSimpleFreeFieldSOSFile( filename );
    const bool validSimpleHeadphoneIR       = sofa::IsValidSimpleHeadphoneIRFile( filename );
    const bool validGeneralFIR              = sofa::IsValidGeneralFIRFile( filename );
    const bool validGeneralTF               = sofa::IsValidGeneralTFFile( filename );
    
    output << "netCDF               = " << sofa::String::bool2yesorno( validnetCDF ) << std::endl;
    output << "SOFA                 = " << sofa::String::bool2yesorno( validSOFA ) << std::endl;
    output << "SimpleFreeFieldHRIR  = " << sofa::String::bool2yesorno( validSimpleFreeFieldHRIR ) << std::endl;
    output << "SimpleFreeFieldSOS   = " << sofa::String::bool2yesorno( validSimpleFreeFieldSOS ) << std::endl;
    output << "SimpleHeadphoneIR    = " << sofa::String::bool2yesorno( validSimpleHeadphoneIR ) << std::endl;
    output << "GeneralFIR           = " << sofa::String::bool2yesorno( validGeneralFIR ) << std::endl;
    output << "GeneralTF            = " << sofa::String::bool2yesorno( validGeneralTF ) << std::endl;
}

/************************************************************************************/
/*!
 *  @brief          Example for displaying all informations about a NetCDFFile file,
 *                  in a fashion similar to matlab 'ncdisp' command
 *
 */
/************************************************************************************/
static void DisplayInformations(const std::string & filename,
                                std::ostream & output = std::cout)
{
    ///@n this doesnt check whether the file corresponds to SOFA conventions...
    const sofa::NetCDFFile file( filename );
    
    const std::string tabSeparator = "\t";
    
    //==============================================================================
    // global attributes
    //==============================================================================
    {
        std::vector< std::string > attributeNames;
        file.GetAllAttributesNames( attributeNames );
        
        output << std::endl;
        output << "Global Attributes:" << std::endl;
        
        for( std::size_t i = 0; i < attributeNames.size(); i++ )
        {
            const std::string name = attributeNames[i];
            const std::string value= file.GetAttributeValueAsString( name );
            
            output << tabSeparator << sofa::String::PadWith( attributeNames[i] ) << " = " << value << std::endl;
        }
    }
    
    //==============================================================================
    // dimensions
    //==============================================================================
    {
        std::vector< std::string > dimensionNames;
        file.GetAllDimensionsNames( dimensionNames );
        
        output << std::endl;
        output << "Dimensions:" << std::endl;
        
        for( std::size_t i = 0; i < dimensionNames.size(); i++ )
        {
            const std::string name = dimensionNames[i];
            const std::size_t dim  = file.GetDimension( name );
            output << tabSeparator << name << " = " << dim << std::endl;
        }
    }
    
    //==============================================================================
    // variables
    //==============================================================================
    {
        std::vector< std::string > variableNames;
        file.GetAllVariablesNames( variableNames );
     
        output << std::endl;
        output << "Variables:" << std::endl;
        
        for( std::size_t i = 0; i < variableNames.size(); i++ )
        {
            const std::string name      = variableNames[i];
            const std::string typeName  = file.GetVariableTypeName( name );

            const std::string dimsNames = file.GetVariableDimensionsNamesAsString( name );
            const std::string dims      = file.GetVariableDimensionsAsString( name );
            
            output << tabSeparator << name << std::endl;
            output << tabSeparator << tabSeparator << sofa::String::PadWith( "Datatype: " ) << typeName << std::endl;
            output << tabSeparator << tabSeparator << sofa::String::PadWith( "Dimensions: ") << dimsNames << std::endl;;
            output << tabSeparator << tabSeparator << sofa::String::PadWith( "Size: ") << dims << std::endl;;
            
            std::vector< std::string > attributeNames;
            std::vector< std::string > attributeValues;
            file.GetVariablesAttributes( attributeNames, attributeValues, name );
            
            SOFA_ASSERT( attributeNames.size() == attributeValues.size() );
            
            if( attributeNames.size() > 0 )
            {
                output << tabSeparator << tabSeparator << sofa::String::PadWith( "Attributes: ") << dims << std::endl;;
            }
            
            for( std::size_t j = 0; j < attributeNames.size(); j++ )
            {
                output << tabSeparator << tabSeparator << tabSeparator;
                output << sofa::String::PadWith( attributeNames[j] ) << " = " << attributeValues[j] << std::endl;
            }
        }
        
    }
}

/************************************************************************************/
/*!
 *  @brief          Example for creating a SOFA file with the SimpleFreeFieldHRIR convention
 *
 */
/************************************************************************************/
static void CreateSimpleFreeFieldHRIRFile()
{
    //==============================================================================
    /// create the file
    
    /// for creating a new file
    const netCDF::NcFile::FileMode mode = netCDF::NcFile::newFile;
    
    /// the file format that is used (netCDF4 / HDF5)
    const netCDF::NcFile::FileFormat format = netCDF::NcFile::nc4;
    
    /// the file shall not exist beforehand
    const std::string filePath = "/Users/tcarpent/Desktop/testwrite.sofa";
    
    const netCDF::NcFile theFile( filePath, mode, format );
    
    //==============================================================================
    /// create the attributes
    sofa::Attributes attributes;
    attributes.ResetToDefault();
    
    /// fill the attributes as you want
    {
        attributes.Set( sofa::Attributes::kRoomLocation,   "IRCAM, Paris" );
        attributes.Set( sofa::Attributes::kRoomShortName,  "IRCAM Anechoic Room" );
        /// etc.
    }
    
    /// put all the attributes into the file
    for( unsigned int k = 0; k < sofa::Attributes::kNumAttributes; k++ )
    {
        const sofa::Attributes::Type attType = static_cast< sofa::Attributes::Type >(k);
        
        const std::string attName  = sofa::Attributes::GetName( attType );
        const std::string attValue = attributes.Get( attType );
        
        theFile.putAtt( attName, attValue );
    }
    
    /// add attribute specific to your convention (e.g. 'DatabaseName' for the 'SimpleFreeFieldHRIR' convention)
    {
        const std::string attName  = "DatabaseName";
        const std::string attValue = "TestDatabase";
        
        theFile.putAtt( attName, attValue );
    }
    
    //==============================================================================
    /// create the dimensions
    const unsigned int numMeasurements  = 1680;
    const unsigned int numReceivers     = 2;
    const unsigned int numEmitters      = 1;
    const unsigned int numDataSamples   = 941;
    
    theFile.addDim( "C", 3 );   ///< this is required by the standard
    theFile.addDim( "I", 1 );   ///< this is required by the standard
    theFile.addDim( "M", numMeasurements );
    theFile.addDim( "R", numReceivers );
    theFile.addDim( "E", numEmitters );
    theFile.addDim( "N", numDataSamples );
    
    //==============================================================================
    /// create the variables
    
    /// Data.SamplingRate
    {
        const std::string varName  = "Data.SamplingRate";
        const std::string typeName = "double";
        const std::string dimName  = "I";
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimName );
        
        const double samplingRate = 48000;
        
        var.putVar( &samplingRate );
        var.putAtt( "Units", "hertz" );
    }
    
    /// Data.Delay
    {
        const std::string varName  = "Data.Delay";
        const std::string typeName = "double";
        
        std::vector< std::string > dimNames;
        dimNames.push_back("I");
        dimNames.push_back("R");
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimNames );
        
        ///@todo : fill the variable
    }
    
    /// ListenerPosition
    {
        const std::string varName  = "ListenerPosition";
        const std::string typeName = "double";
        
        std::vector< std::string > dimNames;
        dimNames.push_back("I");
        dimNames.push_back("C");
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimNames );
        
        var.putAtt( "Type", "cartesian" );
        var.putAtt( "Units", "meter" );
        
        ///@todo : fill the variable
    }
    
    /// ListenerUp
    {
        const std::string varName  = "ListenerUp";
        const std::string typeName = "double";
        
        std::vector< std::string > dimNames;
        dimNames.push_back("I");
        dimNames.push_back("C");
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimNames );
        
        ///@todo : fill the variable
    }
    
    /// ListenerView
    {
        const std::string varName  = "ListenerView";
        const std::string typeName = "double";
        
        std::vector< std::string > dimNames;
        dimNames.push_back("I");
        dimNames.push_back("C");
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimNames );
        
        var.putAtt( "Type", "cartesian" );
        var.putAtt( "Units", "meter" );
        
        ///@todo : fill the variable
    }
    
    /// ReceiverPosition
    {
        const std::string varName  = "ReceiverPosition";
        const std::string typeName = "double";
        
        std::vector< std::string > dimNames;
        dimNames.push_back("R");
        dimNames.push_back("C");
        dimNames.push_back("I");
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimNames );
        
        var.putAtt( "Type", "cartesian" );
        var.putAtt( "Units", "meter" );
        
        ///@todo : fill the variable
    }
    
    /// SourcePosition
    {
        const std::string varName  = "SourcePosition";
        const std::string typeName = "double";
        
        std::vector< std::string > dimNames;
        dimNames.push_back("M");
        dimNames.push_back("C");
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimNames );
        
        var.putAtt( "Type", "spherical" );
        var.putAtt( "Units", "degree, degree, meter" );
        
        ///@todo : fill the variable
    }
    
    /// EmitterPosition
    {
        const std::string varName  = "EmitterPosition";
        const std::string typeName = "double";
        
        std::vector< std::string > dimNames;
        dimNames.push_back("E");
        dimNames.push_back("C");
        dimNames.push_back("I");
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimNames );
        
        var.putAtt( "Type", "cartesian" );
        var.putAtt( "Units", "meter" );
        
        ///@todo : fill the variable
        const double fillValue = 0.0;
        var.setFill( true, fillValue );
    }
    
    /// Data.IR
    {
        const std::string varName  = "Data.IR";
        const std::string typeName = "double";
        
        std::vector< std::string > dimNames;
        dimNames.push_back("M");
        dimNames.push_back("R");
        dimNames.push_back("N");
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimNames );
        
        ///@todo : fill the variable
    }
    
    /// RoomVolume
    {
        const std::string varName  = "RoomVolume";
        const std::string typeName = "double";
        
        const std::string dimName  = "I";
        
        const netCDF::NcVar var = theFile.addVar( varName, typeName, dimName );
        
        var.putAtt( "Units", "cubic meter" );
        
        const double roomVolume = 103;
        var.putVar( &roomVolume );
    }
    
    ///@todo add any other variables, as you need
}

/************************************************************************************/
/*!
 *  @brief          Main entry point
 *
 */
/************************************************************************************/
struct Hrtf * loadSofa(const char *filename)
{
    TestFileConvention( filename );
    DisplayInformations( filename );
    /// example for creating a SimpleFreeFieldHRIR file
    //CreateSimpleFreeFieldHRIRFile();
    
    return NULL;
}


/* Parse the data set definition and process the source data, storing the
 * resulting data set as desired.  If the input name is NULL it will read
 * from standard input.
 */
int ProcessDefinitionSofa(const char *inName, const uint outRate, const uint fftSize, const int equalize, const int surface, const double limit, const uint truncSize, const HeadModelT model, const double radius, const OutputFormatT outFormat, const char *outName)
{
#if 0
    char rateStr[8+1], expName[MAX_PATH_LEN];
    TokenReaderT tr;
    HrirDataT hData;
    double *dfa;
    FILE *fp;

    hData.mIrRate = 0;
    hData.mIrPoints = 0;
    hData.mFftSize = 0;
    hData.mIrSize = 0;
    hData.mIrCount = 0;
    hData.mEvCount = 0;
    hData.mRadius = 0;
    hData.mDistance = 0;
    fprintf(stdout, "Reading HRIR definition...\n");
    if(inName != NULL)
    {
        fp = fopen(inName, "r");
        if(fp == NULL)
        {
            fprintf(stderr, "Error: Could not open definition file '%s'\n", inName);
            return 0;
        }
        TrSetup(fp, inName, &tr);
    }
    else
    {
        fp = stdin;
        TrSetup(fp, "<stdin>", &tr);
    }
    if(!ProcessMetrics(&tr, fftSize, truncSize, &hData))
    {
        if(inName != NULL)
            fclose(fp);
        return 0;
    }
    hData.mHrirs = CreateArray(hData.mIrCount * hData . mIrSize);
    hData.mHrtds = CreateArray(hData.mIrCount);
    if(!ProcessSources(model, &tr, &hData))
    {
        DestroyArray(hData.mHrtds);
        DestroyArray(hData.mHrirs);
        if(inName != NULL)
            fclose(fp);
        return 0;
    }
    if(inName != NULL)
        fclose(fp);
    if(equalize)
    {
        dfa = CreateArray(1 + (hData.mFftSize/2));
        fprintf(stdout, "Calculating diffuse-field average...\n");
        CalculateDiffuseFieldAverage(&hData, surface, limit, dfa);
        fprintf(stdout, "Performing diffuse-field equalization...\n");
        DiffuseFieldEqualize(dfa, &hData);
        DestroyArray(dfa);
    }
    fprintf(stdout, "Performing minimum phase reconstruction...\n");
    ReconstructHrirs(&hData);
    if(outRate != 0 && outRate != hData.mIrRate)
    {
        fprintf(stdout, "Resampling HRIRs...\n");
        ResampleHrirs(outRate, &hData);
    }
    fprintf(stdout, "Truncating minimum-phase HRIRs...\n");
    hData.mIrPoints = truncSize;
    fprintf(stdout, "Synthesizing missing elevations...\n");
    if(model == HM_DATASET)
        SynthesizeOnsets(&hData);
    SynthesizeHrirs(&hData);
    fprintf(stdout, "Normalizing final HRIRs...\n");
    NormalizeHrirs(&hData);
    fprintf(stdout, "Calculating impulse delays...\n");
    CalculateHrtds(model, (radius > DEFAULT_CUSTOM_RADIUS) ? radius : hData.mRadius, &hData);
    snprintf(rateStr, 8, "%u", hData.mIrRate);
    StrSubst(outName, "%r", rateStr, MAX_PATH_LEN, expName);
    switch(outFormat)
    {
        case OF_MHR:
            fprintf(stdout, "Creating MHR data set file...\n");
            if(!StoreMhr(&hData, expName))
            {
                DestroyArray(hData.mHrtds);
                DestroyArray(hData.mHrirs);
                return 0;
            }
            break;
        default:
            break;
    }
    DestroyArray(hData.mHrtds);
    DestroyArray(hData.mHrirs);
#endif
    return 1;
}


