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
* Enums (plus string representations) for parameter parsing.
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
 * Doing the work for OGRSource and OGRRawSource.
 * All the needed parameters for params are explained in ogr_raw_source.h
 */
class OGRSourceUtil
{
public:
	OGRSourceUtil(Json::Value &params, Provenance provenance);
	~OGRSourceUtil() = default;
	std::unique_ptr<PointCollection> getPointCollection(const QueryRectangle &rect, const QueryTools &tools);
	std::unique_ptr<LineCollection> getLineCollection(const QueryRectangle &rect, const QueryTools &tools);
	std::unique_ptr<PolygonCollection> getPolygonCollection(const QueryRectangle &rect, const QueryTools &tools);
	void getProvenance(ProvenanceCollection &pc);
    Json::Value& getParameters();

    /**
     * Writes all parameters into the stream, suitable for OGR Raw Source.
     * OGR Source does not provide all the parameters in the query, so it's not used there.
     */
    void writeSemanticParametersRaw(std::ostringstream& stream);

    /**
     * Opens a GDALDataset with GDAL API for vector files.
     * @param params JSON parameters from query, needs to provide filename. columns with x (,y) members for csv files.
     * @return Pointer to the opened GDALDataset. The pointer needs to be freed with GDALClose. Returns NULL if opening failed.
     */
    static GDALDataset* openGDALDataset(Json::Value &params);

    /**
     * returns if str ends with substring suffix.
     */
    static bool hasSuffix(const std::string &str, const std::string &suffix);

private:
	std::unique_ptr<GDALDataset, std::function<void(GDALDataset *)>> dataset;
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

	void readAnyCollection(const QueryRectangle &rect, SimpleFeatureCollection *collection, std::function<bool(OGRGeometry *)> addFeature);
	void readLineStringToLineCollection(const OGRLineString *line, std::unique_ptr<LineCollection> &collection);
	void readRingToPolygonCollection(const OGRLinearRing *ring, std::unique_ptr<PolygonCollection> &collection);
	void createAttributeArrays(OGRFeatureDefn *attributeDefn, AttributeArrays &attributeArrays);
	bool readAttributesIntoCollection(AttributeArrays &attributeArrays, OGRFeatureDefn *attributeDefn, OGRFeature *feature, int featureIndex);
	void initTimeReading(OGRFeatureDefn *attributeDefn);
	bool readTimeIntoCollection(const QueryRectangle &rect, OGRFeature *feature, std::vector<TimeInterval> &time);
};

#endif
