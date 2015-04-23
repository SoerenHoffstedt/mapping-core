#include "operators/operator.h"
#include "util/make_unique.h"

#include <string>
#include <json/json.h>
#include <limits>
#include <cmath>
#include "datatypes/pointcollection.h"

class PointsFilterByRangeOperator: public GenericOperator {
	public:
		PointsFilterByRangeOperator(int sourcecounts[], GenericOperator *sources[],	Json::Value &params);
		virtual ~PointsFilterByRangeOperator();

		virtual std::unique_ptr<PointCollection> getPointCollection(const QueryRectangle &rect, QueryProfiler &profiler);

	protected:
		void writeSemanticParameters(std::ostringstream& stream);

	private:
		std::string name;
		bool includeNoData;
		double rangeMin, rangeMax;
	};

PointsFilterByRangeOperator::PointsFilterByRangeOperator(int sourcecounts[], GenericOperator *sources[], Json::Value &params) : GenericOperator(sourcecounts, sources) {
	assumeSources(1);

	name = params.get("name", "raster").asString();
	includeNoData = params.get("includeNoData", false).asBool();
	rangeMin = params.get("rangeMin", std::numeric_limits<double>::min()).asDouble();
	rangeMax = params.get("rangeMax", std::numeric_limits<double>::max()).asDouble();
}

PointsFilterByRangeOperator::~PointsFilterByRangeOperator() {
}
REGISTER_OPERATOR(PointsFilterByRangeOperator, "points_filter_by_range");

void PointsFilterByRangeOperator::writeSemanticParameters(std::ostringstream& stream) {
	stream << "\"attributeName\":\"" << name << "\","
			<< "\"includeNoData\":" << includeNoData
			<< "\"rangeMin\"" << rangeMin
			<< "\"rangeMax\"" << rangeMax;
}

std::unique_ptr<PointCollection> PointsFilterByRangeOperator::getPointCollection(const QueryRectangle &rect, QueryProfiler &profiler) {
	auto points = getPointCollectionFromSource(0, rect, profiler);

	size_t count = points->getFeatureCount();
	std::vector<bool> keep(count, false);

	for (size_t featureIndex=0;featureIndex<count;featureIndex++) {
		double value = points->local_md_value.get(featureIndex, name);
		bool copy = false;

		if (std::isnan(value)) {
			copy = includeNoData;
		}
		else {
			copy = (value >= rangeMin && value <= rangeMax);
		}

		keep[featureIndex] = copy;
	}

	return points->filter(keep);
}
