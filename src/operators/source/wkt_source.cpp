#include "operators/operator.h"
#include "datatypes/polygoncollection.h"
#include "datatypes/simplefeaturecollections/wkbutil.h"
#include "util/exceptions.h"
#include <json/json.h>

/**
 * Operator that reads features in Well-Known-Text
 *
 * Parameters:
 * - wkt
 * - type
 *   - "points"
 *   - "lines"
 *   - "polygons"
 */
class WKTSourceOperator : public GenericOperator {
	public:
	WKTSourceOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params) : GenericOperator(sourcecounts, sources), params(params) {
			assumeSources(0);
			wkt = params.get("wkt", "").asString();
			type = params.get("type", "").asString();

			if(type != "points" && type != "lines" && type != "polygons")
				throw ArgumentException("WKTSource: Invalid type given", MappingExceptionType::PERMANENT);
		}

#ifndef MAPPING_OPERATOR_STUBS

		void setTime(SimpleFeatureCollection& collection) {
			auto& rect = collection.stref;

			if(params.isMember("time")){
				auto timeParam = params.get("time", Json::Value());
				if(timeParam.isArray()) {
					if(timeParam.size() != collection.getFeatureCount())
						throw ArgumentException("WKTSource: time array of invalid size given.");

					for(int i = 0; i < timeParam.size(); ++i){
						double t1, t2;

						auto entry = timeParam[i];
						auto start = entry[0];
						if(start.isNumeric())
							t1 = start.asDouble();
						else
							throw ArgumentException("WKTSource: end time is invalid");

						auto end = entry[1];
						if(end.isNumeric())
							t2 = end.asDouble();
						else
							throw ArgumentException("WKTSource: end time is invalid");

						collection.time.push_back(TimeInterval(t1, t2));
					}
				} else
					throw ArgumentException("WKTSource: time parameter is not an array.");
				collection.validate();
			}
		}

		virtual std::unique_ptr<PointCollection> getPointCollection(const QueryRectangle &rect, const QueryTools &tools){
			if(type != "points")
				throw ArgumentException("WKTSource does not contain points");
			auto points = WKBUtil::readPointCollection(wkt, rect);
			setTime(*points);
			return points->filterBySpatioTemporalReferenceIntersection(rect);
		}

		virtual std::unique_ptr<LineCollection> getLineCollection(const QueryRectangle &rect, const QueryTools &tools){
			if(type != "lines")
				throw ArgumentException("WKTSource does not contain lines");
			auto lines = WKBUtil::readLineCollection(wkt, rect);
			setTime(*lines);
			return lines->filterBySpatioTemporalReferenceIntersection(rect);
		}

		virtual std::unique_ptr<PolygonCollection> getPolygonCollection(const QueryRectangle &rect, const QueryTools &tools){
			if(type != "polygons")
				throw ArgumentException("WKTSource does not contain polygons");
			auto polygons = WKBUtil::readPolygonCollection(wkt, rect);
			setTime(*polygons);
			return polygons->filterBySpatioTemporalReferenceIntersection(rect);
		}
#endif

		void writeSemanticParameters(std::ostringstream& stream) {
			Json::Value json;
			json["type"] = type;
			json["wkt"] = wkt;
			if(params.isMember("time"))
				json["time"] = params["time"];

			Json::FastWriter writer;
			stream << writer.write(json);
		}

		virtual ~WKTSourceOperator(){};

	private:
		std::string wkt;
		std::string type;
		Json::Value params;
};
REGISTER_OPERATOR(WKTSourceOperator, "wkt_source");
