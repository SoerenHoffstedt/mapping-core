
#include "raster/raster.h"
#include "raster/typejuggling.h"
#include "raster/profiler.h"
#include "raster/opencl.h"
#include "operators/operator.h"


#include <limits>
#include <memory>
#include <sstream>
#include <json/json.h>
#include <gdal_priv.h>


class MSATRadianceOperator : public GenericOperator {
	public:
		MSATRadianceOperator(int sourcecount, GenericOperator *sources[], Json::Value &params);
		virtual ~MSATRadianceOperator();

		virtual GenericRaster *getRaster(const QueryRectangle &rect);
	private:
};


#include "operators/msat/radiance.cl.h"



MSATRadianceOperator::MSATRadianceOperator(int sourcecount, GenericOperator *sources[], Json::Value &params) : GenericOperator(Type::RASTER, sourcecount, sources) {
	assumeSources(1);
}
MSATRadianceOperator::~MSATRadianceOperator() {
}
REGISTER_OPERATOR(MSATRadianceOperator, "msatradiance");


GenericRaster *MSATRadianceOperator::getRaster(const QueryRectangle &rect) {
	RasterOpenCL::init();
	GenericRaster *raster = sources[0]->getRaster(rect);
	std::unique_ptr<GenericRaster> raster_guard(raster);

	float offset = raster->md_value.get("CalibrationOffset");
	float slope = raster->md_value.get("CalibrationSlope");

	Profiler::Profiler p("CL_MSATRADIANCE_OPERATOR");
	raster->setRepresentation(GenericRaster::OPENCL);

	double newmin = offset + raster->dd.min * slope;
	double newmax = offset + raster->dd.max * slope;

	DataDescription out_dd(GDT_Float32, newmin, newmax); // no no_data //raster->dd.has_no_data, output_no_data);
	if (raster->dd.has_no_data)
		out_dd.addNoData();

	GenericRaster *raster_out = GenericRaster::create(raster->lcrs, out_dd);
	std::unique_ptr<GenericRaster> raster_out_guard(raster_out);

	RasterOpenCL::CLProgram prog;
	prog.addInRaster(raster);
	prog.addOutRaster(raster_out);
	prog.compile(operators_msat_radiance, "radiancekernel");
	prog.addArg(offset);
	prog.addArg(slope);
	prog.run();

	return raster_out_guard.release();
}
