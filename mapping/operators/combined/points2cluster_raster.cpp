
#include "raster/raster.h"
#include "raster/raster_priv.h"
#include "raster/pointcollection.h"
#include "raster/typejuggling.h"
#include "operators/operator.h"
#include "pointvisualization/CircleClusteringQuadTree.h"

#include <memory>
#include <cmath>
#include <algorithm>

class PointsToClusterRasterOperator : public GenericOperator {
	public:
	PointsToClusterRasterOperator(int sourcecount, GenericOperator *sources[], Json::Value &params);
		virtual ~PointsToClusterRasterOperator();

		virtual std::unique_ptr<GenericRaster> getRaster(const QueryRectangle &rect);
};




PointsToClusterRasterOperator::PointsToClusterRasterOperator(int sourcecount, GenericOperator *sources[], Json::Value &params) : GenericOperator(Type::RASTER, sourcecount, sources) {
	assumeSources(1);
}

PointsToClusterRasterOperator::~PointsToClusterRasterOperator() {
}
REGISTER_OPERATOR(PointsToClusterRasterOperator, "points2cluster_raster");


std::unique_ptr<GenericRaster> PointsToClusterRasterOperator::getRaster(const QueryRectangle &rect) {

	const int MAX = 255;
	typedef uint8_t T;

	std::unique_ptr<PointCollection> points = sources[0]->getPoints(rect);

	LocalCRS rm(rect);
	DataDescription vm(GDT_Byte, 0, MAX, true, 0);

	pv::CircleClusteringQuadTree clusterer(pv::BoundingBox(pv::Coordinate((rect.x2 + rect.x1) / 2, (rect.y2 + rect.y2) / 2), pv::Dimension((rect.x2 - rect.x1) / 2, (rect.y2 - rect.y2) / 2), 1), 1);
	for (Point &p : points->collection) {
		double x = p.x, y = p.y;

		int px = floor(rm.WorldToPixelX(x));
		int py = floor(rm.WorldToPixelY(y));

		clusterer.insert(std::make_shared<pv::Circle>(pv::Coordinate(px, py), 5, 1));
	}


	std::unique_ptr<GenericRaster> raster_out_guard = GenericRaster::create(rm, vm, GenericRaster::Representation::CPU);
	Raster2D<T> *raster_out = (Raster2D<T> *) raster_out_guard.get();

	raster_out->clear(0);
	for (auto& circle : clusterer.getCircles()) {
		double RADIUS = circle->getRadius();

		for (int dy = -RADIUS;dy <= RADIUS;dy++)
			for (int dx = -RADIUS;dx <= RADIUS;dx++) {
				double delta = RADIUS-std::sqrt(dx*dx + dy*dy);
				if (delta <= 0)
					continue;
				raster_out->setSafe(circle->getX()+dx, circle->getY()+dy, std::min(circle->getNumberOfPoints(), MAX));
			}
	}

	return raster_out_guard;
}
