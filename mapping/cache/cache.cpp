/*
 * cache.cpp
 *
 *  Created on: 12.05.2015
 *      Author: mika
 */

#include <iostream>
#include <sstream>

#include "cache/cache.h"
#include "raster/exceptions.h"
#include "operators/operator.h"

std::string qrToString(const QueryRectangle &rect) {
	std::ostringstream os;
	os << "QueryRectangle[ epsg: " << (uint16_t) rect.epsg << ", timestamp: "
			<< rect.timestamp << ", x: [" << rect.x1 << "," << rect.x2 << "]"
			<< ", y: [" << rect.y1 << "," << rect.y2 << "]" << ", res: ["
			<< rect.xres << "," << rect.yres << "] ]";
	return os.str();
}

std::string strefToString(const SpatioTemporalReference &ref) {
	std::ostringstream os;
	os << "SpatioTemporalReference[ epsg: " << (uint16_t) ref.epsg
			<< ", timetype: " << (uint16_t) ref.timetype << ", time: ["
			<< ref.t1 << "," << ref.t2 << "]" << ", x: [" << ref.x1 << ","
			<< ref.x2 << "]" << ", y: [" << ref.y1 << "," << ref.y2 << "] ]";
	return os.str();
}

//
// List-Structure
//

//
// A simple std::vector based implementation of the
// STCacheStructure interface
//
template<typename EType>
class STListCacheStructure : public STCacheStructure<EType> {
public:
	virtual ~STListCacheStructure() {};
	virtual void insert( const std::shared_ptr<STCacheEntry<EType>> &entry );
	virtual std::shared_ptr<STCacheEntry<EType>> query( const QueryRectangle &spec );
	virtual void remove( const std::shared_ptr<STCacheEntry<EType>> &entry );
protected:
	virtual bool matches( const QueryRectangle &spec, const EType &ref ) const = 0;
private:
	std::vector<std::shared_ptr<STCacheEntry<EType>>> entries;
};

template<typename EType>
void STListCacheStructure<EType>::insert(
		const std::shared_ptr<STCacheEntry<EType>> &entry) {
	entries.push_back(entry);
}

template<typename EType>
std::shared_ptr<STCacheEntry<EType>> STListCacheStructure<EType>::query(
		const QueryRectangle &spec) {
	for ( auto entry : entries ) {
		if (matches( spec, *entry->result ) ) {
			return entry;
		}
	}
	return nullptr;
}

template<typename EType>
void STListCacheStructure<EType>::remove(
		const std::shared_ptr<STCacheEntry<EType>> &entry) {
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		if (entry == *iter) {
			entries.erase(iter);
			return;
		}
	}
}

//
// List implementation of the raster cache
//

class STRasterCacheStructure : public STListCacheStructure<GenericRaster> {
public:
	virtual ~STRasterCacheStructure() {};
protected:
	virtual bool matches( const QueryRectangle &spec, const GenericRaster &entry ) const;
};

//
// Raster Structure
//

bool STRasterCacheStructure::matches(const QueryRectangle& spec,
		const GenericRaster &result) const {

	if (result.stref.timetype != TIMETYPE_UNIX)
		throw ArgumentException(
				std::string("Cache only accepts unix timestamps"));

	// Enlarge result by degrees of half a pixel in each direction
	double h_spacing = (result.stref.x2 - result.stref.x1) / result.width / 100.0;
	double v_spacing = (result.stref.y2 - result.stref.y1) / result.height / 100.0;

// DEBUGGING PART
	bool res = spec.epsg == result.stref.epsg;
	res &= spec.x1 >= result.stref.x1 - h_spacing;
	res &= spec.x2 <= result.stref.x2 + h_spacing;
	res &= spec.y1 >= result.stref.y1 - v_spacing;
	res &= spec.y2 <= result.stref.y2 + v_spacing;
	res &= spec.timestamp >= result.stref.t1;
	// FIXME: Shouldn't that be half open intervals? World1 -> Webmercator will never result in a hit if < instead of <= is used
	res &= spec.timestamp <= result.stref.t2;

	// Check query rectangle
	if ( spec.epsg == result.stref.epsg
			&& spec.x1 >= result.stref.x1 - h_spacing
			&& spec.x2 <= result.stref.x2 + h_spacing
			&& spec.y1 >= result.stref.y1 - v_spacing
			&& spec.y2 <= result.stref.y2 + v_spacing
			&& spec.timestamp >= result.stref.t1
			&& spec.timestamp <= result.stref.t2 ) {
		// Check resolution
		double ohspan = result.stref.x2 - result.stref.x1;
		double ovspan = result.stref.y2 - result.stref.y1;
		double qhspan = spec.x2 - spec.x1;
		double qvspan = spec.y2 - spec.y1;

		double hfact = qhspan / ohspan;
		double vfact = qvspan / ovspan;

		double clip_width  = result.width  * hfact;
		double clip_height = result.height * vfact;

		return clip_width >= spec.xres && clip_height >= spec.yres &&
			   clip_width < 2*spec.xres && clip_height < 2*spec.yres;
	}
	else
		return false;
}

//
// Cache implementation
//
template<typename EType>
STCache<EType>::~STCache() {
	for( auto &entry : caches ) {
		delete entry.second;
	}
}

template<typename EType>
STCacheStructure<EType>* STCache<EType>::getStructure(const std::string &key,
		bool create) {

	STCacheStructure<EType> *cache;
	auto got = caches.find(key);

	if (got == caches.end() && create) {
		Log::debug("No cache-structure for key found. Creating.");
		cache = newStructure();
		caches[key] = cache;
	} else if (got != caches.end())
		cache = got->second;
	else
		cache = nullptr;
	return cache;
}

template<typename EType>
void STCache<EType>::put(const std::string &key,
		const std::unique_ptr<EType> &item) {
	std::lock_guard<std::mutex> lock(mtx);
	Log::debug( "Adding entry for key \"%s\"", key.c_str());

	auto cache = getStructure(key, true);

	auto ce = newEntry(cache, item);
	Log::debug("Size of new Entry: %d bytes", ce->size);
	if (ce->size > max_size) {
		Log::warn(
				"Size of entry is greater than assigned cache-size of: %d bytes. Not inserting.",
				max_size);
	} else {
		if (current_size + ce->size > max_size) {
			Log::debug("New entry exhausts cache size. Cleaning up.");
			while (current_size + ce->size > max_size) {
				auto victim = policy->evict();
				Log::info("Evicting entry (%ld bytes): \"%s\"", victim->size, strefToString(victim->result->stref).c_str() );
				victim->structure->remove(victim);
				current_size -= victim->size;
			};
			Log::debug("Cleanup finished. Free space: %d bytes",
					(max_size - current_size));
		}
		Log::debug("Inserting new entry into Cache-Structure.");
		current_size += ce->size;
		policy->inserted(ce);
		cache->insert(ce);
	}
}

template<typename EType>
std::unique_ptr<EType> STCache<EType>::get(const std::string &key,
		const QueryRectangle &qr) {
	std::lock_guard<std::mutex> lock(mtx);
	Log::debug("Get: Quering \"%s\" in cache \"%s\"",
				qrToString(qr).c_str(), key.c_str());

	auto cache = getStructure(key);
	if (cache != nullptr) {
		auto entry = cache->query(qr);
		if (entry != nullptr) {
			policy->accessed(entry);
			Log::info("HIT for query \"%s\"", qrToString(qr).c_str());
			return copyContent(entry->result);
		} else {
			Log::info("MISS for query \"%s\"", qrToString(qr).c_str());
			throw NoSuchElementException("Entry not found");
		}
	} else {
		Log::info("MISS for query \"%s\"", qrToString(qr).c_str());
		throw NoSuchElementException("Entry not found");
	}

}

template<typename EType>
std::shared_ptr<STCacheEntry<EType>> STCache<EType>::newEntry(
		STCacheStructure<EType>* structure,
		const std::unique_ptr<EType>& result) {
	return std::shared_ptr<STCacheEntry<EType>>(
			new STCacheEntry<EType>(copyContent(result), getContentSize(result),
					structure));
}

//
// RasterCache
//

size_t RasterCache::getContentSize(
		const std::unique_ptr<GenericRaster>& content) {
	// TODO: Get correct size
	return sizeof(*content) + content->getDataSize();
}

std::unique_ptr<GenericRaster> RasterCache::copyContent(
		const std::unique_ptr<GenericRaster> &content) {
	std::unique_ptr<GenericRaster> copy = GenericRaster::create(content->dd,
			*content.get(), content->getRepresentation());
	copy->blit(content.get(), 0);
	return copy;
}

STCacheStructure<GenericRaster>* RasterCache::newStructure() {
	return new STRasterCacheStructure();
}

// Instantiate all
template class STCache<GenericRaster> ;

//
// Cache-Manager
//
std::unique_ptr<CacheManager> CacheManager::impl;

CacheManager& CacheManager::getInstance() {
	if (CacheManager::impl)
		return *impl;
	else
		throw NotInitializedException(
				"CacheManager was not initialized. Please use CacheManager::init first.");
}

void CacheManager::init(std::unique_ptr<CacheManager>& impl) {
	CacheManager::impl.reset(impl.release());
}

//
// Default local cache
//
std::unique_ptr<GenericRaster> DefaultCacheManager::getRaster(
		const std::string &semantic_id, const QueryRectangle &rect) {
	return rasterCache.get(semantic_id, rect);
}

void DefaultCacheManager::putRaster(const std::string& semantic_id,
		std::unique_ptr<GenericRaster>& raster) {
	rasterCache.put( semantic_id, raster );
}

//
// NopCache
//
std::unique_ptr<GenericRaster> NopCacheManager::getRaster(
		const std::string& semantic_id, const QueryRectangle& rect) {
	(void) semantic_id;
	(void) rect;
	throw NoSuchElementException("Cache Miss");
}

void NopCacheManager::putRaster(const std::string& semantic_id,
		std::unique_ptr<GenericRaster>& raster) {
	(void) semantic_id;
	(void) raster;
	// Nothing TODO
}
