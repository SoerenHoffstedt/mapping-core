
#include "raster/raster.h"
#include "raster/typejuggling.h"
#include "raster/profiler.h"
#include "raster/opencl.h"
#include "operators/operator.h"


#include <memory>
#include <json/json.h>
#include <gdal_priv.h>


class OpenCLOperator : public GenericOperator {
	public:
		OpenCLOperator(int sourcecount, GenericOperator *sources[], Json::Value &params);
		virtual ~OpenCLOperator();

		virtual GenericRaster *getRaster(const QueryRectangle &rect);
};



OpenCLOperator::OpenCLOperator(int sourcecount, GenericOperator *sources[], Json::Value &params) : GenericOperator(Type::RASTER, sourcecount, sources) {
	assumeSources(1);
}
OpenCLOperator::~OpenCLOperator() {
}
REGISTER_OPERATOR(OpenCLOperator, "opencl");


GenericRaster *OpenCLOperator::getRaster(const QueryRectangle &rect) {
	RasterOpenCL::init();
	GenericRaster *raster = sources[0]->getRaster(rect);
	std::unique_ptr<GenericRaster> raster_guard(raster);

	Profiler::Profiler p("CL_OPERATOR");

	raster->setRepresentation(GenericRaster::OPENCL);

	auto raster_out = GenericRaster::create(raster->lcrs, raster->dd, GenericRaster::OPENCL);


	cl::Kernel kernel = RasterOpenCL::addProgramFromFile("operators/cl/test.cl", "testKernel");
	kernel.setArg(0, *raster->getCLBuffer());
	kernel.setArg(1, *raster_out->getCLBuffer());
	kernel.setArg(2, (int) raster->lcrs.size[0]);
	kernel.setArg(3, (int) raster->lcrs.size[1]);
	//kernel.setArg(2, (int) raster->lcrs.getPixelCount());


	cl::Event event;
	try {
		Profiler::Profiler p("CL_EXECUTE");
		RasterOpenCL::getQueue()->enqueueNDRangeKernel(kernel,
			cl::NullRange, // Offset
			cl::NDRange(raster->lcrs.getPixelCount()), // Global
			cl::NullRange, // cl::NDRange(1, 1), // local
			nullptr, //events
			&event //event
		);
	}
	catch (cl::Error &e) {
		printf("Error %d: %s\n", e.err(), e.what());

		throw;
	}

	event.wait();
	return raster_out.release();
}

