#ifndef UTIL_OGR_SOURCE_UTIL_H
#define UTIL_OGR_SOURCE_UTIL_H

#include <gdal/ogrsf_frmts.h>
#include "util/exceptions.h"
#include "util/enumconverter.h"
#include "util/timeparser.h"
#include "datatypes/pointcollection.h"
#include "datatypes/linecollection.h"
#include "datatypes/polygoncollection.h"
#include "operators/provenance.h"
#include "operators/querytools.h"
#include <istream>
#include <vector>
#include <json/json.h>
#include <unordered_map>
#include <functional>


/*
* Additionally defines a few enums (including string representations) for parameter parsing.
* These are copied from CSV Source because that functionality is supposed to be replaced by this operator.
*/

enum class TimeSpecification {
	NONE,
	START,
	START_END,
	START_DURATION
};

const std::vector< std::pair<TimeSpecification, std::string> > TimeSpecificationMap = {
	std::make_pair(TimeSpecification::NONE, "none"),
	std::make_pair(TimeSpecification::START, "start"),
	std::make_pair(TimeSpecification::START_END, "start+end"),
	std::make_pair(TimeSpecification::START_DURATION, "start+duration")
};

static EnumConverter<TimeSpecification> TimeSpecificationConverter(TimeSpecificationMap);

enum class ErrorHandling {
	ABORT,
	SKIP,
	KEEP
};

const std::vector< std::pair<ErrorHandling, std::string> > ErrorHandlingMap {
	std::make_pair(ErrorHandling::ABORT, "abort"),
	std::make_pair(ErrorHandling::SKIP, "skip"),
	std::make_pair(ErrorHandling::KEEP, "keep")
};

enum class AttributeType
{
	TEXTUAL,
	NUMERIC,
	TIME
};

static EnumConverter<ErrorHandling>ErrorHandlingConverter(ErrorHandlingMap);

/**
 * Class for reading OGR feature collections and creating SimpleFeatureCollections.
 *
 */

class OGRSourceUtil 
{
public:
	OGRSourceUtil(Json::Value &params, Provenance provenance);
	~OGRSourceUtil();
	std::unique_ptr<PointCollection> getPointCollection(const QueryRectangle &rect, const QueryTools &tools);
	std::unique_ptr<LineCollection> getLineCollection(const QueryRectangle &rect, const QueryTools &tools);
	std::unique_ptr<PolygonCollection> getPolygonCollection(const QueryRectangle &rect, const QueryTools &tools);
	void writeSemanticParameters(std::ostringstream& stream);
	void getProvenance(ProvenanceCollection &pc);

	/**
	 * Opens a GDALDataset and returns the pointer to it. This pointer has to be freed with GDALClose(GDALDataset*) when done.
	 * @param params
	 * @return
	 */
    static GDALDataset* openGDALDataset(Json::Value &params);

private:
	GDALDataset *dataset;
	Provenance provenance;
	bool hasDefault;
	Json::Value params;
	std::vector<std::string> attributeNames;
	std::unordered_map<std::string, AttributeType> wantedAttributes;
	std::string time1Name;
	std::string time2Name;
	int time1Index;
	int time2Index;
	double timeDuration;
	std::unique_ptr<TimeParser> time1Parser;
	std::unique_ptr<TimeParser> time2Parser;
	ErrorHandling errorHandling;
	TimeSpecification timeSpecification;

	void close();
	void readAnyCollection(const QueryRectangle &rect, SimpleFeatureCollection *collection, std::function<bool(OGRGeometry *, OGRFeature *, int)> addFeature);
	void readLineStringToLineCollection(const OGRLineString *line, std::unique_ptr<LineCollection> &collection);
	void readRingToPolygonCollection(const OGRLinearRing *ring, std::unique_ptr<PolygonCollection> &collection);
	void createAttributeArrays(OGRFeatureDefn *attributeDefn, AttributeArrays &attributeArrays);
	bool readAttributesIntoCollection(AttributeArrays &attributeArrays, OGRFeatureDefn *attributeDefn, OGRFeature *feature, int featureIndex);
	void initTimeReading(OGRFeatureDefn *attributeDefn);
	bool readTimeIntoCollection(const QueryRectangle &rect, OGRFeature *feature, std::vector<TimeInterval> &time);
};

bool hasSuffix(const std::string &str, const std::string &suffix);

#endif
