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

/*


Global Attributes:
	APIName                        = ARI SOFA API for Matlab/Octave
	APIVersion                     = 0.4.2
	ApplicationName                = itaToolbox
	ApplicationVersion             = 
	AuthorContact                  =  (@)
	Comment                        = 
	Conventions                    = SOFA
	DataType                       = FIR
	DatabaseName                   = 
	DateCreated                    = 24-Sep-2016
	DateModified                   = 2016-09-24 17:23:10
	History                        = 
	License                        = No license provided, ask the author for permission
	ListenerShortName              = 
	Organization                   = 
	Origin                         = 
	References                     = 
	RoomType                       = free field
	SOFAConventions                = SimpleFreeFieldHRIR
	SOFAConventionsVersion         = 0.4
	Title                          = 
	Version                        = 0.6

Variables:
	Data.Delay
		Datatype:                     double
		Dimensions:                   I,R
		Size:                         1 x 2
0,0
	Data.IR
		Datatype:                     double
		Dimensions:                   M,R,N
		Size:                         2304 x 2 x 256
-0.000113541,-1.75549e-06,-0.000115488,3.48965e-06,-0.000117226,8.33753e-06,-0.000118438,1.62399e-05,-0.000118946,2.17615e-05
	Data.SamplingRate
		Datatype:                     double
		Dimensions:                   I
		Size:                         1
		Attributes:                   1
			Units                          = hertz
44100
	EmitterPosition
		Datatype:                     double
		Dimensions:                   E,C,I
		Size:                         1 x 3 x 1
		Attributes:                   1 x 3 x 1
			Type                           = cartesian
			Units                          = meter
0,0,0
	ListenerPosition
		Datatype:                     double
		Dimensions:                   I,C
		Size:                         1 x 3
		Attributes:                   1 x 3
			Type                           = cartesian
			Units                          = meter
0,0,0
	ListenerUp
		Datatype:                     double
		Dimensions:                   I,C
		Size:                         1 x 3
0,0,1
	ListenerView
		Datatype:                     double
		Dimensions:                   I,C
		Size:                         1 x 3
		Attributes:                   1 x 3
			Type                           = cartesian
			Units                          = meter
1,0,0
	ReceiverPosition
		Datatype:                     double
		Dimensions:                   R,C,I
		Size:                         2 x 3 x 1
		Attributes:                   2 x 3 x 1
			Type                           = cartesian
			Units                          = meter
0,-0.09,0,0,0.09,0
	SourcePosition
		Datatype:                     double
		Dimensions:                   M,C
		Size:                         2304 x 3
		Attributes:                   2304 x 3
			Type                           = cartesian
			Units                          = meter
7.34788e-17,-1.79971e-32,1.2,0.105421,-2.58208e-17,1.19536,0.210028,-5.14419e-17,1.18148,0.31301


or

	SourcePosition
		Datatype:                     double
		Dimensions:                   M,C
		Size:                         360 x 3
		Attributes:                   360 x 3
			Type                           = spherical
			Units                          = degree, degree, meter
180,0,1,181,0,1,182,0,1,183

*/



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

bool verifyVariable(const sofa::NetCDFFile &file, const char *name, const char *type, const char *dimensions, const double *values)
{
	if(file.GetVariableTypeName( name ).compare(type))
		return false;

	if(file.GetVariableDimensionsNamesAsString( name ).compare(dimensions))
		return false;

//            const std::string dims      = file.GetVariableDimensionsAsString( name );
//            std::vector< std::string > attributeNames;
//            std::vector< std::string > attributeValues;
//            file.GetVariablesAttributes( attributeNames, attributeValues, name );
//          SOFA_ASSERT( attributeNames.size() == attributeValues.size() );
//                output << sofa::String::PadWith( attributeNames[j] ) << " = " << attributeValues[j] << std::endl;

	if(values) {
	    std::vector<double> v;
	    file.GetValues(v,name);
            for( std::size_t j = 0; j < v.size() ; j++ )
		if(v[j]!=values[j])
			return false;
	}

	return true;
}





/* Parse the data set definition and process the source data, storing the
 * resulting data set as desired.  If the input name is NULL it will read
 * from standard input.
 */
int ProcessDefinitionSofa(const char *inName, const uint outRate, const uint fftSize, const int equalize, const int surface, const double limit, const uint truncSize, const HeadModelT model, const double radius, const OutputFormatT outFormat, const char *outName)
{
	std::ostream & output = std::cout;
    	const bool validSimpleFreeFieldHRIR     = sofa::IsValidSimpleFreeFieldHRIRFile( inName );

    if(!validSimpleFreeFieldHRIR) {
            fprintf(stderr, "Error: Expecting a SimpleFreeFieldHRIR sofa format\n");
            return 0;
    }


    ///@n this doesnt check whether the file corresponds to SOFA conventions...
	const sofa::NetCDFFile file( inName );
    
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

	if( file.GetDimension("C") != 3 ||
	    file.GetDimension("I") != 1) {
		fprintf(stderr, "Error: Expecting C=3 and I=1 but the sofa file has C=%lu and I=%lu\n",file.GetDimension("C"),file.GetDimension("I"));
            	return 0;
	}
	if( file.GetDimension("E") != 1 ||
	    file.GetDimension("R") != 2) {
		fprintf(stderr, "Error: Expecting one exmitter and two receivers but the sofa file has E=%lu and R=%lu\n",
			file.GetDimension("E"),file.GetDimension("R"));
            	return 0;
	}
	size_t numMeasurements = file.GetDimension("M");
	size_t numDataSamples = file.GetDimension("N");
    
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

	    std::vector<double> values;
	    file.GetValues(values,name);
            for( std::size_t j = 0; j < values.size() && j < 10 ; j++ )
            {
                output << values[j] << ((j==values.size()-1 || j==9) ? "\n" : ",");
            }
        }
        
    }

	/* verify format */
	const double array1[] = { 1, 0, 0 };
	if(!verifyVariable(file,"ListenerView","double","I,C", array1)) {
		fprintf(stderr, "Error: Expecting ListenerView 1,0,0\n");
		return 0;
	}

	const double array2[] = { 0, 0, 1 };
	if(!verifyVariable(file,"ListenerUp","double","I,C", array2)) {
		fprintf(stderr, "Error: Expecting ListenerUp 0,0,1\n");
		return 0;
	}

	const double array3[] = { 0, 0, 0 };
	if(!verifyVariable(file,"ListenerPosition","double","I,C", array3)) {
		fprintf(stderr, "Error: Expecting ListenerPosition 0,0,0\n");
		return 0;
	}

	const double array4[] = { 0, 0, 0 };
	if(!verifyVariable(file,"EmitterPosition","double","I,C", array4)) {
		fprintf(stderr, "Error: Expecting EmitterPosition 0,0,0\n");
		return 0;
	}

	/* read hrtf */
	std::vector< double > values;
	HrirDataT hData;

	hData.mIrRate = 0;
	hData.mIrPoints = 0;
	hData.mFftSize = 0;
	hData.mIrSize = 0;
	hData.mIrCount = 0;
	hData.mEvCount = 0;
	hData.mRadius = 0;
	hData.mDistance = 0;	

	file.GetValues(values, "Data.SamplingRate");
	if(!verifyVariable(file,"Data.SamplingRate","double","I",NULL) || values.size()!=1) {
		fprintf(stderr, "Error: Expecting sampling rate\n");
            	return 0;
	}
	hData.mIrRate = values[0];


	file.GetValues(values, "ReceiverPosition");
	if(!verifyVariable(file,"ReceiverPosition","double","R,C,I",NULL) || values.size()!=6) {
		fprintf(stderr, "Error: Expecting sampling rate\n");
            	return 0;
	}
//	hData.mIrRate = values[0]; TODO

	fprintf(stderr,"\n\nall done\n\n");
	return 0;

#if 0


// Process the data set definition to read and validate the data set metrics.
static int ProcessMetrics(TokenReaderT *tr, const uint fftSize, const uint truncSize, HrirDataT *hData)
{
    int hasRate = 0, hasPoints = 0, hasAzimuths = 0;
    int hasRadius = 0, hasDistance = 0;
    char ident[MAX_IDENT_LEN+1];
    uint line, col;
    double fpVal;
    uint points;
    int intVal;

    while(!(hasRate && hasPoints && hasAzimuths && hasRadius && hasDistance))
    {
...
        else if(strcasecmp(ident, "points") == 0)
        {
            if (hasPoints) {
                TrErrorAt(tr, line, col, "Redefinition of 'points'.\n");
                return 0;
            }
            if(!TrReadOperator(tr, "="))
                return 0;
            TrIndication(tr, &line, &col);
            if(!TrReadInt(tr, MIN_POINTS, MAX_POINTS, &intVal))
                return 0;
            points = (uint)intVal;
            if(fftSize > 0 && points > fftSize)
            {
                TrErrorAt(tr, line, col, "Value exceeds the overridden FFT size.\n");
                return 0;
            }
            if(points < truncSize)
            {
                TrErrorAt(tr, line, col, "Value is below the truncation size.\n");
                return 0;
            }
            hData->mIrPoints = points;
            hData->mFftSize = fftSize;
            if(fftSize <= 0)
            {
                points = 1;
                while(points < (4 * hData->mIrPoints))
                    points <<= 1;
                hData->mFftSize = points;
                hData->mIrSize = 1 + (points / 2);
            }
            else
            {
                hData->mFftSize = fftSize;
                hData->mIrSize = 1 + (fftSize / 2);
                if(points > hData->mIrSize)
                    hData->mIrSize = points;
            }
            hasPoints = 1;
        }
        else if(strcasecmp(ident, "azimuths") == 0)
        {
            if(hasAzimuths)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'azimuths'.\n");
                return 0;
            }
            if(!TrReadOperator(tr, "="))
                return 0;
            hData->mIrCount = 0;
            hData->mEvCount = 0;
            hData->mEvOffset[0] = 0;
            for(;;)
            {
                if(!TrReadInt(tr, MIN_AZ_COUNT, MAX_AZ_COUNT, &intVal))
                    return 0;
                hData->mAzCount[hData->mEvCount] = (uint)intVal;
                hData->mIrCount += (uint)intVal;
                hData->mEvCount ++;
                if(!TrIsOperator(tr, ","))
                    break;
                if(hData->mEvCount >= MAX_EV_COUNT)
                {
                    TrError(tr, "Exceeded the maximum of %d elevations.\n", MAX_EV_COUNT);
                    return 0;
                }
                hData->mEvOffset[hData->mEvCount] = hData->mEvOffset[hData->mEvCount - 1] + ((uint)intVal);
                TrReadOperator(tr, ",");
            }
            if(hData->mEvCount < MIN_EV_COUNT)
            {
                TrErrorAt(tr, line, col, "Did not reach the minimum of %d azimuth counts.\n", MIN_EV_COUNT);
                return 0;
            }
            hasAzimuths = 1;
        }
        else if(strcasecmp(ident, "radius") == 0)
        {
            if(hasRadius)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'radius'.\n");
                return 0;
            }
            if(!TrReadOperator(tr, "="))
                return 0;
            if(!TrReadFloat(tr, MIN_RADIUS, MAX_RADIUS, &fpVal))
                return 0;
            hData->mRadius = fpVal;
            hasRadius = 1;
        }
        else if(strcasecmp(ident, "distance") == 0)
        {
            if(hasDistance)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'distance'.\n");
                return 0;
            }
            if(!TrReadOperator(tr, "="))
                return 0;
            if(!TrReadFloat(tr, MIN_DISTANCE, MAX_DISTANCE, & fpVal))
                return 0;
            hData->mDistance = fpVal;
            hasDistance = 1;
        }
        else
        {
            TrErrorAt(tr, line, col, "Expected a metric name.\n");
            return 0;
        }
        TrSkipWhitespace (tr);
    }
    return 1;
}




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
}


