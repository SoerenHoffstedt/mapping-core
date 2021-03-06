#include "util/configuration.h"
#include "cache/manager.h"
#include "util/timeparser.h"

#include "services/httpservice.h"
#include "userdb/userdb.h"
#include "util/log.h"
#include "featurecollectiondb/featurecollectiondb.h"

#include "cache/node/manager/local_manager.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <json/json.h>
#include <fcgio.h>
#include <unistd.h>
#include <thread>


/*
A few benchmarks:
SAVE_PNG8:   0.052097
SAVE_PNG32:  0.249503
SAVE_JPEG8:  0.021444 (90%)
SAVE_JPEG32: 0.060772 (90%)
SAVE_JPEG8:  0.021920 (100%)
SAVE_JPEG32: 0.060187 (100%)

Sizes:
JPEG8:  200526 (100%)
PNG8:   159504
JPEG8:  124698 (95%)
JPEG8:   92284 (90%)

PNG32:  366925
JPEG32: 308065 (100%)
JPEG32: 168333 (95%)
JPEG32: 120703 (90%)
*/

/**
 * A thread function that handles fcgi request
 */
void fcgiThread(int fd) {
	FCGX_Init();

	FCGX_Request request;

	FCGX_InitRequest(&request, fd, 0);

	while (FCGX_Accept_r(&request) == 0) {
		fcgi_streambuf streambuf_in(request.in);
		fcgi_streambuf streambuf_out(request.out);
		fcgi_streambuf streambuf_err(request.err);

		HTTPService::run(&streambuf_in, &streambuf_out, &streambuf_err, request);
	}
}

int main() {
	Configuration::loadFromDefaultPaths();
	Log::off();

	/*
	 * Initialize Cache
	 */
	bool cache_enabled = Configuration::get<bool>("cache.enabled",false);

	std::unique_ptr<CacheManager> cm;

	// Plug in Cache-Dummy if cache is disabled
	if ( !cache_enabled ) {
		cm = make_unique<NopCacheManager>();
	}
	else {
		std::string cacheType = Configuration::get<std::string>("cache.type");

		if(cacheType == "local") {
			cm = make_unique<LocalCacheManager>(Configuration::get<std::string>("cache.strategy"), Configuration::get<std::string>("cache.replacement"),
					Configuration::get<int>("cache.raster.size"),
					Configuration::get<int>("cache.points.size"),
					Configuration::get<int>("cache.lines.size"),
					Configuration::get<int>("cache.polygons.size"),
					Configuration::get<int>("cache.plots.size"),
					Configuration::get<int>("cache.provenance.size")
			);
		} else if(cacheType == "remote") {
			std::string host = Configuration::get<std::string>("indexserver.host");
			int port = Configuration::get<int>("indexserver.port");
			cm = make_unique<ClientCacheManager>(host,port);
		} else {
			throw ArgumentException("Invalid cache.type");
		}
	}
	CacheManager::init( cm.get() );

	/*
	 * Initialize UserDB
	 */
	UserDB::initFromConfiguration();

	/**
	 * Initialize FeatureCollectionDB
	 */
	FeatureCollectionDB::initFromConfiguration();


	if (getenv("FCGI_WEB_SERVER_ADDRS") == nullptr) {
		// CGI mode
		HTTPService::run(std::cin.rdbuf(), std::cout.rdbuf(), std::cerr.rdbuf());
	}
	else {
		// FCGI mode
		// save stdin fd because of OpenCL Bug
		int fd = dup(0);

		size_t numberOfThreads = Configuration::get<size_t>("fcgi.threads", 1);

		// spawn threads
		std::vector<std::thread> threads;
		for(size_t i = 0; i < numberOfThreads; ++i) {
			threads.emplace_back(fcgiThread, fd);
		}

		// wait for finish
		for(size_t i = 0; i < numberOfThreads; ++i) {
			threads[i].join();
		}
	}
}
