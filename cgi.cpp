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



int main() {
	Configuration::loadFromDefaultPaths();
	Log::off();

	/*
	 * Initialize Cache
	 */
	bool cache_enabled = Configuration::getBool("cache.enabled",false);

	std::unique_ptr<CacheManager> cm;

	// Plug in Cache-Dummy if cache is disabled
	if ( !cache_enabled ) {
		cm = make_unique<NopCacheManager>();
	}
	else {
		cm = make_unique<LocalCacheManager>("always", "lru",
				Configuration::getInt("nodeserver.cache.raster.size"),
				Configuration::getInt("nodeserver.cache.points.size"),
				Configuration::getInt("nodeserver.cache.lines.size"),
				Configuration::getInt("nodeserver.cache.polygons.size"),
				Configuration::getInt("nodeserver.cache.plots.size"));
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
		FCGX_Request request;
		int res;
		res = FCGX_Init();
		if (res != 0)
			throw std::runtime_error("FCGX_Init failed");
		res = FCGX_InitRequest(&request, 0, 0);
		if (res != 0)
			throw std::runtime_error("FCGX_InitRequest failed");

		while (FCGX_Accept_r(&request) == 0) {
			// save stdin, because Intel OpenCL driver closes it
			int stdin = dup(0);
			
			fcgi_streambuf streambuf_in(request.in);
			fcgi_streambuf streambuf_out(request.out);
			fcgi_streambuf streambuf_err(request.err);

			HTTPService::run(&streambuf_in, &streambuf_out, &streambuf_err, request);
			
			// restore stdin
			dup2(stdin, 0);
		}
	}
}
