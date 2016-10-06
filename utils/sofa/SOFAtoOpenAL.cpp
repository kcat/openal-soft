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
#include <math.h>

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

enum AttributeType { ATTR_TYPE_NONE, ATTR_TYPE_CARTESIAN, ATTR_TYPE_SPHERICAL };


bool iequals(const std::string& a, const char *b)
{
    unsigned int sz = a.size();
    if (strlen(b) != sz)
        return false;
    for (unsigned int i = 0; i < sz; ++i)
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    return true;
}

bool verifyVariable(const sofa::NetCDFFile &file, const char *name, const char *type, const char *dimensions, 
	size_t size=0, const double *values=NULL, AttributeType at=ATTR_TYPE_NONE)
{
	if(file.GetVariableTypeName( name ).compare(type))
		return false;

	if(file.GetVariableDimensionsNamesAsString( name ).compare(dimensions))
		return false;

//            const std::string dims      = file.GetVariableDimensionsAsString( name );
//                output << sofa::String::PadWith( attributeNames[j] ) << " = " << attributeValues[j] << std::endl;

	std::vector<double> v;
	file.GetValues(v,name);
	if(size>0 && v.size()!=size)
		return false;

	if(values) {
            for( std::size_t j = 0; j < v.size() ; j++ )
		if(v[j]!=values[j])
			return false;
	}

        std::vector< std::string > attributeNames;
        std::vector< std::string > attributeValues;
        file.GetVariablesAttributes( attributeNames, attributeValues, name );
        SOFA_ASSERT( attributeNames.size() == attributeValues.size() );

	if(at == ATTR_TYPE_CARTESIAN) {
		bool found=false;
		for( std::size_t j = 0; j < attributeNames.size() ; j++ ) {
			if(iequals(attributeNames[j],"Type") && iequals(attributeValues[j],"cartesian")) {
				found=true;
				break;
			}
		}
		if(!found)
			return false;

		found=false;
		for( std::size_t j = 0; j < attributeNames.size() ; j++ ) {
			if(iequals(attributeNames[j],"Units") && (iequals(attributeValues[j],"metre") ||
				iequals(attributeValues[j],"meter") || 
				iequals(attributeValues[j],"meters") ||
				iequals(attributeValues[j],"metres") )) {
				found=true;
				break;
			}
		}
		if(!found)
			return false;
	}
	else if(at == ATTR_TYPE_CARTESIAN) {
		bool found=false;
		for( std::size_t j = 0; j < attributeNames.size() ; j++ ) {
			if(iequals(attributeNames[j],"Type") && iequals(attributeValues[j],"spherical")) {
				found=true;
				break;
			}
		}
		if(!found)
			return false;

		found=false;
		for( std::size_t j = 0; j < attributeNames.size() ; j++ ) {
			if(iequals(attributeNames[j],"Units") && (iequals(attributeValues[j],"metre, metre, degree") ||
				iequals(attributeValues[j],"meter, meter, degree") || 
				iequals(attributeValues[j],"meters, meters, degrees") ||
				iequals(attributeValues[j],"metres, metres, degrees") )) {
				found=true;
				break;
			}
		}
		if(!found)
			return false;
	}

	return true;
}


void convertCartesianToSpherical(std::vector< double > &values)
{
 	SOFA_ASSERT( values.size() % 3 == 0 );

	for(size_t i = 0; i < values.size(); i += 3) {
		double x = values[i];
		double y = values[i+1];
		double z = values[i+2];
		double r = sqrt(x*x + y*y + z*z);

		double theta = atan2(z,sqrt(x*x + y*y));
		double phi = atan2(y,x);

		values[i] = fmod(phi * 180 / M_PI + 360, 360);
		values[i+1] = theta * 180 / M_PI;
		values[i+2] = r;

//		printf("%7.2f %7.2f %7.2f %7.2f %7.2f %7.2f\n", x, y, z, values[i], values[i+1], values[i+2]);
	}
}

/**
OpenAL Soft uses a clockwise azimuth convention while SOFA uses
counter-clockwise.  Thus, we invert the azimuth of all
sources.  
*/

void convertSOFAtoOpenAL(std::vector< double > &values)
{
 	SOFA_ASSERT( values.size() % 3 == 0 );

	for(size_t i = 0; i < values.size(); i += 3) {
		double phi = values[i];
		phi = fmod(720 - phi, 360); 
		values[i] = phi;
	}
}


double getMeanDistance(const std::vector< double > &values)
{
 	SOFA_ASSERT( values.size() % 3 == 0 );

	double distance = 0;

	for(size_t i = 2; i < values.size(); i += 3) {
		distance += values[i];
	}

	return distance * 3 / values.size();
}

/* Comparison function. Receives two generic (void) pointers to the items under comparison. */
extern "C" int compare_c(const void *p, const void *q) {

    const double *x = (const double *)p;
    const double *y = (const double *)q;

    /* Avoid return x - y, which can cause undefined behaviour
       because of signed integer overflow. */
    if (x[1] < y[1])
        return -1;  // Return -1 if you want ascending, 1 if you want descending order. 
    else if (x[1] > y[1])
        return 1;   // Return 1 if you want ascending, -1 if you want descending order. 

    if (x[0] < y[0])
        return -1;  // Return -1 if you want ascending, 1 if you want descending order. 
    else if (x[0] > y[0])
        return 1;   // Return 1 if you want ascending, -1 if you want descending order. 

    return 0;
}

void sortSourcePositions(std::vector< double > &values)
{
 	SOFA_ASSERT( values.size() % 3 == 0 );

// round theta values to allow for proper sorting
	for(size_t i = 0; i < values.size(); i += 3) {
		values[i+1] = round(values[i+1]*100) / 100;
		values[i+2] = i/3;		// we do not need the distance anymore, but the index before sorting
	}

	qsort(&values[0], values.size()/3, sizeof(double)*3, &compare_c);

#ifdef VERBOSE
	for(size_t i = 0; i < values.size(); i += 3) {
		printf("%7.2f %7.2f %7.2f\n", values[i], values[i+1], values[i+2]);
	}
#endif
}


void countElevationsAndAzimuths(const std::vector< double > &values, HrirDataT &hData)
{
	hData.mIrCount = 0;
	hData.mEvStart = 0;
	hData.mEvCount = 0;
	double lastElevation = -100;

	for(size_t i = 0; i < values.size(); i += 3) {
		if(lastElevation != values[i+1]) {
		        hData.mEvOffset[hData.mEvCount] = hData.mIrCount;
			hData.mAzCount[hData.mEvCount] = 0;
			lastElevation = values[i+1];
			hData.mEvCount++;
		}		
                hData.mAzCount[hData.mEvCount-1]++;
		hData.mIrCount++;
	}

#ifdef VERBOSE
	for(size_t i = 0; i < hData.mEvCount; i++) {
		std::cout << i << "\t" << hData.mEvOffset[i] << "\t" << hData.mAzCount[i] << std::endl;
	}
	std::cout << hData.mIrCount << std::endl;
#endif
}

void readData(const std::vector< double > values,const std::vector< double > data, HrirDataT &hData)
{
	size_t numMeasurements = values.size() / 3;
	size_t numDataSamples = data.size() / 2 / numMeasurements;
	double *pL = hData.mHrirs;
	double *pR = hData.mHrirs + (numMeasurements * hData.mIrSize);

	for(size_t i = 0; i < values.size(); i += 3) {
		size_t offset = values[i+2] * 2 * numDataSamples;		// index before sorting
		size_t j;
		for(j=0; j < numDataSamples; j++) {
			*pL++ = data[offset+(j*2)];		
			*pR++ = data[offset+(j*2)+1];
		}
		for(;j < hData.mIrSize; j++) {
			*pL++ = 0;
			*pR++ = 0;
		}
	}

       SOFA_ASSERT( (pL-hData.mHrirs) == (hData.mIrSize*hData.mIrCount) );
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
    
#ifdef VERBOSE
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
#endif
    
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
    
#ifdef VERBOSE
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
#endif

	/* verify format */

	if(file.HasVariable("ListenerView")) {
		const double array1[] = { 1, 0, 0 };
		if(!verifyVariable(file,"ListenerView","double","I,C", 3, array1)) {
			if(!verifyVariable(file,"ListenerView","double","I,C", 3, array1, ATTR_TYPE_CARTESIAN)) {
				const double array1b[] = { 0, 0, 1 };
				if(!verifyVariable(file,"ListenerView","double","I,C", 3, array1b, ATTR_TYPE_SPHERICAL)) {
					fprintf(stderr, "Error: Expecting ListenerView 1,0,0\n");
					return 0;
				}
			}
		}
	}

	if(file.HasVariable("ListenerUp")) {
		const double array2[] = { 0, 0, 1 };
		if(!verifyVariable(file,"ListenerUp","double","I,C", 3, array2)) {
			if(!verifyVariable(file,"ListenerUp","double","I,C", 3, array2, ATTR_TYPE_CARTESIAN)) {
				const double array1b[] = { 0, 90, 1 };
				if(!verifyVariable(file,"ListenerUp","double","I,C", 3, array1b, ATTR_TYPE_SPHERICAL)) {
					fprintf(stderr, "Error: Expecting ListenerUp 0,0,1\n");
					return 0;
				}
			}
		}
	}

	const double array3[] = { 0, 0, 0 };
	if(!verifyVariable(file,"ListenerPosition","double","I,C", 3, array3, ATTR_TYPE_CARTESIAN)) {
		fprintf(stderr, "Error: Expecting ListenerPosition 0,0,0\n");
		return 0;
	}

	const double array4[] = { 0, 0, 0 };
	if(!verifyVariable(file,"EmitterPosition","double","E,C,I", 3, array4, ATTR_TYPE_CARTESIAN)) {
		fprintf(stderr, "Error: Expecting EmitterPosition 0,0,0\n");
		return 0;
	}

// TODO: Support data delays for each measurement and for other values
	const double array5[] = { 0, 0 };
	if(!verifyVariable(file,"Data.Delay","double","I,R", 2, array5)) {
		fprintf(stderr, "Error: Expecting Data.Delay 0,0\n");
		return 0;
	}


	/* read hrtf */
	std::vector< double > values;
	HrirDataT hData;

	hData.mIrPoints = numDataSamples;
	hData.mIrSize = 0;
	hData.mIrCount = 0;
	hData.mEvCount = 0;
	hData.mRadius = 0;
	hData.mDistance = 0;	

	hData.mFftSize = 1;
	while(hData.mFftSize < numDataSamples*4)
		hData.mFftSize *= 2;
        hData.mIrSize = 1 + (hData.mFftSize / 2);

	if(fftSize>hData.mFftSize) {
                hData.mFftSize = fftSize;
                hData.mIrSize = 1 + (fftSize / 2);
	}

	

// TODO: Support different sampling rate per measurement, support default sampling rate of 48000
	file.GetValues(values, "Data.SamplingRate");
	if(!verifyVariable(file,"Data.SamplingRate","double","I",1)) {
		fprintf(stderr, "Error: Expecting sampling rate\n");
            	return 0;
	}
	hData.mIrRate = values[0];

	file.GetValues(values, "ReceiverPosition");
	if(!verifyVariable(file,"ReceiverPosition","double","R,C,I",6,NULL,ATTR_TYPE_CARTESIAN) ||
		values[0] != 0 || values[1] >= 0 || values[3] !=0 ||
		values[3] != 0 || values[4] != -values[1] || values[5] !=0) {
		fprintf(stderr, "Error: Expecting proper ReceiverPosition\n");
            	return 0;
	}
	hData.mRadius = values[4];

	// read source positions
	file.GetValues(values, "SourcePosition");
	if(verifyVariable(file,"SourcePosition","double","M,C", numMeasurements*3, NULL,  ATTR_TYPE_CARTESIAN)) {
		convertCartesianToSpherical(values);
	}
	else if(!verifyVariable(file,"SourcePosition","double","M,C", numMeasurements*3, NULL, ATTR_TYPE_SPHERICAL)) {
		fprintf(stderr, "Error: Expecting SourcePosition\n");
            	return 0;
	}

	convertSOFAtoOpenAL(values);

	hData.mDistance = getMeanDistance(values);
	output << "Mean Distance " << hData.mDistance << std::endl;
	sortSourcePositions(values);
	countElevationsAndAzimuths(values, hData);
        SOFA_ASSERT( hData.mIrCount == numMeasurements  );

	// read FIR filters
	std::vector< double > data;
	file.GetValues(data, "Data.IR");
	if(!verifyVariable(file,"Data.IR","double","M,R,N", numMeasurements*2*numDataSamples)) {
		fprintf(stderr, "Error: Expecting proper Data.IR\n");
            	return 0;

	}

	hData.mStereo = 1;
	hData.mHrirs = CreateArray(hData.mIrCount * hData.mIrSize * 2);
	readData(values,data,hData);

	// TODO consider readed time delays from sofa file
    	hData.mHrtds = CreateArray(hData.mIrCount * 2);
	for(size_t ei=0;ei<hData.mEvCount;ei++) {
		for(size_t ai=0;ai<hData.mAzCount[ei];ai++) {
			AverageHrirOnset(hData.mHrirs + (hData.mEvOffset[ei] + ai) * hData.mIrCount, 1, ei, ai, &hData);
			AverageHrirMagnitude(hData.mHrirs + (hData.mEvOffset[ei] + ai) * hData.mIrCount, 1, ei, ai, &hData);
		}
	}

	// verify OpenAL parameters
	if(hData.mIrRate < MIN_RATE || hData.mIrRate > MAX_RATE) {
		fprintf(stderr, "Error: Sampling rate is not within proper limits: %u vs %d to %d\n",hData.mIrRate, MIN_RATE, MAX_RATE);
            	return 0;
	}

	if(hData.mIrPoints < MIN_POINTS || hData.mIrPoints > MAX_POINTS) {
		fprintf(stderr, "Error: FIR filter length is not within proper limits: %u vs %d to %d\n",hData.mIrPoints, MIN_POINTS, MAX_POINTS);
            	return 0;
	}

	if(hData.mEvCount < MIN_EV_COUNT || hData.mEvCount > MAX_EV_COUNT) {
		fprintf(stderr, "Error: Number of elevations is not within proper limits: %u vs %d to %d\n",hData.mEvCount, MIN_EV_COUNT, MAX_EV_COUNT);
            	return 0;
	}

	for(size_t i = 0; i < hData.mEvCount; i++) {
		if(hData.mAzCount[i] < MIN_AZ_COUNT || hData.mAzCount[i] > MAX_AZ_COUNT) {
			fprintf(stderr, "Error: Number of azimuths is not within proper limits: %u at %lu vs %d to %d\n", hData.mAzCount[i], i, MIN_AZ_COUNT, MAX_AZ_COUNT);
	            	return 0;
		}
	}

	if(hData.mRadius < MIN_RADIUS || hData.mRadius > MAX_RADIUS) {
		fprintf(stderr, "Error: Radius is not within proper limits: %f vs %f to %f\n",hData.mRadius, MIN_RADIUS, MAX_RADIUS);
            	return 0;
	}

	if(hData.mDistance < MIN_DISTANCE || hData.mDistance > MAX_DISTANCE) {
		fprintf(stderr, "Error: Distance is not within proper limits: %f vs %f to %f\n",hData.mDistance, MIN_DISTANCE, MAX_DISTANCE);
            	return 0;
	}


	fprintf(stderr,"\n\nall done\n\n");


    	return hrtfPostProcessing(outRate, equalize, surface, limit, truncSize, HM_DATASET, radius, outFormat, outName, &hData);
}

