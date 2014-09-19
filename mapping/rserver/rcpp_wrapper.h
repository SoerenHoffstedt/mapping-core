

#ifndef ***REMOVED***_hpp

namespace ***REMOVED*** {
	// QueryRectangle
	template<> SEXP wrap(const QueryRectangle &rect);
	template<> QueryRectangle as(SEXP sexp);

	// Raster
	template<> SEXP wrap(const GenericRaster &raster);
	template<> SEXP wrap(const std::unique_ptr<GenericRaster> &raster);
	template<> std::unique_ptr<GenericRaster> as(SEXP sexp);

	// PointCollection
	template<> SEXP wrap(const PointCollection &points);
	template<> SEXP wrap(const std::unique_ptr<PointCollection> &points);
	//template<> std::unique_ptr<PointCollection> as(SEXP sexp); // TODO

}

#else

namespace ***REMOVED*** {
	// QueryRectangle
	template<> SEXP wrap(const QueryRectangle &rect) {
		Profiler::Profiler p("***REMOVED***: wrapping qrect");
		***REMOVED***::List list;

		list["timestamp"] = rect.timestamp;
		list["x1"] = rect.x1;
		list["y1"] = rect.y1;
		list["x2"] = rect.x2;
		list["y2"] = rect.y2;
		list["xres"] = rect.xres;
		list["yres"] = rect.xres;
		list["epsg"] = rect.epsg;

		return ***REMOVED***::wrap(list);
	}
	template<> QueryRectangle as(SEXP sexp) {
		Profiler::Profiler p("***REMOVED***: unwrapping qrect");
		***REMOVED***::List list = ***REMOVED***::as<***REMOVED***::List>(sexp);

		return QueryRectangle(
			list["timestamp"],
			list["x1"],
			list["y1"],
			list["x2"],
			list["y2"],
			list["xres"],
			list["yres"],
			list["epsg"]
		);
	}

	// Raster
	template<> SEXP wrap(const GenericRaster &raster) {
		/*
		class	   : RasterLayer
		dimensions  : 180, 360, 64800  (nrow, ncol, ncell)
		resolution  : 1, 1  (x, y)
		extent	  : -180, 180, -90, 90  (xmin, xmax, ymin, ymax)
		coord. ref. : +proj=longlat +datum=WGS84

		attributes(r)
		$history: list()
		$file: class .RasterFile
		$data: class .SingleLayerData  (hat haveminmax, min, max!)
		$legend: class .RasterLegend
		$title: character(0)
		$extent: class Extent (xmin, xmax, ymin, ymax)
		$rotated: FALSE
		$rotation: class .Rotation
		$ncols: int
		$nrows: int
		$crs: CRS arguments: +proj=longlat +datum=WGS84
		$z: list()
		$class: c("RasterLayer", "raster")

		 */
		Profiler::Profiler p("***REMOVED***: wrapping raster");
		int width = raster.lcrs.size[0];
		int height = raster.lcrs.size[1];

		***REMOVED***::NumericVector pixels(raster.lcrs.getPixelCount());
		int pos = 0;
		for (int y=0;y<height;y++) {
			for (int x=0;x<width;x++) {
				double val = raster.getAsDouble(x, y);
				if (raster.dd.is_no_data(val))
					pixels[pos++] = NAN;
				else
					pixels[pos++] = val;
			}
		}

		***REMOVED***::S4 data(".SingleLayerData");
		data.slot("values") = pixels;
		data.slot("inmemory") = true;
		data.slot("fromdisk") = false;
		data.slot("haveminmax") = true;
		data.slot("min") = raster.dd.min;
		data.slot("max") = raster.dd.max;

		***REMOVED***::S4 extent("Extent");
		extent.slot("xmin") = raster.lcrs.origin[0];
		extent.slot("ymin") = raster.lcrs.origin[1];
		extent.slot("xmax") = raster.lcrs.PixelToWorldX(raster.lcrs.size[0]);
		extent.slot("ymax") = raster.lcrs.PixelToWorldY(raster.lcrs.size[1]);

		***REMOVED***::S4 crs("CRS");
		std::ostringstream epsg;
		epsg << "EPSG:" << raster.lcrs.epsg;
		crs.slot("projargs") = epsg.str();

		***REMOVED***::S4 rasterlayer("RasterLayer");
		rasterlayer.slot("data") = data;
		rasterlayer.slot("extent") = extent;
		rasterlayer.slot("crs") = crs;
		rasterlayer.slot("ncols") = raster.lcrs.size[0];
		rasterlayer.slot("nrows") = raster.lcrs.size[1];

		return ***REMOVED***::wrap(rasterlayer);
	}
	template<> SEXP wrap(const std::unique_ptr<GenericRaster> &raster) {
		return ***REMOVED***::wrap(*raster);
	}
	template<> std::unique_ptr<GenericRaster> as(SEXP sexp) {
		Profiler::Profiler p("***REMOVED***: unwrapping raster");
		***REMOVED***::S4 rasterlayer(sexp);
		if (!rasterlayer.is("RasterLayer"))
			throw OperatorException("R: Result is not a RasterLayer");

		int width = rasterlayer.slot("ncols");
		int height = rasterlayer.slot("nrows");

		***REMOVED***::S4 crs = rasterlayer.slot("crs");
		std::string epsg_string = crs.slot("projargs");
		epsg_t epsg = EPSG_UNKNOWN;
		if (epsg_string.compare(0,5,"EPSG:") == 0)
			epsg = std::stoi(epsg_string.substr(5, std::string::npos));
		if (epsg == EPSG_UNKNOWN)
			throw OperatorException("R: result raster has no projection of form EPSG:1234 set");

		***REMOVED***::S4 extent = rasterlayer.slot("extent");
		double xmin = extent.slot("xmin"), ymin = extent.slot("ymin"), xmax = extent.slot("xmax"), ymax = extent.slot("ymax");

		LocalCRS lcrs(epsg, width, height, xmin, ymin, (xmax-xmin)/width, (ymax-ymin)/height);

		***REMOVED***::S4 data = rasterlayer.slot("data");
		if ((bool) data.slot("inmemory") != true)
			throw OperatorException("R: result raster not inmemory");
		if ((bool) data.slot("haveminmax") != true)
			throw OperatorException("R: result raster does not have min/max");

		double min = data.slot("min");
		double max = data.slot("max");

		DataDescription dd(GDT_Float32, min, max, true, NAN);

		lcrs.verify();
		dd.verify();
		auto raster_out = GenericRaster::create(lcrs, dd, GenericRaster::Representation::CPU);
		Raster2D<float> *raster2d = (Raster2D<float> *) raster_out.get();

		***REMOVED***::NumericVector pixels = data.slot("values");
		int pos = 0;
		for (int y=0;y<height;y++) {
			for (int x=0;x<width;x++) {
				float val = pixels[pos++];
				raster2d->set(x, y, val);
			}
		}
		return raster_out;
	}


	// PointCollection
	template<> SEXP wrap(const PointCollection &points) {
		/*
		new("SpatialPointsDataFrame"
			, data = structure(list(), .Names = character(0), row.names = integer(0), class = "data.frame")
			, coords.nrs = numeric(0)
			, coords = structure(NA, .Dim = c(1L, 1L))
			, bbox = structure(NA, .Dim = c(1L, 1L))
			, proj4string = new("CRS"
			, projargs = NA_character_
		)
		*/
		Profiler::Profiler p("***REMOVED***: wrapping pointcollection");
		***REMOVED***::S4 SPDF("SpatialPointsDataFrame");

		auto size = points.collection.size();

		***REMOVED***::DataFrame data;
		auto keys = points.getLocalMDValueKeys();
		for(auto key : keys) {
			***REMOVED***::NumericVector vec(size);
			for (decltype(size) i=0;i<size;i++) {
				const Point &p = points.collection[i];
				double value = points.getLocalMDValue(p, key);
				vec[i] = value;
			}
			data[key] = vec;
		}

		***REMOVED***::NumericMatrix coords(size, 2);
		for (decltype(size) i=0;i<size;i++) {
			const Point &p = points.collection[i];
			coords(i, 0) = p.x;
			coords(i, 1) = p.y;
		}

		***REMOVED***::NumericMatrix bbox(2,2); // TODO ?

		***REMOVED***::S4 crs("CRS");
		std::ostringstream epsg;
		epsg << "EPSG:" << points.epsg;
		crs.slot("projargs") = epsg.str();


		SPDF.slot("data") = data;
		SPDF.slot("coords.nrs") = true;
		SPDF.slot("coords") = coords;
		SPDF.slot("bbox") = bbox;
		SPDF.slot("proj4string") = crs;

		return SPDF;
	}
	template<> SEXP wrap(const std::unique_ptr<PointCollection> &points) {
		return ***REMOVED***::wrap(*points);
	}
	//template<> std::unique_ptr<PointCollection> as(SEXP sexp) {
	//}

}

#endif