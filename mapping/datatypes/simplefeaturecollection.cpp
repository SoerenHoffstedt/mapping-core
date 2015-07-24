
#include "simplefeaturecollection.h"
#include "util/exceptions.h"
#include "util/binarystream.h"
#include "util/hash.h"
#include "util/make_unique.h"

#include <sstream>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <limits>
#include "boost/date_time/posix_time/posix_time.hpp"

Coordinate::Coordinate(BinaryStream &stream) {
	stream.read(&x);
	stream.read(&y);
}
void Coordinate::toStream(BinaryStream &stream) {
	stream.write(x);
	stream.write(y);
}

/**
 * Timestamps
 */
bool SimpleFeatureCollection::hasTime() const {
	return time_start.size() == getFeatureCount();
}

void SimpleFeatureCollection::addDefaultTimestamps() {
	addDefaultTimestamps(std::numeric_limits<double>::min(), std::numeric_limits<double>::max());
}

void SimpleFeatureCollection::addDefaultTimestamps(double min, double max) {
	if (hasTime())
		return;
	auto fcount = getFeatureCount();
	time_start.empty();
	time_start.resize(fcount, min);
	time_end.empty();
	time_end.resize(fcount, max);
}


/**
 * Global Metadata
 */
const std::string &SimpleFeatureCollection::getGlobalMDString(const std::string &key) const {
	return global_md_string.get(key);
}

double SimpleFeatureCollection::getGlobalMDValue(const std::string &key) const {
	return global_md_value.get(key);
}

DirectMetadata<std::string>* SimpleFeatureCollection::getGlobalMDStringIterator() {
	return &global_md_string;
}

DirectMetadata<double>* SimpleFeatureCollection::getGlobalMDValueIterator() {
	return &global_md_value;
}

std::vector<std::string> SimpleFeatureCollection::getGlobalMDValueKeys() const {
	std::vector<std::string> keys;
	for (auto keyValue : global_md_value) {
		keys.push_back(keyValue.first);
	}
	return keys;
}

std::vector<std::string> SimpleFeatureCollection::getGlobalMDStringKeys() const {
	std::vector<std::string> keys;
	for (auto keyValue : global_md_string) {
		keys.push_back(keyValue.first);
	}
	return keys;
}

void SimpleFeatureCollection::setGlobalMDString(const std::string &key, const std::string &value) {
	global_md_string.set(key, value);
}

void SimpleFeatureCollection::setGlobalMDValue(const std::string &key, double value) {
	global_md_value.set(key, value);
}

std::string SimpleFeatureCollection::toWKT() const {
	std::ostringstream wkt;

	wkt << "GEOMETRYCOLLECTION(";

	for(size_t i = 0; i < getFeatureCount(); ++i){
		featureToWKT(i, wkt);
		wkt << ",";
	}
	wkt.seekp(((long) wkt.tellp()) - 1); // delete last ,

	wkt << ")";

	return wkt.str();
}

std::string SimpleFeatureCollection::featureToWKT(size_t featureIndex) const{
	std::ostringstream wkt;
	featureToWKT(featureIndex, wkt);
	return wkt.str();
}

std::string SimpleFeatureCollection::toARFF(std::string layerName) const {
	std::ostringstream arff;

	//TODO: maybe take name of layer as relation name, but this is not accessible here
	arff << "@RELATION " << layerName << std::endl << std::endl;

	arff << "@ATTRIBUTE wkt STRING" << std::endl;

	if (hasTime()){
		arff << "@ATTRIBUTE time_start DATE" << std::endl;
		arff << "@ATTRIBUTE time_end DATE" << std::endl;
	}

	auto string_keys = local_md_string.getKeys();
	auto value_keys = local_md_value.getKeys();

	for(auto &key : string_keys) {
		arff << "@ATTRIBUTE" << " " << key << " " << "STRING" << std::endl;
	}
	for(auto &key : value_keys) {
		arff << "@ATTRIBUTE" << " " << key << " " << "NUMERIC" << std::endl;
	}

	arff << std::endl;
	arff << "@DATA" << std::endl;

	for (size_t featureIndex = 0; featureIndex < getFeatureCount(); ++featureIndex) {
		arff << "\"" << featureToWKT(featureIndex) << "\"";
		if (hasTime()){
			arff << "," << "\"" << to_iso_extended_string(boost::posix_time::from_time_t(time_start[featureIndex])) << "\"" << ","
					 << "\"" << to_iso_extended_string(boost::posix_time::from_time_t(time_end[featureIndex])) << "\"";
		}

		//TODO: handle missing metadata values
		for(auto &key : string_keys) {
			arff << ",\"" << local_md_string.get(featureIndex, key) << "\"";
		}
		for(auto &key : value_keys) {
			arff << "," << local_md_value.get(featureIndex, key);
		}
		arff << std::endl;
	}

	return arff.str();
}
