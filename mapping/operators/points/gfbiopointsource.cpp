#include "datatypes/multipointcollection.h"
#include "datatypes/multipolygoncollection.h"

#include "datatypes/simplefeaturecollections/wkbutil.h"

#include "operators/operator.h"
#include "raster/exceptions.h"
#include "util/curl.h"
#include "util/csvparser.h"
#include "util/configuration.h"
#include "util/make_unique.h"

#include <string>
#include <sstream>
#include <geos/geom/GeometryFactory.h>
#include <geos/geom/CoordinateSequence.h>
#include <geos/io/WKBReader.h>
#include <json/json.h>


class GFBioPointSourceOperator : public GenericOperator {
	public:
		GFBioPointSourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params);
		virtual ~GFBioPointSourceOperator();

		virtual std::unique_ptr<MultiPointCollection> getMultiPointCollection(const QueryRectangle &rect, QueryProfiler &profiler);
		virtual std::unique_ptr<MultiPolygonCollection> getMultiPolygonCollection(const QueryRectangle &rect, QueryProfiler &profiler);

	protected:
		void writeSemanticParameters(std::ostringstream& stream);

	private:
		void getStringFromServer(const QueryRectangle& rect, std::stringstream& data, std::string format);
		std::string datasource;
		std::string query;
		cURL curl;
		std::string includeMetadata;
};


GFBioPointSourceOperator::GFBioPointSourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params) : GenericOperator(sourcecounts, sources) {
	assumeSources(0);

	datasource = params.get("datasource", "").asString();
	query = params.get("query", "").asString();
	includeMetadata = params.get("includeMetadata", "false").asString();
}

GFBioPointSourceOperator::~GFBioPointSourceOperator() {
}
REGISTER_OPERATOR(GFBioPointSourceOperator, "gfbiopointsource");

void GFBioPointSourceOperator::writeSemanticParameters(std::ostringstream& stream) {
	stream << "\"datasource\":\"" << datasource << "\","
			<< "\"query\":\"" << query << "\","
			<< "\"includeMetadata\":\"" << includeMetadata << "\"";
}

class GFBioGeometrySourceOperator : public GFBioPointSourceOperator {
	public:
		GFBioGeometrySourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params) : GFBioPointSourceOperator(sourcecounts, sources, params) {};
};
REGISTER_OPERATOR(GFBioGeometrySourceOperator, "gfbiogeometrysource");



std::unique_ptr<MultiPointCollection> GFBioPointSourceOperator::getMultiPointCollection(const QueryRectangle &rect, QueryProfiler &profiler) {
	auto points_out = std::make_unique<MultiPointCollection>(rect);

	std::stringstream data;
	getStringFromServer(rect, data, "CSV");
	profiler.addIOCost( data.tellp() );

	try {
		CSVParser parser(data, ',', '\n');

		auto header = parser.readHeaders();
		//TODO: distinguish between double and string properties
		for(int i=2; i < header.size(); i++){
			points_out->local_md_string.addVector(header[i]);
		}

		while(true){
			auto tuple = parser.readTuple();
			if (tuple.size() < 1)
				break;

			size_t idx = points_out->addSinglePointFeature(Coordinate(std::stod(tuple[0]),std::stod(tuple[1])));
			//double year = std::atof(csv[3].c_str());

			for(int i=2; i < tuple.size(); i++)
				points_out->local_md_string.set(idx, header[i], tuple[i]);
		}
		//fprintf(stderr, data.str().c_str());
		return points_out;
	}
	catch (const OperatorException &e) {
		data.seekg(0, std::ios_base::beg);
		fprintf(stderr, "CSV:\n%s\n", data.str().c_str());
		throw;
	}

}


// pc12316:81/GFBioJavaWS/Wizzard/fetchDataSource/WKB?datasource=IUCN&query={"globalAttributes":{"speciesName":"Puma concolor"}}
std::unique_ptr<MultiPolygonCollection> GFBioPointSourceOperator::getMultiPolygonCollection(const QueryRectangle &rect, QueryProfiler &profiler) {
	if (rect.epsg != EPSG_LATLON) {
		std::ostringstream msg;
		msg << "GFBioSourceOperator: Shouldn't load points in a projection other than latlon (got " << (int) rect.epsg << ", expected " << (int) EPSG_LATLON << ")";
		throw OperatorException(msg.str());
	}

	std::stringstream data;
	getStringFromServer(rect, data, "WKB");
	profiler.addIOCost( data.tellp() );

	auto multiPolygonCollection = WKBUtil::readMultiPolygonCollection(data);

	return multiPolygonCollection;
}

void GFBioPointSourceOperator::getStringFromServer(const QueryRectangle& rect, std::stringstream& data, std::string format) {
	std::ostringstream url;
	url
		<< Configuration::get("operators.gfbiosource.webserviceurl")
		<< format << "?datasource="
		<< curl.escape(datasource) << "&query=" << curl.escape(query)
		<< "&BBOX=" << std::fixed << rect.x1 << "," << rect.y1 << ","
		<< rect.x2 << "," << rect.y2 << "&includeMetadata=" << includeMetadata;

	curl.setOpt(CURLOPT_PROXY, Configuration::get("operators.gfbiosource.proxy", "").c_str());
	curl.setOpt(CURLOPT_URL, url.str().c_str());
	curl.setOpt(CURLOPT_WRITEFUNCTION, cURL::defaultWriteFunction);
	curl.setOpt(CURLOPT_WRITEDATA, &data);

	curl.perform();
}
