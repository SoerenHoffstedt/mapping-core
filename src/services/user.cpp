
#include "services/httpservice.h"
#include "userdb/userdb.h"
#include "util/configuration.h"

#include "rasterdb/rasterdb.h"

#include <json/json.h>
#include <util/gdal_source_datasets.h>
#include <util/ogr_source_datasets.h>

/**
 * This class provides user specific methods
 *
 * Operations:
 * - request = login: login with credential, returns sessiontoken
 *   - parameters:
 *     - username
 *     - password
 * - request = logout: destroys current session
 * - request = sourcelist: get list of available raster sources
 * - request = info: get user information
 *
 */
class UserService : public HTTPService {
	public:
		using HTTPService::HTTPService;
		virtual ~UserService() = default;
		virtual void run();
};
REGISTER_HTTP_SERVICE(UserService, "USER");



void UserService::run() {
	try {
		std::string request = params.get("request");

		if (request == "login") {
			auto session = UserDB::createSession(params.get("username"), params.get("password"), 8*3600);
			response.sendSuccessJSON("session", session->getSessiontoken());
			return;
		}

		// anything except login is only allowed with a valid session, so check for it.
		auto session = UserDB::loadSession(params.get("sessiontoken"));
		auto user = session->getUser();

		if (request == "logout") {
			session->logout();
			response.sendSuccessJSON();
			return;
		}

		if (request == "sourcelist") {
			Json::Value v(Json::ValueType::objectValue);

			// RasterDB
			auto sourcenames = RasterDB::getSourceNames();
			for (const auto &name : sourcenames) {
				// check permissions: only return rasters the user can access
				if(user.hasPermission("data.rasterdb_source." + name)) {
					auto description = RasterDB::getSourceDescription(name);
					std::istringstream iss(description);
					Json::Reader reader(Json::Features::strictMode());
					Json::Value root;
					if (!reader.parse(iss, root))
						continue;

					v[name] = root;
				}
			}

			// GDALSource
			auto dataSets= GDALSourceDataSets::getDataSetNames();
			for(const auto &dataSet : dataSets) {
                if (user.hasPermission("data.gdal_source." + dataSet)) {
                    auto description = GDALSourceDataSets::getDataSetDescription(dataSet);
                    description["operator"] = "gdal_source";

                    // TODO: resolve name clashes
                    v[dataSet] = description;
                }
            }

			// OGR File Source
			std::vector<std::string> ogr_source_names = OGRSourceDatasets::getDatasetNames();
			for(const auto &name : ogr_source_names){
				if(user.hasPermission("data.ogr_source." + name)){
                    Json::Value description = OGRSourceDatasets::getDatasetListing(name);
                    description["operator"] = "ogr_source";

					// TODO: resolve name clashes
					v[name] = description;
				}
			}

			response.sendSuccessJSON("sourcelist", v);
			return;
		}

		if(request == "info") {
			UserDB::User &user = session->getUser();

			Json::Value json(Json::ValueType::objectValue);
			json["username"] = user.getUsername();
			json["realname"] = user.getRealname();
			json["email"] = user.getEmail();
			json["externalid"] = user.getExternalid();

			response.sendSuccessJSON(json);
			return;
		}

		response.sendFailureJSON("unknown request");
	}
	catch (const std::exception &e) {
		response.sendFailureJSON(e.what());
	}
}
