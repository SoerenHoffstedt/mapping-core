
#include "raster/raster_priv.h"
#include "raster/typejuggling.h"
#include "raster/opencl.h"
#include "util/hash.h"
#include "operators/operator.h"

#include <memory>
#include <cmath>
#include <limits>
#include <vector>
#include <string>
#include <sstream>


LocalCRS::LocalCRS(const QueryRectangle &rect) : epsg(rect.epsg), dimensions(2), size{rect.xres, rect.yres, 0}, origin{rect.x1, rect.y1, 0}, scale{(rect.x2-rect.x1)/rect.xres, (rect.y2-rect.y1)/rect.yres, 0} {

}

bool LocalCRS::operator==(const LocalCRS &b) const {
	if (dimensions != b.dimensions)
		return false;
	for (int i=0;i<dimensions;i++) {
		if (size[i] != b.size[i]) {
			std::cerr << "size mismatch" << std::endl;
			return false;
		}
		if (fabs(origin[i] - b.origin[i]) > 0.5) {
			std::cerr << "origin mismatch: " << fabs(origin[i] - b.origin[i]) << std::endl;
			return false;
		}
		if (fabs(scale[i] / b.scale[i] - 1.0) > 0.001) {
			std::cerr << "scale mismatch" << std::endl;
			return false;
		}
	}
	return true;
}

void LocalCRS::verify() const {
	if (dimensions < 1 || dimensions > 3)
		throw MetadataException("Amount of dimensions not between 1 and 3");
	for (int i=0;i<dimensions;i++) {
		if (/*size[i] < 0 || */ size[i] > 1<<16)
			throw MetadataException("Size out of limits");
		if (scale[i] == 0)
			throw MetadataException("Scale cannot be 0");
	}
}

size_t LocalCRS::getPixelCount() const {
	if (dimensions == 1)
		return (size_t) size[0];
	if (dimensions == 2)
		return (size_t) size[0] * size[1];
	if (dimensions == 3)
		return (size_t) size[0] * size[1] * size[2];
	throw MetadataException("Amount of dimensions not between 1 and 3");
}


std::ostream& operator<< (std::ostream &out, const LocalCRS &rm) {
	out << "LocalCRS(epsg=" << rm.epsg << " dim=" << rm.dimensions << " size=["<<rm.size[0]<<","<<rm.size[1]<<"] origin=["<<rm.origin[0]<<","<<rm.origin[1]<<"] scale=["<<rm.scale[0]<<","<<rm.scale[1]<<"])";
	return out;
}


bool DataDescription::operator==(const DataDescription &b) const {
	return datatype == b.datatype
		&& min == b.min && max == b.max
		&& has_no_data == b.has_no_data && (!has_no_data || (no_data == b.no_data));
}

void DataDescription::verify() const {
	if (min >= max)
		throw MetadataException("ValueMetadata::verify: min >= max");
	if (min < getMinByDatatype() || max > getMaxByDatatype())
		throw MetadataException("ValueMetadata::verify: min or max outside of range allowed by datatype");

	if (has_no_data) {
		/*
		if (no_data < min || no_data > max)
			throw MetadataException("ValueMetadata::verify: no_data not in [min, max]");
		*/
		if (no_data < getMinByDatatype() || no_data > getMaxByDatatype())
			throw MetadataException("ValueMetadata::verify: no_data outside of range allowed by datatype");
	}
}

int DataDescription::getBPP() const {
	switch(datatype) {
		case GDT_Byte: return sizeof(uint8_t);
		case GDT_Int16: return sizeof(int16_t);
		case GDT_UInt16: return sizeof(uint16_t);
		case GDT_Int32: return sizeof(int32_t);
		case GDT_UInt32: return sizeof(uint32_t);
		case GDT_Float32: return sizeof(float);
		case GDT_Float64:
			throw MetadataException("Unsupported data type: Float64");
		case GDT_CInt16:
			throw MetadataException("Unsupported data type: CInt16");
		case GDT_CInt32:
			throw MetadataException("Unsupported data type: CInt32");
		case GDT_CFloat32:
			throw MetadataException("Unsupported data type: CFloat32");
		case GDT_CFloat64:
			throw MetadataException("Unsupported data type: CFloat64");
		default:
			throw MetadataException("Unknown data type");
	}
}

double DataDescription::getMinByDatatype() const {
	switch(datatype) {
		case GDT_Byte: return std::numeric_limits<uint8_t>::min();
		case GDT_Int16: return std::numeric_limits<int16_t>::min();
		case GDT_UInt16: return std::numeric_limits<uint16_t>::min();
		case GDT_Int32: return std::numeric_limits<int32_t>::min();
		case GDT_UInt32: return std::numeric_limits<uint32_t>::min();
		case GDT_Float32: return std::numeric_limits<float>::min();
		case GDT_Float64:
			throw MetadataException("Unsupported data type: Float64");
		case GDT_CInt16:
			throw MetadataException("Unsupported data type: CInt16");
		case GDT_CInt32:
			throw MetadataException("Unsupported data type: CInt32");
		case GDT_CFloat32:
			throw MetadataException("Unsupported data type: CFloat32");
		case GDT_CFloat64:
			throw MetadataException("Unsupported data type: CFloat64");
		default:
			throw MetadataException("Unknown data type");
	}
}

double DataDescription::getMaxByDatatype() const {
	switch(datatype) {
		case GDT_Byte: return std::numeric_limits<uint8_t>::max();
		case GDT_Int16: return std::numeric_limits<int16_t>::max();
		case GDT_UInt16: return std::numeric_limits<uint16_t>::max();
		case GDT_Int32: return std::numeric_limits<int32_t>::max();
		case GDT_UInt32: return std::numeric_limits<uint32_t>::max();
		case GDT_Float32: return std::numeric_limits<float>::max();
		case GDT_Float64:
			throw MetadataException("Unsupported data type: Float64");
		case GDT_CInt16:
			throw MetadataException("Unsupported data type: CInt16");
		case GDT_CInt32:
			throw MetadataException("Unsupported data type: CInt32");
		case GDT_CFloat32:
			throw MetadataException("Unsupported data type: CFloat32");
		case GDT_CFloat64:
			throw MetadataException("Unsupported data type: CFloat64");
		default:
			throw MetadataException("Unknown data type");
	}
}

void DataDescription::print() const {
	printf("Datatype: %d (%g - %g), nodata = %s (%g)\n", datatype, min, max, has_no_data ? "yes" : "no", no_data);
}

std::ostream& operator<< (std::ostream &out, const DataDescription &dd) {
	out << "Datatype: " << dd.datatype << " (" << dd.min << " - " << dd.max << ")";
	if (dd.has_no_data)
		out << " nodata = " << dd.no_data;
	else
		out << " no nodata";
	out << std::endl;
	return out;
}

void DataDescription::addNoData() {
	if (has_no_data)
		return;

	double real_min = getMinByDatatype();
	double real_max = getMaxByDatatype();
	if (real_min <= min - 1) {
		min = min - 1;
		no_data = min;
	}
	else if (real_max >= max + 1) {
		max = max + 1;
		no_data = max;
	}
	else {
		std::ostringstream ss;
		ss << "Cannot add value for no_data: range of datatype is exhausted. range (" << min << " - " << max << "), datatype (" << real_min << " - " << real_max << ")";
		throw MetadataException(ss.str());
	}

	has_no_data = true;
}




GenericRaster *GenericRaster::create(const LocalCRS &localcrs, const DataDescription &datadescription, Representation representation)
{
	if (localcrs.dimensions != 2)
		throw MetadataException("Cannot instantiate raster with dimensions != 2 yet");

	if (localcrs.getPixelCount() <= 0)
		throw MetadataException("Cannot instantiate raster with 0 pixels");

	GenericRaster *result = nullptr;
	switch(datadescription.datatype) {
		case GDT_Byte:
			result = new Raster2D<uint8_t>(localcrs, datadescription);
			break;
		case GDT_Int16:
			result = new Raster2D<int16_t>(localcrs, datadescription);
			break;
		case GDT_UInt16:
			result = new Raster2D<uint16_t>(localcrs, datadescription);
			break;
		case GDT_Int32:
			result = new Raster2D<int32_t>(localcrs, datadescription);
			break;
		case GDT_UInt32:
			result = new Raster2D<uint32_t>(localcrs, datadescription);
			break;
		case GDT_Float32:
			result = new Raster2D<float>(localcrs, datadescription);
			break;
		case GDT_Float64:
			throw MetadataException("Unsupported data type: Float64");
		case GDT_CInt16:
			throw MetadataException("Unsupported data type: CInt16");
		case GDT_CInt32:
			throw MetadataException("Unsupported data type: CInt32");
		case GDT_CFloat32:
			throw MetadataException("Unsupported data type: CFloat32");
		case GDT_CFloat64:
			throw MetadataException("Unsupported data type: CFloat64");
		default:
			throw MetadataException("Unknown data type");
	}

	result->setRepresentation(representation);
	return result;
}

GenericRaster::GenericRaster(const LocalCRS &localcrs, const DataDescription &datadescription)
	: lcrs(localcrs), dd(datadescription), representation(Representation::CPU) {
}

GenericRaster::~GenericRaster() {
}




template<typename T, int dimensions>
Raster<T, dimensions>::Raster(const LocalCRS &localcrs, const DataDescription &datadescription)
	: GenericRaster(localcrs, datadescription), clbuffer(nullptr), clbuffer_info(nullptr) {
	if (lcrs.dimensions != dimensions)
		throw MetadataException("metadata dimensions do not match raster dimensions");
	auto count = lcrs.getPixelCount();
	data = new T[count + 1];
	data[count] = 42;
}


template<typename T, int dimensions>
Raster<T, dimensions>::~Raster() {
	if (data[lcrs.getPixelCount()] != 42) {
		printf("Error in Raster: guard value was overwritten. Memory corruption!\n");
		exit(6);
	}

	delete [] data;
	data = nullptr;

}


template<typename T>
Raster2D<T>::~Raster2D() {
}


template<typename T, int dimensions>
void Raster<T, dimensions>::setRepresentation(Representation r) {
	if (r == representation)
		return;

	if (r == Representation::OPENCL) {
		//printf("Migrating raster to GPU\n");
		// https://www.khronos.org/registry/cl/sdk/1.0/docs/man/xhtml/clCreateBuffer.html
		try {
			clbuffer = new cl::Buffer(
				*RasterOpenCL::getContext(),
				CL_MEM_READ_WRITE, // | CL_MEM_USE_HOST_PTR, // CL_MEM_COPY_HOST_PTR
				getDataSize(),
				nullptr //data
			);
			RasterOpenCL::getQueue()->enqueueWriteBuffer(*clbuffer, CL_TRUE, 0, getDataSize(), data);
		}
		catch (cl::Error &e) {
			std::stringstream ss;
			ss << "CL Error in Raster::setRepresentation(): " << e.err() << ": " << e.what();
			throw OpenCLException(ss.str());
		}

		clbuffer_info = RasterOpenCL::getBufferWithRasterinfo(this);

		// TODO: data löschen?
	}
	else if (r == Representation::CPU) {
		//printf("Migrating raster back to CPU\n");
		RasterOpenCL::getQueue()->enqueueReadBuffer(*clbuffer, CL_TRUE, 0, getDataSize(), data);
		delete clbuffer;
		clbuffer = nullptr;
		delete clbuffer_info;
		clbuffer_info = nullptr;
	}
	else
		throw MetadataException("Invalid representation chosen");

	representation = r;
}



template<typename T>
void Raster2D<T>::clear(double _value) {
	T value = (T) _value;

	setRepresentation(GenericRaster::Representation::CPU);
	auto size = lcrs.getPixelCount();
	for (decltype(size) i=0;i<size;i++) {
		data[i] = value;
	}
}


template<typename T>
void Raster2D<T>::blit(const GenericRaster *genericraster, int destx, int desty, int) {
	if (genericraster->lcrs.dimensions != 2 || genericraster->lcrs.epsg != lcrs.epsg || genericraster->dd.datatype != dd.datatype)
		throw MetadataException("blit with incompatible raster");

	setRepresentation(GenericRaster::Representation::CPU);
	if (genericraster->getRepresentation() != GenericRaster::Representation::CPU)
		throw MetadataException("blit from raster that's not in a CPU buffer");

	Raster2D<T> *raster = (Raster2D<T> *) genericraster;
	int x1 = std::max(destx, 0);
	int y1 = std::max(desty, 0);
	int x2 = std::min(lcrs.size[0], destx+raster->lcrs.size[0]);
	int y2 = std::min(lcrs.size[1], desty+raster->lcrs.size[1]);

/*
	fprintf(stderr, "this raster is %dx%d\n", lcrs.size[0], lcrs.size[1]);
	fprintf(stderr, "other raster is %dx%d\n", raster->lcrs.size[0], raster->lcrs.size[1]);
	fprintf(stderr, "blitting other at (%d,%d) to (%d,%d) -> (%d,%d)\n", destx, desty, x1, y1, x2, y2);
*/
	if (x1 >= x2 || y1 >= y2)
		throw MetadataException("blit without overlapping region");

#define BLIT_TYPE 1
#if BLIT_TYPE == 1 // 0.0286
	for (int y=y1;y<y2;y++)
		for (int x=x1;x<x2;x++)
			set(x, y, raster->get(x-destx, y-desty));
#elif BLIT_TYPE == 2 // 0.0246
	int blitwidth = x2-x1;
	for (int y=y1;y<y2;y++) {
		int rowoffset_dest = y * this->lcrs.size[0] + x1;
		int rowoffset_src = (y-desty) * raster->lcrs.size[0] + (x1-destx);
		memcpy(&data[rowoffset_dest], &raster->data[rowoffset_src], blitwidth * sizeof(T));
	}
#else // 0.031
	int blitwidth = x2-x1;
	for (int y=y1;y<y2;y++) {
		int rowoffset_dest = y * this->lcrs.size[0] + x1;
		int rowoffset_src = (y-desty) * raster->lcrs.size[0] + (x1-destx);
		for (int x=0;x<blitwidth;x++)
			data[rowoffset_dest+x] = raster->data[rowoffset_src+x];
	}
#endif
}


template<typename T>
GenericRaster *Raster2D<T>::cut(int x1, int y1, int z1, int width, int height, int depth) {
	if (lcrs.dimensions != 2)
		throw MetadataException("cut() only works on 2d rasters");
	if (z1 != 0 || depth != 0)
		throw MetadataException("cut() should not specify 3d coordinates on a 2d raster");

	if (x1 < 0 || x1 + width > (int) lcrs.size[0] || y1 < 0 || y1 + height > (int) lcrs.size[1])
		throw MetadataException("cut() not inside the raster");

	// Der Origin in Weltkoordinaten ist jetzt woanders, skalierung bleibt
	LocalCRS newrmd(lcrs.epsg, width, height,
		lcrs.PixelToWorldX(x1), lcrs.PixelToWorldY(y1),
		lcrs.scale[0], lcrs.scale[1]
	);

	Raster2D<T> *outputraster = (Raster2D<T> *) GenericRaster::create(newrmd, dd);

/*
#define BLIT_TYPE 2
#if BLIT_TYPE == 1 // 0.0286
	for (int y=y1;y<y2;y++)
		for (int x=x1;x<x2;x++)
			set(x, y, raster->get(x-destx, y-desty));
#elif BLIT_TYPE == 2 // 0.0246
*/
	for (int y=0;y<height;y++) {
		int rowoffset_src = (y+y1) * lcrs.size[0] + x1;
		int rowoffset_dest = y * width;
		memcpy(&outputraster->data[rowoffset_dest], &data[rowoffset_src], width * sizeof(T));
	}
/*
#else // 0.031
	int blitwidth = x2-x1;
	for (int y=y1;y<y2;y++) {
		int rowoffset_dest = y * this->lcrs.size[0] + x1;
		int rowoffset_src = y * raster->lcrs.size[0] + x1;
		for (int x=0;x<blitwidth;x++)
			data[rowoffset_dest+x] = raster->data[rowoffset_src+x];
	}
#endif
*/
	return outputraster;
}

template<typename T>
GenericRaster *Raster2D<T>::scale(int width, int height, int depth) {
	if (lcrs.dimensions != 2)
		throw MetadataException("scale() only works on 2d rasters");
	if (depth != 0)
		throw MetadataException("scale() should not specify z depth on a 2d raster");

	if (width <= 0 || height <= 0)
		throw MetadataException("scale() to empty area not allowed");

	LocalCRS newrmd(lcrs.epsg, width, height,
		lcrs.PixelToWorldX(0), lcrs.PixelToWorldY(0),
		lcrs.scale[0] * (double) width / lcrs.size[0], lcrs.scale[1] * (double) height / lcrs.size[0]
	);

	Raster2D<T> *outputraster = (Raster2D<T> *) GenericRaster::create(newrmd, dd);

	int src_width = lcrs.size[0], src_height = lcrs.size[1];

	for (int y=0;y<height;y++) {
		for (int x=0;x<width;x++) {
			int px = (x * src_width / width);
			int py = (y * src_height / height);
			outputraster->set(x, y, get(px, py));
		}
	}

	return outputraster;
}



template<typename T>
double Raster2D<T>::getAsDouble(int x, int y, int) {
	return (double) get(x, y);
}



std::string GenericRaster::hash() {
	setRepresentation(GenericRaster::Representation::CPU);
	const int len = getDataSize();
	const uint8_t * data = (const uint8_t*) getData();

	return calculateHash(data, len).asHex();
}


RASTER_PRIV_INSTANTIATE_ALL
