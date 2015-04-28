#include "datatypes/raster.h"
#include "datatypes/raster/raster_priv.h"
#include "datatypes/raster/typejuggling.h"
#include "datatypes/pointcollection.h"
#include "raster/profiler.h"
#include "raster/opencl.h"
#include "operators/operator.h"
#include "util/make_unique.h"

#include <cmath>
#include <json/json.h>
#include <algorithm>
#include <vector>
#include <utility>


class RasterMetaDataToPoints: public GenericOperator {
	public:
		RasterMetaDataToPoints(int sourcecounts[], GenericOperator *sources[], Json::Value &params);
		virtual ~RasterMetaDataToPoints();

		virtual std::unique_ptr<PointCollection> getPointCollection(const QueryRectangle &rect, QueryProfiler &profiler);

	protected:
		void writeSemanticParameters(std::ostringstream& stream);

	private:
		std::vector<std::string> names;
};

RasterMetaDataToPoints::RasterMetaDataToPoints(int sourcecounts[], GenericOperator *sources[], Json::Value &params) : GenericOperator(sourcecounts, sources) {
//	assumeSources(2);

	names.clear();
	auto arr = params["names"];
	if (!arr.isArray())
		throw OperatorException("raster_metadata_to_points: names parameter invalid");

	int len = (int) arr.size();
	names.reserve(len);
	for (int i=0;i<len;i++) {
		names.push_back( arr[i].asString() );
	}
}

RasterMetaDataToPoints::~RasterMetaDataToPoints() {
}
REGISTER_OPERATOR(RasterMetaDataToPoints, "raster_metadata_to_points");

void RasterMetaDataToPoints::writeSemanticParameters(std::ostringstream& stream) {
	stream << "\"parameterNames\":[";
	for(auto& name : names) {
		stream << "\"" << name << "\",";
	}
	stream.seekp(((long) stream.tellp()) - 1); // remove last comma
	stream << "]";
}

template<typename T>
struct PointDataEnhancement {
	static void execute(Raster2D<T>* raster, PointCollection *points, const std::string &name) {
		raster->setRepresentation(GenericRaster::Representation::CPU);

		auto &md_vec = points->local_md_value.getVector(name);

		for (auto &point : points->coordinates) {
			size_t rasterCoordinateX = floor(raster->lcrs.WorldToPixelX(point.x));
			size_t rasterCoordinateY = floor(raster->lcrs.WorldToPixelY(point.y));

			double md = std::numeric_limits<double>::quiet_NaN();
			if (rasterCoordinateX >= 0 && rasterCoordinateY >= 0 &&	rasterCoordinateX < raster->lcrs.size[0] && rasterCoordinateY < raster->lcrs.size[1]) {
				T value = raster->get(rasterCoordinateX, rasterCoordinateY);
				if (!raster->dd.is_no_data(value))
					md = (double) value;
			}
			md_vec.push_back(md);
		}
	}
};

#include "operators/combined/raster_metadata_to_points.cl.h"

static void enhance(PointCollection &points, GenericRaster &raster, const std::string name, QueryProfiler &profiler) {
#ifdef MAPPING_NO_OPENCL
	points.local_md_value.addEmptyVector(name, points.getFeatureCount());
	callUnaryOperatorFunc<PointDataEnhancement>(&raster, &points, name);
#else
	RasterOpenCL::init();

	points.local_md_value.addVector(name, points.getFeatureCount());
	try {
		RasterOpenCL::CLProgram prog;
		prog.setProfiler(profiler);
		prog.addPointCollection(&points);
		prog.addInRaster(&raster);
		prog.compile(operators_combined_raster_metadata_to_points, "add_attribute");
		prog.addPointCollectionPositions(0);
		prog.addPointCollectionAttribute(0, name);
		prog.run();
	}
	catch (cl::Error &e) {
		printf("cl::Error %d: %s\n", e.err(), e.what());
		throw;
	}
#endif
}

std::unique_ptr<PointCollection> RasterMetaDataToPoints::getPointCollection(const QueryRectangle &rect, QueryProfiler &profiler) {
	auto points = getPointCollectionFromSource(0, rect, profiler, FeatureCollectionQM::SINGLE_ELEMENT_FEATURES);

	if (points->hasTime()) {
		// sort by time, iterate over all timestamps, fetch the correct raster, then add the attribute
		// TODO: currently, missing rasters will just throw an exception. Maybe we should add NaN instead?
		using p = std::pair<size_t, double>;
		std::vector< p > temporal_index;
		auto featurecount = points->getFeatureCount();
		temporal_index.reserve(featurecount);
		for (size_t i=0;i<featurecount;i++)
			temporal_index.emplace_back(i, points->time_start[i]);

		std::sort(temporal_index.begin(), temporal_index.end(), [=] (const p &a, const p&b) -> bool {
			return a.second < b.second;
		});

		auto rasters = getRasterSourceCount();
		TemporalReference tref = TemporalReference::unreferenced();
		for (int r=0;r<rasters;r++) {
			auto &attributevector = points->local_md_value.addVector(names.at(r), featurecount);
			// iterate over time
			size_t current_idx = 0;
			while (current_idx < featurecount) {
				QueryRectangle rect2 = rect;
				rect2.timestamp = temporal_index[current_idx].second;
				auto raster = getRasterFromSource(r, rect2, profiler);
				while (current_idx < featurecount && temporal_index[current_idx].second < raster->stref.t2) {
					// load point and add
					auto featureidx = temporal_index[current_idx].first;
					const auto &c = points->coordinates[featureidx];

					auto rasterCoordinateX = raster->WorldToPixelX(c.x);
					auto rasterCoordinateY = raster->WorldToPixelY(c.y);
					if (rasterCoordinateX >= 0 && rasterCoordinateY >= 0 &&	rasterCoordinateX < raster->width && rasterCoordinateY < raster->height) {
						double value = raster->getAsDouble(rasterCoordinateX, rasterCoordinateY);
						if (!raster->dd.is_no_data(value)) {
							printf("Setting %lu to %f from raster %f -> %f\n", featureidx, value, raster->stref.t1, raster->stref.t2);
							attributevector[featureidx] = value;
						}
					}

					current_idx++;
				}
			}
		}
	}
	else {
		auto rasters = getRasterSourceCount();
		TemporalReference tref = TemporalReference::unreferenced();
		for (int r=0;r<rasters;r++) {
			auto raster = getRasterFromSource(r, rect, profiler);
			Profiler::Profiler p("RASTER_METADATA_TO_POINTS_OPERATOR");
			enhance(*points, *raster, names.at(r), profiler);
			if (r == 0)
				tref = raster->stref;
			else
				tref.intersect(raster->stref);
		}
		points->addDefaultTimestamps(tref.t1, tref.t2);
	}
	return points;
}
