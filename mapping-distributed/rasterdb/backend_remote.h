#ifndef RASTERDB_BACKEND_REMOTE_H
#define RASTERDB_BACKEND_REMOTE_H

#include "rasterdb/backend.h"
#include "util/sqlite.h"
#include "util/binarystream.h"

#include <string>
#include <memory>

class RemoteRasterDBBackend : public RasterDBBackend {
	public:
		RemoteRasterDBBackend(const std::string &location);
		virtual ~RemoteRasterDBBackend();

		static const uint8_t COMMAND_EXIT = 1;
		static const uint8_t COMMAND_ENUMERATESOURCES = 2;
		static const uint8_t COMMAND_READANYJSON = 3;

		static const uint8_t COMMAND_OPEN = 9;
		static const uint8_t FIRST_SOURCE_SPECIFIC_COMMAND = 10;

		static const uint8_t COMMAND_READJSON = 10;
		static const uint8_t COMMAND_CREATERASTER = 11;
		static const uint8_t COMMAND_WRITETILE = 12;

		static const uint8_t COMMAND_GETCLOSESTRASTER = 13;
		static const uint8_t COMMAND_READATTRIBUTES = 14;
		static const uint8_t COMMAND_GETBESTZOOM = 15;
		static const uint8_t COMMAND_ENUMERATETILES = 16;
		static const uint8_t COMMAND_HASTILE = 17;
		static const uint8_t COMMAND_READTILE = 18;

		virtual std::vector<std::string> enumerateSources();
		virtual std::string readJSON(const std::string &sourcename);

		virtual void open(const std::string &sourcename, bool writeable);
		virtual std::string readJSON();

		virtual RasterDescription getClosestRaster(int channelid, double t1, double t2);
		virtual void readAttributes(rasterid_t rasterid, AttributeMaps &attributes);
		virtual int getBestZoom(rasterid_t rasterid, int desiredzoom);
		virtual const std::vector<TileDescription> enumerateTiles(int channelid, rasterid_t rasterid, int x1, int y1, int x2, int y2, int zoom = 0);
		virtual bool hasTile(rasterid_t rasterid, uint32_t width, uint32_t height, uint32_t depth, int offx, int offy, int offz, int zoom);
		virtual std::unique_ptr<ByteBuffer> readTile(const TileDescription &tiledesc);

	private:
		void init();

		BinaryStream stream;
		std::string sourcename;
		std::string cache_directory;
		std::string remote_host;
		std::string remote_port;
		std::string json;
};

#endif