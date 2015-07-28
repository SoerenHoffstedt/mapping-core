#include "datatypes/spatiotemporal.h"
#include "util/exceptions.h"
#include "operators/operator.h"
#include "util/binarystream.h"
#include "util/debug.h"

#include "boost/date_time/posix_time/posix_time.hpp"
#include <math.h>
#include <limits>
#include <sstream>



/**
 * SpatialReference
 */
SpatialReference::SpatialReference(epsg_t epsg) : epsg(epsg) {
	x1 = -std::numeric_limits<double>::infinity();
	y1 = -std::numeric_limits<double>::infinity();
	x2 = std::numeric_limits<double>::infinity();
	y2 = std::numeric_limits<double>::infinity();

	validate();
};

SpatialReference::SpatialReference(epsg_t epsg, double x1, double y1, double x2, double y2)
	: epsg(epsg), x1(x1), y1(y1), x2(x2), y2(y2) {
	validate();
};

SpatialReference::SpatialReference(epsg_t epsg, double x1, double y1, double x2, double y2, bool &flipx, bool &flipy)
	: epsg(epsg), x1(x1), y1(y1), x2(x2), y2(y2) {
	flipx = flipy = false;
	if (x1 > x2) {
		flipx = true;
		std::swap(this->x1, this->x2);
	}
	if (y1 > y2) {
		flipy = true;
		std::swap(this->y1, this->y2);
	}
	validate();
}

SpatialReference::SpatialReference(BinaryStream &stream) {
	uint32_t uint;
	stream.read(&uint);
	epsg = (epsg_t) uint;

	stream.read(&x1);
	stream.read(&y1);
	stream.read(&x2);
	stream.read(&y2);

	validate();
}

void SpatialReference::toStream(BinaryStream &stream) const {
	stream.write((uint32_t) epsg);
	stream.write(x1);
	stream.write(y1);
	stream.write(x2);
	stream.write(y2);
}

/*
 * Returns whether the other SpatialReference is contained (smaller or equal) within this.
 * Throws an exception if the crs don't match
 */
bool SpatialReference::contains(const SpatialReference &other) const {
	if (epsg != other.epsg)
		throw ArgumentException("SpatialReference::contains(): epsg don't match");

	return x1 <= other.x1 && y1 <= other.y1 && x2 >= other.x2 && y2 >= other.y2;
}


void SpatialReference::validate() const {
	if (x1 > x2 || y1 > y2) {
		std::stringstream msg;
		msg << "SpatialReference invalid, requires x1:" << x1 << " <= x2:" << x2 << ", y1:" << y1 << " <= y2:" << y2;
		throw ArgumentException(msg.str());
	}
}

SpatialReference SpatialReference::extent(epsg_t epsg) {
	if (epsg == EPSG_WEBMERCATOR)
		return SpatialReference(EPSG_WEBMERCATOR, -20037508.34,-20037508.34,20037508.34,20037508.34);
	if (epsg == EPSG_LATLON)
		return SpatialReference(EPSG_LATLON, -180, -90, 180, 90);
	if (epsg == EPSG_GEOSMSG)
		return SpatialReference(EPSG_GEOSMSG, -5568748.276, -5568748.276, 5568748.276, 5568748.276);

	throw ArgumentException("Cannot return extent of an unknown CRS");
}

/**
 * TemporalReference
 */

TemporalReference::TemporalReference(timetype_t timetype) : timetype(timetype) {
	t1 = -std::numeric_limits<double>::infinity();
	t2 = std::numeric_limits<double>::infinity();

	validate();
};


TemporalReference::TemporalReference(timetype_t timetype, double t1, double t2)
	: timetype(timetype), t1(t1), t2(t2) {
	validate();
};


TemporalReference::TemporalReference(BinaryStream &stream) {
	uint32_t uint;
	stream.read(&uint);
	timetype = (timetype_t) uint;

	stream.read(&t1);
	stream.read(&t2);

	validate();
}

void TemporalReference::toStream(BinaryStream &stream) const {
	stream.write((uint32_t) timetype);
	stream.write(t1);
	stream.write(t2);
}


void TemporalReference::validate() const {
	if (t1 > t2) {
		std::stringstream msg;
		msg << "TemporalReference invalid, requires t1:" << t1 << " <= t2:" << t2;
		throw ArgumentException(msg.str());
	}
}


bool TemporalReference::contains(const TemporalReference &other) const {
	if (timetype != other.timetype)
		throw ArgumentException("TemporalReference::contains(): timetypes don't match");

	return t1 <= other.t1 && t2 >= other.t2;
}

void TemporalReference::intersect(const TemporalReference &other) {
	if (timetype != other.timetype)
		throw ArgumentException("Cannot intersect() TemporalReferences with different timetype");

	t1 = std::max(t1, other.t1);
	t2 = std::min(t2, other.t2);
	if (t1 > t2)
		throw ArgumentException("intersect(): both TemporalReferences do not intersect");
}

std::string TemporalReference::toIsoString(double time) const {
	std::ostringstream result;

	if(timetype == TIMETYPE_UNIX){
		result << boost::posix_time::to_iso_extended_string(boost::posix_time::from_time_t(time));
	} else {
		//TODO: throw Exception?
		result << time;
	}

	return result.str();
}


/**
 * SpatioTemporalReference
 */
SpatioTemporalReference::SpatioTemporalReference(BinaryStream &stream) : SpatialReference(stream), TemporalReference(stream) {
}

void SpatioTemporalReference::toStream(BinaryStream &stream) const {
	SpatialReference::toStream(stream);
	TemporalReference::toStream(stream);
}

SpatioTemporalReference::SpatioTemporalReference(const QueryRectangle &rect) : SpatialReference(rect), TemporalReference(rect) {
}

void SpatioTemporalReference::validate() const {
	SpatialReference::validate();
	TemporalReference::validate();
}

/**
 * SpatioTemporalResult
 */
void SpatioTemporalResult::replaceSTRef(const SpatioTemporalReference &newstref) {
	const_cast<SpatioTemporalReference&>(this->stref) = newstref;
}

/**
 * helper functions
 */
epsg_t epsgCodeFromSrsString(const std::string &srsString, epsg_t def) {
	if (srsString == "")
		return def;
	if (srsString.compare(0,5,"EPSG:") == 0)
		return (epsg_t) std::stoi(srsString.substr(5, std::string::npos));
	throw ArgumentException("Unknown CRS specified");
}


