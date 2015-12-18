#include "datatypes/pointcollection.h"

#include "util/binarystream.h"
#include "util/hash.h"
#include "util/make_unique.h"

#include <sstream>
#include <iomanip>
#include <iostream>
#include <cmath>



std::unique_ptr<PointCollection> PointCollection::clone() const {
	auto copy = make_unique<PointCollection>(stref);
	copy->global_attributes = global_attributes;
	copy->feature_attributes = feature_attributes.clone();
	copy->coordinates = coordinates;
	copy->time_start = time_start;
	copy->time_end = time_end;
	copy->start_feature = start_feature;
	return copy;
}


template<typename T>
std::unique_ptr<PointCollection> filter(const PointCollection &in, const std::vector<T> &keep, size_t kept_count) {
	size_t count = in.getFeatureCount();
	if (keep.size() != count) {
		throw ArgumentException(concat("PointCollection::filter(): size of filter does not match (", keep.size(), " != ",count, ")"));
	}

	auto out = make_unique<PointCollection>(in.stref);
	out->start_feature.reserve(kept_count);

	// copy global attributes
	out->global_attributes = in.global_attributes;

	// copy features
	for (auto feature : in) {
		if (keep[feature]) {
			//copy coordinates
			for (auto & c : feature) {
				out->addCoordinate(c.x, c.y);
			}
			out->finishFeature();
		}
	}

	// copy feature attributes
	out->feature_attributes = in.feature_attributes.filter(keep, kept_count);

	// copy time arrays
	if (in.hasTime()) {
		out->time_start.reserve(kept_count);
		out->time_end.reserve(kept_count);
		for (size_t idx = 0; idx < count; idx++) {
			if (keep[idx]) {
				out->time_start.push_back(in.time_start[idx]);
				out->time_end.push_back(in.time_end[idx]);
			}
		}
	}

	return out;
}

std::unique_ptr<PointCollection> PointCollection::filter(const std::vector<bool> &keep) const {
	auto kept_count = calculate_kept_count(keep);
	return ::filter<bool>(*this, keep, kept_count);
}

std::unique_ptr<PointCollection> PointCollection::filter(const std::vector<char> &keep) const {
	auto kept_count = calculate_kept_count(keep);
	return ::filter<char>(*this, keep, kept_count);
}

void PointCollection::filterInPlace(const std::vector<bool> &keep) {
	auto kept_count = calculate_kept_count(keep);
	if (kept_count == getFeatureCount())
		return;
	auto other = ::filter<bool>(*this, keep, kept_count);
	*this = std::move(*other);
}

void PointCollection::filterInPlace(const std::vector<char> &keep) {
	auto kept_count = calculate_kept_count(keep);
	if (kept_count == getFeatureCount())
		return;
	auto other = ::filter<char>(*this, keep, kept_count);
	*this = std::move(*other);
}

std::unique_ptr<PointCollection> PointCollection::filterBySpatioTemporalReferenceIntersection(const SpatioTemporalReference& stref) const{
	auto keep = getKeepVectorForFilterBySpatioTemporalReferenceIntersection(stref);
	auto filtered = filter(keep);
	filtered->replaceSTRef(stref);
	return filtered;
}

void PointCollection::filterBySpatioTemporalReferenceIntersectionInPlace(const SpatioTemporalReference& stref) {
	auto keep = getKeepVectorForFilterBySpatioTemporalReferenceIntersection(stref);
	replaceSTRef(stref);
	filterInPlace(keep);
}


bool PointCollection::featureIntersectsRectangle(size_t featureIndex, double x1, double y1, double x2, double y2) const{
	for(auto& c : getFeatureReference(featureIndex)){
		if(c.x >= x1 && c.x <= x2 && c.y >= y1 && c.y <= y2){
			return true;
		}
	}
	return false;
}


PointCollection::PointCollection(BinaryStream &stream) : SimpleFeatureCollection(stream) {
	bool hasTime;
	stream.read(&hasTime);

	size_t featureCount;
	stream.read(&featureCount);
	start_feature.reserve(featureCount);

	size_t coordinateCount;
	stream.read(&coordinateCount);
	coordinates.reserve(coordinateCount);

	global_attributes.fromStream(stream);
	feature_attributes.fromStream(stream);

	if (hasTime) {
		time_start.reserve(featureCount);
		time_end.reserve(featureCount);
		double time;
		for (size_t i = 0; i < featureCount; i++) {
			stream.read(&time);
			time_start.push_back(time);
		}
		for (size_t i = 0; i < featureCount; i++) {
			stream.read(&time);
			time_end.push_back(time);
		}
	}

	uint32_t offset;
	for (size_t i = 0; i < featureCount; i++) {
		stream.read(&offset);
		start_feature.push_back(offset);
	}

	for (size_t i = 0; i < coordinateCount; i++) {
		coordinates.push_back(Coordinate(stream));
	}
}

void PointCollection::toStream(BinaryStream &stream) const {
	stream.write(stref);
	stream.write(hasTime());

	size_t featureCount = start_feature.size();
	stream.write(featureCount);
	size_t coordinateCount = coordinates.size();
	stream.write(coordinateCount);

	stream.write(global_attributes);
	stream.write(feature_attributes);

	if (hasTime()) {
		for (size_t i = 0; i < featureCount; i++) {
			stream.write(time_start[i]);
		}
		for (size_t i = 0; i < featureCount; i++) {
			stream.write(time_end[i]);
		}
	}

	for (size_t i = 0; i < featureCount; i++) {
		stream.write(start_feature[i]);
	}

	for (size_t i = 0; i < coordinateCount; i++) {
		coordinates[i].toStream(stream);
	}
}


void PointCollection::addCoordinate(double x, double y) {
	coordinates.push_back(Coordinate(x, y));
}

size_t PointCollection::finishFeature(){
	if(start_feature.back() >= coordinates.size()){
		throw FeatureException("Tried to finish feature with 0 coordinates");
	}

	start_feature.push_back(coordinates.size());
	return start_feature.size() -2;
}

size_t PointCollection::addSinglePointFeature(Coordinate coordinate){
	coordinates.push_back(coordinate);
	start_feature.push_back(coordinates.size());
	return start_feature.size() -2;
}


/**
 * Export
 */
#if 0
// http://www.gdal.org/ogr_apitut.html
#include "ogrsf_frmts.h"

void gdal_init(); // implemented in raster/import_gdal.cpp

void PointCollection::toOGR(const char *driver = "ESRI Shapefile") {
	gdal_init();

    GDALDriver *poDriver = GetGDALDriverManager()->GetDriverByName(pszDriverName );
    if (poDriver == nullptr)
    	throw ExporterException("OGR driver not available");

    GDALDataset *poDS = poDriver->Create( "point_out.shp", 0, 0, 0, GDT_Unknown, NULL );
    if (poDS == nullptr) {
    	// TODO: free driver?
    	throw ExporterException("Dataset creation failed");
    }

    OGRLayer *poLayer = poDS->CreateLayer( "point_out", NULL, wkbPoint, NULL );
    if (poLayer == nullptr) {
    	// TODO: free driver and dataset?
    	throw ExporterException("Layer Creation failed");
    }

    // No attributes

    // Loop over all points
	for (const Coordinate &p : collection) {
		double x = p.x, y = p.y;

		OGRFeature *poFeature = OGRFeature::CreateFeature(poLayer->GetLayerDefn());
		//poFeature->SetField( "Name", szName );
		OGRPoint pt;
		pt.setX(p.x);
		pt.setY(p.y);
		poFeature->SetGeometry(&pt);

        if (poLayer->CreateFeature( poFeature ) != OGRERR_NONE) {
        	// TODO: free stuf..
        	throw ExporterException("CreateFeature failed");
        }

        OGRFeature::DestroyFeature( poFeature );
	}
	GDALClose(poDS);
}
#endif


void PointCollection::featureToGeoJSONGeometry(size_t featureIndex, std::ostringstream& json) const {
	auto feature = getFeatureReference(featureIndex);

	if(feature.size() == 1)
		json << "{\"type\":\"Point\",\"coordinates\":";
	else
		json << "{\"type\":\"MultiPoint\",\"coordinates\":[";

	for(auto& c : feature){
		json << "[" << c.x << "," << c.y << "],";
	}
	json.seekp(((long)json.tellp()) - 1); //delete last ,

	if(feature.size() > 1)
		json << "]";
	json << "}";
}

//TODO: include global metadata?
std::string PointCollection::toCSV() const {
	std::ostringstream csv;
	csv << std::fixed; // std::setprecision(4);

	auto string_keys = feature_attributes.getTextualKeys();
	auto value_keys = feature_attributes.getNumericKeys();

	bool isSimpleCollection = isSimple();

	//header
	if(!isSimpleCollection){
		csv << "feature,";
	}
	csv << "lon" << "," << "lat";
	if (hasTime())
		csv << ",\"time_start\",\"time_end\"";
	for(auto &key : string_keys) {
		csv << ",\"" << key << "\"";
	}
	for(auto &key : value_keys) {
		csv << ",\"" << key << "\"";
	}
	csv << std::endl;

	for (auto feature : *this) {
		for (auto & c : feature) {
			if(!isSimpleCollection)
				csv << (size_t) feature << ",";
			csv << c.x << "," << c.y;

			if (hasTime())
				csv << "," << time_start[feature] << "," << time_end[feature];

			//TODO: handle missing metadata values
			for(auto &key : string_keys) {
				csv << ",\"" << feature_attributes.textual(key).get(feature) << "\"";
			}
			for(auto &key : value_keys) {
				csv << "," << feature_attributes.numeric(key).get(feature);
			}
			csv << std::endl;
		}
	}

	return csv.str();
}

void PointCollection::featureToWKT(size_t featureIndex, std::ostringstream& wkt) const {
	if(featureIndex >= getFeatureCount()){
		throw ArgumentException("featureIndex is greater than featureCount");
	}

	auto feature = getFeatureReference(featureIndex);

	if(feature.size() == 1) {
		const Coordinate& coordinate = *feature.begin();
		wkt << "POINT(" << coordinate.x << " " << coordinate.y << ")";
	}
	else {
		wkt << "MULTIPOINT(";

		for(auto& coordinate : feature){
			wkt << "(" << coordinate.x << " " << coordinate.y << "),";
		}
		wkt.seekp(((long) wkt.tellp()) - 1); // delete last ,

		wkt << ")";
	}
}

std::string PointCollection::toARFF(std::string layerName) const {
	std::ostringstream arff;

	//TODO: maybe take name of layer as relation name, but this is not accessible here
	arff << "@RELATION " << layerName << std::endl << std::endl;

	bool isSimpleCollection = isSimple();

	if(!isSimpleCollection){
		arff << "@ATTRIBUTE feature NUMERIC" << std::endl;
	}
	arff << "@ATTRIBUTE longitude NUMERIC" << std::endl;
	arff << "@ATTRIBUTE latitude NUMERIC" << std::endl;

	if (hasTime()){
		arff << "@ATTRIBUTE time_start DATE" << std::endl;
		arff << "@ATTRIBUTE time_end DATE" << std::endl;
	}

	auto string_keys = feature_attributes.getTextualKeys();
	auto value_keys = feature_attributes.getNumericKeys();


	//TODO: handle missing metadata values
	for(auto &key : string_keys) {
		arff << "@ATTRIBUTE" << " " << key << " " << "STRING" << std::endl;
	}
	for(auto &key : value_keys) {
		arff << "@ATTRIBUTE" << " " << key << " " << "NUMERIC" << std::endl;
	}

	arff << std::endl;

	arff << "@DATA" << std::endl;

	for (auto feature : *this) {
		for (auto & c : feature) {
			if(!isSimpleCollection)
				arff << (size_t) feature << ",";
			arff << c.x << "," << c.y;

			if (hasTime()){
				arff << "," << "\"" << stref.toIsoString(time_start[feature]) << "\"" << ","
						 << "\"" << stref.toIsoString(time_end[feature]) << "\"";
			}

			for(auto &key : string_keys) {
				arff << ",\"" << feature_attributes.textual(key).get(feature) << "\"";
			}
			for(auto &key : value_keys) {
				arff << "," << feature_attributes.numeric(key).get(feature);
			}
			arff << std::endl;
		}
	}

	return arff.str();
}

std::string PointCollection::hash()const {
	// certainly not the most stable solution, but it has few lines of code..
	std::string csv = toCSV();

	return calculateHash((const unsigned char *) csv.c_str(), (int) csv.length()).asHex();
}

bool PointCollection::isSimple() const {
	return coordinates.size() == getFeatureCount();
}

std::string PointCollection::getAsString(){
	std::ostringstream string;

	string << "points" << std::endl;
	for(auto p = coordinates.begin(); p !=coordinates.end(); ++p){
		string << (*p).x << "," << (*p).y << ' ';
	}

	string << std::endl;
	string << "features" << std::endl;
	for(auto p = start_feature.begin(); p != start_feature.end(); ++p){
		string << *p << ' ';
	}

	return string.str();
}

SpatialReference PointCollection::getFeatureMBR(size_t featureIndex) const{
	return getFeatureReference(featureIndex).getMBR();
}

void PointCollection::validateSpecifics() const {
	if(start_feature.back() != coordinates.size())
		throw FeatureException("Feature not finished");
}
