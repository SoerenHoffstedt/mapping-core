
#ifndef MAPPING_NO_OPENCL

#include "datatypes/raster.h"
#include "datatypes/raster/raster_priv.h"
#include "datatypes/raster/typejuggling.h"
#include "raster/profiler.h"
#include "raster/opencl.h"
#include "operators/operator.h" // For QueryProfiler
#include "util/configuration.h"
#include "util/log.h"

#include <sstream>
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace RasterOpenCL {

static std::mutex opencl_init_mutex;
static std::atomic<int> initialization_status(0); // 0: not initialized, 1: success, 2: failure


static cl::Platform platform;

static cl::Context context;

static std::vector<cl::Device> deviceList;
static cl::Device device;

static cl::CommandQueue queue;

static size_t max_alloc_size = 0;
size_t getMaxAllocSize() {
	return max_alloc_size;
}

void init() {
	if (initialization_status == 0) {
		Profiler::Profiler p("CL_INIT");
		std::lock_guard<std::mutex> guard(opencl_init_mutex);
		if (initialization_status == 0) {
			// ok, let's initialize everything. Default to "failure".
			initialization_status = 2;

			// Platform
			std::vector<cl::Platform> platformList;
			cl::Platform::get(&platformList);
			if (platformList.size() == 0)
				throw PlatformException("No CL platforms found");
			//printf("Platform number is: %d\n", (int) platformList.size());

			auto preferredPlatformName = Configuration::get<std::string>("global.opencl.preferredplatform", "");
			int64_t selectedPlatform = -1;

			for (size_t i=0;i<platformList.size();i++) {
				std::string platformName;
				platformList[i].getInfo((cl_platform_info)CL_PLATFORM_NAME, &platformName);
				Log::info(concat("CL vendor ", i, ": ", platformName));

				if ( platformName[platformName.length()-1] == 0 )
					platformName = platformName.substr(0,platformName.length()-1);

				if (platformName == preferredPlatformName)
					selectedPlatform = i;
			}

			if (selectedPlatform < 0) {
				if (preferredPlatformName != "" || platformList.size() > 0)
					Log::debug("Configured openCL platform not found, using the first one offered");
				selectedPlatform = 0;
			}
			platform = platformList[selectedPlatform];

			// Context
			cl_context_properties cprops[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)(platform)(), 0};
			try {
				int device_type = CL_DEVICE_TYPE_GPU;
				if (Configuration::get<bool>("global.opencl.forcecpu", false))
					device_type = CL_DEVICE_TYPE_CPU;

				context = cl::Context(
						device_type, // _CPU
					cprops
				);
			}
			catch (const cl::Error &e) {
				std::stringstream ss;
				ss << "Error " << e.err() << ": " << e.what();
				throw OpenCLException(ss.str(), MappingExceptionType::CONFIDENTIAL);
			}

			// Device
			deviceList = context.getInfo<CL_CONTEXT_DEVICES>();
			if (deviceList.size() == 0)
				throw PlatformException("No CL devices found");
			device = deviceList[0];

			cl_ulong _max_alloc_size = 0;
			device.getInfo((cl_platform_info) CL_DEVICE_MAX_MEM_ALLOC_SIZE, &_max_alloc_size);
			max_alloc_size = _max_alloc_size;

			// Command Queue
			// CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE is not set
			queue = cl::CommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE);

			initialization_status = 1;
		}
	}

	if (initialization_status != 1)
		throw PlatformException("could not initialize opencl");
}

void freeProgramCache();
void free() {
	std::lock_guard<std::mutex> guard(opencl_init_mutex);
	if (initialization_status == 1) {
		freeProgramCache();

		platform = cl::Platform();

		context = cl::Context();

		deviceList.clear();
		device = cl::Device();

		queue = cl::CommandQueue();
	}

	initialization_status = 0;
}

cl::Platform *getPlatform() {
	return &platform;
}

cl::Context *getContext() {
	return &context;
}

cl::Device *getDevice() {
	return &device;
}

cl::CommandQueue *getQueue() {
	return &queue;
}


/*
struct RasterInfo {
	cl_double origin[3];
	cl_double scale[3];

	cl_double min, max, no_data;

	cl_uint size[3];

	cl_ushort epsg;
	cl_ushort has_no_data;
};

static const std::string rasterinfo_source(
"typedef struct {"
"	double origin[3];"
"	double scale[3];"
"	double min, max, no_data;"
"	uint size[3];"
"	ushort epsg;"
"	ushort has_no_data;"
"} RasterInfo;\n"
);
*/
struct RasterInfo {
	cl_uint size[3];
	cl_double origin[3];
	cl_double scale[3];

	cl_double min, max, no_data;

	cl_ushort crs_code;
	cl_ushort has_no_data;
};

static const std::string rasterinfo_source(
"typedef struct {"
"	uint size[3];"
"	double origin[3];"
"	double scale[3];"
"	double min, max, no_data;"
"	ushort crs_code;"
"	ushort has_no_data;"
"} RasterInfo;\n"
"#define R(t,x,y) t ## _data[y * t ## _info->size[0] + x]\n"
);


std::unique_ptr<cl::Buffer> getBufferWithRasterinfo(GenericRaster *raster) {
	RasterInfo ri;
	memset(&ri, 0, sizeof(RasterInfo));
	ri.size[0] = raster->width;
	ri.size[1] = raster->height;
	ri.size[2] = 1;
	ri.origin[0] = raster->PixelToWorldX(0);
	ri.origin[1] = raster->PixelToWorldY(0);
	ri.origin[2] = 0.0;
	ri.scale[0] = raster->pixel_scale_x;
	ri.scale[1] = raster->pixel_scale_y;
	ri.scale[2] = 1.0;

	ri.crs_code = raster->stref.crsId.code;

	ri.min = raster->dd.unit.getMin();
	ri.max = raster->dd.unit.getMax();
	ri.no_data = raster->dd.has_no_data ? raster->dd.no_data : 0.0;
	ri.has_no_data = raster->dd.has_no_data;

	try {
		auto buffer = make_unique<cl::Buffer>(
			*RasterOpenCL::getContext(),
			CL_MEM_READ_ONLY,
			sizeof(RasterInfo),
			nullptr
		);
		RasterOpenCL::getQueue()->enqueueWriteBuffer(*buffer, CL_TRUE, 0, sizeof(RasterInfo), &ri);

		return buffer;
	}
	catch (cl::Error &e) {
		std::stringstream ss;
		ss << "CL Error in getBufferWithRasterinfo(): " << e.err() << ": " << e.what();
		throw OpenCLException(ss.str(), MappingExceptionType::CONFIDENTIAL);
	}
}

const std::string &getRasterInfoStructSource() {
	return rasterinfo_source;
}


/*
 * ProgramCache
 */

// For the first implementation, only one program may be compiled at a time, and
// there's no replacement strategy; the cache just keeps on filling.
static std::mutex program_cache_mutex;
static std::unordered_map<std::string, cl::Program> program_cache;

void freeProgramCache() {
	std::lock_guard<std::mutex> guard(program_cache_mutex);
	program_cache.clear();
}

cl::Program compileSource(const std::string &sourcecode) {
	std::lock_guard<std::mutex> guard(program_cache_mutex);

	if (program_cache.count(sourcecode) == 1) {
		return program_cache.at(sourcecode);
	}

	cl::Program program;
	try {
		cl::Program::Sources sources(1, std::make_pair(sourcecode.c_str(), sourcecode.length()));
		program = cl::Program(context, sources);
		program.build(deviceList,""); // "-cl-std=CL2.0"
	}
	catch (const cl::Error &e) {
		std::stringstream ss;
		ss << "Error building cl::Program: " << e.what() << " " << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(deviceList[0]);
		throw OpenCLException(ss.str(), MappingExceptionType::CONFIDENTIAL);
	}

	program_cache[sourcecode] = program;
	return program;
}


/*
 * CLProgram
 */
CLProgram::CLProgram() : profiler(nullptr), kernel(nullptr), argpos(0), finished(false), iteration_type(0), in_rasters(), out_rasters(), pointcollections() {
}
CLProgram::~CLProgram() {
	reset();
}
void CLProgram::addInRaster(GenericRaster *raster) {
	if (kernel)
		throw OpenCLException("addInRaster() must be called before compile()");
	in_rasters.push_back(raster);
}
void CLProgram::addOutRaster(GenericRaster *raster) {
	if (kernel)
		throw OpenCLException("addOutRaster() must be called before compile()");
	if (iteration_type == 0)
		iteration_type = 1;
	out_rasters.push_back(raster);
}

size_t CLProgram::addPointCollection(PointCollection *pc) {
	if (kernel)
		throw OpenCLException("addPointCollection() must be called before compile()");
	if (iteration_type == 0)
		iteration_type = 2;
	pointcollections.push_back(pc);
	return pointcollections.size() - 1;
}

void CLProgram::addPointCollectionPositions(size_t idx, bool readonly) {
	if (sizeof(cl_double2) != sizeof(Coordinate))
		throw OpenCLException("sizeof(cl_double2) != sizeof(Coordinate), cannot use opencl on pointcollections");

	PointCollection *pc = pointcollections.at(idx);
	addArg(pc->coordinates, readonly);
}

void CLProgram::addPointCollectionAttribute(size_t idx, const std::string &name, bool readonly) {
	PointCollection *pc = pointcollections.at(idx);
	auto &array = pc->feature_attributes.numeric(name);
	addArg(array.array, readonly);
}


template<typename T>
struct getCLTypeName {
	static const char *execute(Raster2D<T> *) {
		return RasterTypeInfo<T>::cltypename;
	}
};

void CLProgram::compile(const std::string &sourcecode, const char *kernelname) {
	if (iteration_type == 0)
		throw OpenCLException("No raster or pointcollection added, cannot iterate");

	std::stringstream assembled_source;
	assembled_source << RasterOpenCL::getRasterInfoStructSource();
	for (decltype(in_rasters.size()) idx = 0; idx < in_rasters.size(); idx++) {
		assembled_source << "typedef " << callUnaryOperatorFunc<getCLTypeName>(in_rasters[idx]) << " IN_TYPE" << idx << ";\n";
		// TODO: the first case is an optimization that may reduce our ability to cache programs.
		if (!in_rasters[idx]->dd.has_no_data)
			assembled_source << "#define ISNODATA"<<idx<<"(v,i) (false)\n";
		else if (in_rasters[idx]->dd.datatype == GDT_Float32 || in_rasters[idx]->dd.datatype == GDT_Float64)
			assembled_source << "#define ISNODATA"<<idx<<"(v,i) (i->has_no_data && (isnan(v) || v == i->no_data))\n";
		else
			assembled_source << "#define ISNODATA"<<idx<<"(v,i) (i->has_no_data && v == i->no_data)\n";
	}
	for (decltype(out_rasters.size()) idx = 0; idx < out_rasters.size(); idx++) {
		assembled_source << "typedef " << callUnaryOperatorFunc<getCLTypeName>(out_rasters[idx]) << " OUT_TYPE" << idx << ";\n";
	}

	assembled_source << sourcecode;

	cl::Program program = compileSource(assembled_source.str());

	try {
		kernel = make_unique<cl::Kernel>(program, kernelname);

		for (decltype(in_rasters.size()) idx = 0; idx < in_rasters.size(); idx++) {
			GenericRaster *raster = in_rasters[idx];
			raster->setRepresentation(GenericRaster::Representation::OPENCL);
			kernel->setArg(argpos++, *raster->getCLBuffer());
			kernel->setArg(argpos++, *raster->getCLInfoBuffer());
		}
		for (decltype(out_rasters.size()) idx = 0; idx < out_rasters.size(); idx++) {
			GenericRaster *raster = out_rasters[idx];
			raster->setRepresentation(GenericRaster::Representation::OPENCL);
			kernel->setArg(argpos++, *raster->getCLBuffer());
			kernel->setArg(argpos++, *raster->getCLInfoBuffer());
		}
		for (decltype(pointcollections.size()) idx = 0; idx < pointcollections.size(); idx++) {
			PointCollection *points = pointcollections[idx];
			int size = points->coordinates.size();
			kernel->setArg<cl_int>(argpos++, (cl_int) size);
		}
	}
	catch (const cl::Error &e) {
		kernel.reset(nullptr);
		std::stringstream msg;
		msg << "CL Error in compile(): " << e.err() << ": " << e.what();
		throw OpenCLException(msg.str(), MappingExceptionType::CONFIDENTIAL);
	}

}


void CLProgram::run() {
	try {
		cl::Event event = run(nullptr);
		event.wait();
		if (profiler) {
			cl_ulong start = 0, end = 0;
			event.getProfilingInfo(CL_PROFILING_COMMAND_START, &start);
			event.getProfilingInfo(CL_PROFILING_COMMAND_END, &end);
			double seconds = (end - start) / 1000000000.0;
			profiler->addGPUCost(seconds);
		}
	}
	catch (cl::Error &e) {
		std::stringstream ss;
		ss << "CL Error: " << e.err() << ": " << e.what();
		throw OpenCLException(ss.str(), MappingExceptionType::CONFIDENTIAL);
	}
}

cl::Event CLProgram::run(std::vector<cl::Event>* events_to_wait_for) {
	if (!kernel)
		throw OpenCLException("Cannot run() before compile()");

	if (finished)
		throw OpenCLException("Cannot run() a CLProgram twice (TODO: lift this restriction? Use case?)");

	finished = true;
	cl::Event event;

	cl::NDRange range;
	if (iteration_type == 1)
		range = cl::NDRange(out_rasters[0]->width, out_rasters[0]->height);
	else if (iteration_type == 2)
		range = cl::NDRange(pointcollections[0]->coordinates.size());
	else
		throw OpenCLException("Unknown iteration_type, cannot create range");

	try {
		RasterOpenCL::getQueue()->enqueueNDRangeKernel(*kernel,
			cl::NullRange, // Offset
			range, // Global
			cl::NullRange, // local
			events_to_wait_for, //events to wait for
			&event //event to create
		);
		return event;
	}
	catch (cl::Error &e) {
		std::stringstream ss;
		ss << "CL Error: " << e.err() << ": " << e.what();
		throw OpenCLException(ss.str(), MappingExceptionType::CONFIDENTIAL);
	}
	cleanScratch();
}


void CLProgram::cleanScratch() {
	size_t buffers = scratch_buffers.size();
	for (size_t i=0;i<buffers;i++) {
		auto clbuffer = scratch_buffers[i];
		if (!clbuffer)
			continue;
		if (i < scratch_maps.size()) {
			auto clhostptr = scratch_maps[i];
			RasterOpenCL::getQueue()->enqueueUnmapMemObject(*clbuffer, clhostptr);
		}
		scratch_buffers[i] = nullptr;
		delete clbuffer;
	}
	scratch_buffers.clear();
	scratch_maps.clear();
}

void CLProgram::reset() {
	kernel.reset(nullptr);
	cleanScratch();
	argpos = 0;
	finished = false;
	iteration_type = 0;
	in_rasters.clear();
	out_rasters.clear();
	pointcollections.clear();
}


} // End namespace RasterOpenCL

#endif
