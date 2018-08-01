#include "gdal_dataset_importer.h"
#include "gdal_timesnap.h"
#include "configuration.h"
#include <ogr_spatialref.h>
#include <boost/filesystem.hpp>

const std::string GDALDatasetImporter::placeholder = "%%%TIME_STRING%%%";

GDALDataset* GDALDatasetImporter::openGDALDataset(std::string file_name){

	GDAL::init();

	GDALDataset *dataset = (GDALDataset *) GDALOpen(file_name.c_str(), GA_ReadOnly);

	if (dataset == NULL)
		throw ImporterException(concat("GDAL Source: Could not open dataset ", file_name));	

	return dataset;
}

//write all the values to Json and save it to disk
void GDALDatasetImporter::importDataset(std::string dataset_name, 
									std::string dataset_filename_with_placeholder, 
									std::string dataset_file_path, 
									std::string time_format, 
									std::string time_start, 
									std::string time_unit, 
									std::string interval_value, 
									std::string citation, 
									std::string license, 
									std::string uri,
							  		std::string measurement,
							  		std::string unit,
							  		std::string interpolation)
{
		
	size_t placeholderPos =	dataset_filename_with_placeholder.find(placeholder);

	boost::filesystem::path dataset_json_path(Configuration::get<std::string>("gdalsource.datasetpath"));

	if(placeholderPos == std::string::npos){
		throw ImporterException("GDALDatasetImporter: Date placeholder " + placeholder + " not found in dataset filename " + dataset_filename_with_placeholder);
	}

	TimeUnit tu;
	int interval = std::stoi(interval_value);

	if(GDALTimesnap::string_to_TimeUnit.find(time_unit) == GDALTimesnap::string_to_TimeUnit.end()){		
		throw ImporterException("GDALDatasetImporter: " + time_unit + " is not a valid time unit (Year, Month, Day, Hour, Minute or Second)");
	} else {
		tu = GDALTimesnap::createTimeUnit(time_unit);
	}

	//parse time_start with time_format to check if its valid, else parse throws an exception
	auto timeParser = TimeParser::createCustom(time_format); 
	timeParser->parse(time_start);

	//create Json 
	Json::Value timeIntervalJson(Json::ValueType::objectValue);
	timeIntervalJson["unit"] 	= time_unit;
	timeIntervalJson["value"] 	= interval;

	Json::Value datasetJson(Json::ValueType::objectValue);
	datasetJson["dataset_name"] = dataset_name;
	datasetJson["path"] 		= dataset_file_path;
	datasetJson["file_name"]	= dataset_filename_with_placeholder;
	datasetJson["time_format"] 	= time_format;
	datasetJson["time_start"] 	= time_start;
	datasetJson["time_interval"]= timeIntervalJson;

	std::string replaced_file_name = dataset_filename_with_placeholder.replace(placeholderPos, placeholder.length(), time_start);
	boost::filesystem::path file_to_open(dataset_file_path);
	file_to_open /= replaced_file_name;
	GDALDataset *dataset = openGDALDataset(file_to_open.string());
	
	datasetJson["coords"]		= readCoords(dataset);
	datasetJson["channels"]		= readChannels(dataset, measurement, unit, interpolation);

	Json::Value provenanceJson(Json::ValueType::objectValue);
	provenanceJson["citation"] 	= citation;
	provenanceJson["license"] 	= license;
	provenanceJson["uri"] 		= uri;
	datasetJson["provenance"] 	= provenanceJson;

	GDALClose(dataset);	

	//save Json to disk
	Json::StyledWriter writer;	
	std::ofstream file;
	dataset_json_path /= dataset_name + ".json";
	file.open(dataset_json_path.string());
	file << writer.write(datasetJson);		
	file.close();

}

//read crsId, size, scale, origin from actual GDALDatset
Json::Value GDALDatasetImporter::readCoords(GDALDataset *dataset){
	Json::Value coordsJson(Json::ValueType::objectValue);

	double adfGeoTransform[6];

	if( dataset->GetGeoTransform( adfGeoTransform ) != CE_None ) {
		GDALClose(dataset);
		throw ImporterException("GDAL Source: No GeoTransform information in raster");
	}

	Json::Value originJson(Json::ValueType::arrayValue);
	originJson.append(adfGeoTransform[0]);
	originJson.append(adfGeoTransform[3]);

	Json::Value scaleJson(Json::ValueType::arrayValue);
	scaleJson.append(adfGeoTransform[1]);
	scaleJson.append(adfGeoTransform[5]);

	Json::Value sizeJson(Json::ValueType::arrayValue);
	sizeJson.append(dataset->GetRasterXSize());
	sizeJson.append(dataset->GetRasterYSize());

	coordsJson["crs"] = CrsId::from_wkt(dataset->GetProjectionRef()).to_string();
	coordsJson["origin"] = originJson;
	coordsJson["scale"] = scaleJson;
	coordsJson["size"] = sizeJson;

	return coordsJson;
}

//read channel values from GDALDataset, some from given parameters
Json::Value GDALDatasetImporter::readChannels(GDALDataset *dataset, std::string measurement, std::string unit, std::string interpolation){
	Json::Value channelsJson(Json::ValueType::arrayValue);
	
	if(measurement == "")
		measurement = "unknown";
	if(unit == "")
		unit = "unknown";
	if(interpolation == "")
		interpolation = "unknown";

	int channelCount = dataset->GetRasterCount();

	for(int i = 1 ; i <= channelCount; i++){
		Json::Value channelJson(Json::ValueType::objectValue);
		//GDALRasterBand is not to be freed, is owned by GDALDataset that will be closed later
		GDALRasterBand *raster = dataset->GetRasterBand(i);

		int success;
		Json::Value unitJson;
		
		unitJson["interpolation"] 	= interpolation;
		unitJson["measurement"] 	= measurement;
		unitJson["unit"] 			= unit;
		
		GDALDataType dataType 		= raster->GetRasterDataType();
		channelJson["datatype"] 	= dataTypeToString(dataType);
		
		double minimum 				= raster->GetMinimum(&success);
		if(!success)
			minimum 				= 0.0;
		unitJson["min"] 			= minimum;

		double maximum 				= raster->GetMaximum(&success);
		if(!success)
			maximum = 254.0;
		unitJson["max"] 			= maximum;		

		double nodata = raster->GetNoDataValue(&success);
		if(!success)
			nodata = 255.0;
		channelJson["nodata"] 		= nodata;

		channelJson["unit"]			= unitJson;

		channelsJson.append(channelJson);
	}

	return channelsJson;
}

//GDALDataType to string
std::string GDALDatasetImporter::dataTypeToString(GDALDataType type){
	switch(type){
		case GDT_Byte:
			return "Byte";
		case GDT_UInt16:
			return "UInt16";
		case GDT_Int16:
			return "Int16";
		case GDT_UInt32:
			return "UInt32";
		case GDT_Int32:
			return "Int32";
		case GDT_Float32:
			return "Float32";
		case GDT_Float64:
			return "Float64";
		case GDT_CInt16:
			return "CInt16";
		case GDT_CInt32:
			return "CInt32";
		case GDT_CFloat32:
			return "CFloat32";
		case GDT_CFloat64:
			return "CFloat64";
		default:
		case GDT_Unknown:
			return "Unknown";
	}
}