/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C) 2007-2008  Joseph Artsimovich <joseph_a@mail.ru>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ContentBoxFinder.h"
#include "TaskStatus.h"
#include "DebugImages.h"
#include "FilterData.h"
#include "ImageTransformation.h"
#include "ContentSpanFinder.h"
#include "Span.h"
#include "Dpi.h"
#include "imageproc/BinaryImage.h"
#include "imageproc/BinaryThreshold.h"
#include "imageproc/BWColor.h"
#include "imageproc/Constants.h"
#include "imageproc/Connectivity.h"
#include "imageproc/ConnComp.h"
#include "imageproc/ConnCompEraserExt.h"
#include "imageproc/Transform.h"
#include "imageproc/RasterOp.h"
#include "imageproc/SeedFill.h"
#include "imageproc/Morphology.h"
#include "imageproc/Grayscale.h"
#include "imageproc/SlicedHistogram.h"
#include "imageproc/DentFinder.h"
#include <boost/lambda/lambda.hpp>
#include <boost/lambda/bind.hpp>
#include <QRect>
#include <QRectF>
#include <QImage>
#include <QColor>
#include <QPainter>
#include <QTransform>
#include <deque>
#include <algorithm>

namespace select_content
{

using namespace imageproc;

static double const LIGHTER_REDUCE_FACTOR = 1.7;
static double const LIGHTER_REDUCE_FACTOR_SAFE = LIGHTER_REDUCE_FACTOR - 0.2;

QRectF
ContentBoxFinder::findContentBox(
	TaskStatus const& status, FilterData const& data, DebugImages* dbg)
{
	ImageTransformation xform_150dpi(data.xform());
	xform_150dpi.preScaleToDpi(Dpi(150, 150));
#if 0
	QTransform const scale_to_150(transformTo150DPI(data.image()));
	
	ImageTransformation const scaled_xform(
		data.xform().recalculatedForScaledInput(
			scale_to_150.mapRect(data.xform().origRect())
		)
	);

	QTransform const resulting_xform(scale_to_150 * scaled_xform.transform());
#endif	
	QColor const black(0x00, 0x00, 0x00);
	QImage const gray150(
		transformToGray(
			data.image(), xform_150dpi.transform(),
			xform_150dpi.resultingRect().toRect(), black
		)
	);
	// Note that we fill new areas that appear as a result of
	// rotation with black, not white.  Filling them with white
	// may be bad for detecting the shadow around the page.
	if (dbg) {
		dbg->add(gray150, "gray150");
	}
	
	status.throwIfCancelled();
	
	BinaryImage bw150(gray150, data.bwThreshold());
	if (dbg) {
		dbg->add(bw150, "bw150");
	}
	
	status.throwIfCancelled();
	
	BinaryImage hor_shadows_seed(openBrick(bw150, QSize(200, 14), BLACK));
	if (dbg) {
		dbg->add(hor_shadows_seed, "hor_shadows_seed");
	}
	
	status.throwIfCancelled();
	
	BinaryImage ver_shadows_seed(openBrick(bw150, QSize(14, 300), BLACK));
	if (dbg) {
		dbg->add(ver_shadows_seed, "ver_shadows_seed");
	}
	
	status.throwIfCancelled();
	
	rasterOp<RopOr<RopSrc, RopDst> >(hor_shadows_seed, ver_shadows_seed);
	BinaryImage shadows_seed(hor_shadows_seed.release());
	ver_shadows_seed.release();
	if (dbg) {
		dbg->add(shadows_seed, "shadows_seed");
	}
	
	status.throwIfCancelled();
	
	BinaryImage dilated(dilateBrick(bw150, QSize(3, 3)));
	if (dbg) {
		dbg->add(dilated, "dilated");
	}
	
	status.throwIfCancelled();
	
	BinaryImage shadows_dilated(seedFill(shadows_seed, dilated, CONN8));
	dilated.release();
	if (dbg) {
		dbg->add(shadows_dilated, "shadows_dilated");
	}
	
	status.throwIfCancelled();
	
	rasterOp<RopAnd<RopSrc, RopDst> >(shadows_dilated, bw150);
	BinaryImage shadows(shadows_dilated.release());
	if (dbg) {
		dbg->add(shadows, "shadows");
	}
	
	status.throwIfCancelled();
	
	filterShadows(status, shadows, dbg);
	if (dbg) {
		dbg->add(shadows, "filtered_shadows");
	}
	
	status.throwIfCancelled();
	
	BinaryImage content(bw150.release());
	rasterOp<RopSubtract<RopDst, RopSrc> >(content, shadows);
	shadows.release();
	if (dbg) {
		dbg->add(content, "content");
	}
	
	status.throwIfCancelled();
	
	BinaryImage content_lighter(
		getLighterContent(gray150, data.bwThreshold(), content)
	);
	if (dbg) {
		dbg->add(content_lighter, "content_lighter");
	}
	
	status.throwIfCancelled();
	
	if (dbg) {
		BinaryImage garbage(content.size(), WHITE);
		BinaryImage non_garbage(content.size(), WHITE);
		BinaryImage big_dark(content.size(), WHITE);
		
		ConnCompEraserExt eraser(content, CONN8);
		for (ConnComp cc; !(cc = eraser.nextConnComp()).isNull(); ) {
			if (cc.pixCount() < 10 || (cc.width() < 5 && cc.height() < 5)) {
				// Too small.  Probably garbage.
				continue;
			}
			
			BinaryImage const cc_img(eraser.computeConnCompImage());
			
			if (isBigAndDark(cc)) {
				rasterOp<RopOr<RopSrc, RopDst> >(big_dark, cc.rect(), cc_img, QPoint(0, 0));
			}
			
			if (!isWindingComponent(cc_img)) {
				// The connected component doesn't wind that much.
				// It must be a blot or a stroke. We consider such things as garbage.
				
				rasterOp<RopOr<RopSrc, RopDst> >(garbage, cc.rect(), cc_img, QPoint(0, 0));
				continue;
			}
			
			rasterOp<RopOr<RopSrc, RopDst> >(non_garbage, cc.rect(), cc_img, QPoint(0, 0));
		}
		
		QImage garbage_red(garbage.size(), QImage::Format_ARGB32_Premultiplied);
		garbage_red.fill(qRgb(0xff, 0x00, 0x00));
		garbage_red.setAlphaChannel(garbage.inverted().toQImage());
		
		QImage non_garbage_green(non_garbage.size(), QImage::Format_ARGB32_Premultiplied);
		non_garbage_green.fill(qRgb(0x00, 0xff, 0x00));
		non_garbage_green.setAlphaChannel(non_garbage.inverted().toQImage());
		
		QImage dent_stats(content.size(), QImage::Format_ARGB32_Premultiplied);
		dent_stats.fill(qRgb(0xff, 0xff, 0xff));
		
		{
			QPainter painter(&dent_stats);
			painter.drawImage(QPoint(0, 0), garbage_red);
			painter.drawImage(QPoint(0, 0), non_garbage_green);
		}
		dbg->add(dent_stats, "dent_stats");
		dbg->add(big_dark, "big_dark");
	}
	
	QRect content_rect(content.rect());
	
	for (bool first = true;; first = false) {
		status.throwIfCancelled();
		
		QRect old_content_rect(content_rect);
		content_rect = trimLeftRight(content, content_lighter, content_rect);
		if (content_rect.isEmpty()) {
			break;
		}
		if (!first && content_rect == old_content_rect) {
			break;
		}
		
		old_content_rect = content_rect;
		content_rect = trimTopBottom(content, content_lighter, content_rect);
		if (content_rect.isEmpty()) {
			break;
		}
		if (content_rect == old_content_rect) {
			break;
		}
	}
	
	status.throwIfCancelled();
#if 0
	// Map from pre-scaled-then-transformed coordinates
	// to physical image coordinates.
	QPolygonF const phys_coord_box(
		resulting_xform.inverted().map(QRectF(content_rect))
	);
	
	// Map from physical image coordinates to
	// transformed-without-pre-scaling coordinates.
	QPolygonF const virt_coord_box(
		data.xform().transform().map(phys_coord_box)
	);
	
	return virt_coord_box.boundingRect();
#endif
	// Transform back from 150dpi.
	QTransform combined_xform(xform_150dpi.transform().inverted());
	combined_xform *= data.xform().transform();
	return combined_xform.map(QRectF(content_rect)).boundingRect();
}

QTransform
ContentBoxFinder::transformTo150DPI(QImage const& image)
{
	double const xfactor = (150.0 * constants::DPI2DPM) / image.dotsPerMeterX();
	double const yfactor = (150.0 * constants::DPI2DPM) / image.dotsPerMeterY();
	
	QTransform xform;
	xform.scale(xfactor, yfactor);
	
	return xform;
}

void
ContentBoxFinder::filterShadows(
	TaskStatus const& status, imageproc::BinaryImage& shadows, DebugImages* dbg)
{
	// The input image should only contain shadows from the edges
	// of a page, but in practice it may also contain things like
	// a black table header which white letters on it.  Here we
	// try to filter them out.
	
	BinaryImage opened(openBrick(shadows, QSize(10, 4), BLACK));
	if (dbg) {
		dbg->add(opened, "opened");
	}
	
	status.throwIfCancelled();
	
	rasterOp<RopSubtract<RopNot<RopDst>, RopNot<RopSrc> > >(opened, shadows);
	if (dbg) {
		dbg->add(opened, "became white");
	}
	
	status.throwIfCancelled();
	
	BinaryImage closed(closeBrick(opened, QSize(20, 1), WHITE));
	opened.release();
	if (dbg) {
		dbg->add(closed, "closed");
	}
	
	status.throwIfCancelled();
	
	opened = openBrick(closed, QSize(50, 10), WHITE);
	closed.release();
	if (dbg) {
		dbg->add(opened, "reopened");
	}
	
	status.throwIfCancelled();
	
	BinaryImage non_shadows(seedFill(opened, shadows, CONN8));
	opened.release();
	if (dbg) {
		dbg->add(non_shadows, "non_shadows");
	}
	
	status.throwIfCancelled();
	
	rasterOp<RopSubtract<RopDst, RopSrc> >(shadows, non_shadows);
}

BinaryImage
ContentBoxFinder::getLighterContent(
	QImage const& gray, BinaryThreshold const reference_threshold,
	BinaryImage const& content_mask)
{
	GrayscaleHistogram const hist(gray, content_mask);
	int num_black_reference = 0;
	for (int i = 0; i < reference_threshold; ++i) {
		num_black_reference += hist[i];
	}
	
	int const max_threshold_delta = 20;
	int new_threshold = std::max(0, reference_threshold - max_threshold_delta);
	int num_black_delta = 0;
	for (int i = new_threshold; i < reference_threshold; ++i) {
		num_black_delta += hist[i];
	}
	
	for (; new_threshold < reference_threshold;
			++new_threshold, num_black_delta -= hist[new_threshold]) {
		int const num_black_new = num_black_reference - num_black_delta;
		if (num_black_reference < num_black_new * LIGHTER_REDUCE_FACTOR_SAFE) {
			break;
		}
	}
	
	BinaryImage content_lighter(gray, BinaryThreshold(new_threshold));
	rasterOp<RopAnd<RopSrc, RopDst> >(content_lighter, content_mask);
	return content_lighter;
}

bool
ContentBoxFinder::isBigAndDark(ConnComp const& cc)
{
	// We are working with 150 dpi images here.
	
	int const min_side = std::min(cc.width(), cc.height());
	if (min_side < 15) {
		return false;
	}
	
	int const square = cc.width() * cc.height();
	
	if (square < 100 * 30) {
		return false;
	}
	
	if (cc.pixCount() < square * 0.3) {
		return false;
	}
	
	return true;
}

bool
ContentBoxFinder::isWindingComponent(imageproc::BinaryImage const& cc_img)
{
	BinaryImage const dents(DentFinder::findDentsAndHoles(cc_img));
	
	int const w = cc_img.width();
	int const h = cc_img.height();
	int const cc_diagonal_squared = w * w + h * h;
	
	return (dents.countBlackPixels() >= cc_diagonal_squared * 0.04);
}

QRect
ContentBoxFinder::trimLeftRight(
	BinaryImage const& img, BinaryImage const& img_lighter, QRect const& area)
{
	using namespace boost::lambda;
	
	SlicedHistogram const hist(img, area, SlicedHistogram::COLS);
	
	ContentSpanFinder span_finder;
	span_finder.setMinContentWidth(10);
	
	// This should be more than the space between letters and less
	// than the space between content and the folding area.  The latter
	// is more important.
	span_finder.setMinWhitespaceWidth(7);
	
	typedef std::deque<Span> SpanList;
	SpanList spans;
	span_finder.find(
		hist,
		bind(&SpanList::push_back, var(spans), ret<Span>(_1 + area.left()))
	);
	
	// Go from top to bottom spans, removing garbage.
	for (; !spans.empty(); spans.pop_front()) {
		Span& span = spans.front();
		QRect span_area(area);
		span_area.setLeft(span.begin());
		span_area.setRight(span.end() - 1);
		QRect const new_area = processColumn(img, img_lighter, span_area);
		if (!new_area.isEmpty()) {
			span = Span(new_area.left(), new_area.right() + 1);
			break;
		}
	}
	
	// Go from bottom to top spans, removing garbage.
	for (; !spans.empty(); spans.pop_back()) {
		Span& span = spans.back();
		QRect span_area(area);
		span_area.setLeft(span.begin());
		span_area.setRight(span.end() - 1);
		QRect const new_area = processColumn(img, img_lighter, span_area);
		if (!new_area.isEmpty()) {
			span = Span(new_area.left(), new_area.right() + 1);
			break;
		}
	}
	
	if (spans.empty()) {
		return QRect();
	}
	
	QRect new_area(area);
	new_area.setLeft(spans.front().begin());
	new_area.setRight(spans.back().end() - 1);
	return new_area;
}

QRect
ContentBoxFinder::trimTopBottom(
	BinaryImage const& img, BinaryImage const& img_lighter, QRect const& area)
{
	using namespace boost::lambda;
	
	SlicedHistogram const hist(img, area, SlicedHistogram::ROWS);
	
	ContentSpanFinder span_finder;
	
	// Reduced because there may be a horizontal line at the top.
	span_finder.setMinContentWidth(5);
	
	span_finder.setMinWhitespaceWidth(10);
	
	typedef std::deque<Span> SpanList;
	SpanList spans;
	span_finder.find(
		hist,
		bind(&SpanList::push_back, var(spans), ret<Span>(_1 + area.top()))
	);
	
	// Go from top to bottom spans, removing garbage.
	for (; !spans.empty(); spans.pop_front()) {
		Span& span = spans.front();
		QRect span_area(area);
		span_area.setTop(span.begin());
		span_area.setBottom(span.end() - 1);
		QRect const new_area = processRow(img, img_lighter, span_area);
		if (!new_area.isEmpty()) {
			span = Span(new_area.top(), new_area.bottom() + 1);
			break;
		}
	}
	
	// Go from bottom to top spans, removing garbage.
	for (; !spans.empty(); spans.pop_back()) {
		Span& span = spans.back();
		QRect span_area(area);
		span_area.setTop(span.begin());
		span_area.setBottom(span.end() - 1);
		QRect const new_area = processRow(img, img_lighter, span_area);
		if (!new_area.isEmpty()) {
			span = Span(new_area.top(), new_area.bottom() + 1);
			break;
		}
	}
	
	if (spans.empty()) {
		return QRect();
	}
	
	QRect new_area(area);
	new_area.setTop(spans.front().begin());
	new_area.setBottom(spans.back().end() - 1);
	return new_area;
}

QRect
ContentBoxFinder::processColumn(
	BinaryImage const& img, BinaryImage const& img_lighter, QRect const& area)
{
	if (area.width() < 8) {
		return QRect();
	}
	
#if 0
	// This prevents us from removing garbage on the sides of a content region.
	if (area.width() > 45) {
		return area;
	}
#endif
	
	int const total_black_pixels = img.countBlackPixels(area);
	
	if (total_black_pixels > img_lighter.countBlackPixels(area) * LIGHTER_REDUCE_FACTOR) {
		/*
		Two possibilities here:
		1. We are dealing with a gradient, most likely the shadow
		from the folding area.
		2. We are dealing with content of slightly lighter color
		than the threshold gray level.  This could be a pencil
		stroke or some other garbage.
		*/
		return QRect();
	}
	
	BinaryImage region(area.size());
	rasterOp<RopSrc>(region, region.rect(), img, area.topLeft());
	
	int non_garbage_pixels = 0;
	int left = area.right();
	int right = area.left();
	
	ConnCompEraserExt eraser(region.release(), CONN8);
	for (ConnComp cc; !(cc = eraser.nextConnComp()).isNull(); ) {
		if (cc.pixCount() < 10 || (cc.width() < 5 && cc.height() < 5)) {
			// Too small, probably garbage.
			continue;
		}
		
		left = std::min(left, area.left() + cc.rect().left());
		right = std::max(right, area.left() + cc.rect().right());
		
		BinaryImage const cc_img(eraser.computeConnCompImage());
		
		if (!isWindingComponent(cc_img)) {
			// This connected component doesn't wind that much.
			// It must be a speckle or a stroke.  Actually, it
			// can also be one of the following symbols:
			// I, 1, l, /, \, ...
			// Usually, misclassifying one or two symbols won't
			// classify the whole region as garbage, but it may
			// shrink the region so that these symbols are out
			// of it.  To prevent that, we update left and right
			// anyway (see above), but we don't update non_garbage_pixels.
			// If it's too low, left and right won't matter.
			continue;
		}
		
		non_garbage_pixels += cc.pixCount();
	}
	
	if (left > right || non_garbage_pixels <= total_black_pixels * 0.3) {
		return QRect();
	} else {
		QRect new_area(area);
		new_area.setLeft(left);
		new_area.setRight(right);
		return new_area;
	}
}

QRect
ContentBoxFinder::processRow(
	BinaryImage const& img, BinaryImage const& img_lighter, QRect const& area)
{
#if 0
	// This prevents us from removing garbage on the sides of a content region.
	if (area.height() > 45) {
		return area;
	}
#endif
	
	int const total_black_pixels = img.countBlackPixels(area);
	int const lighter_black_pixels = img_lighter.countBlackPixels(area);
	
	if (total_black_pixels > lighter_black_pixels * LIGHTER_REDUCE_FACTOR) {
		// Here we have a different situation compared to processColumn().
		// On one hand, we are not going to encounter a shadow from
		// the folding area, but on the other hand we may easily
		// hit pictures, dithered table headers, etc.
		// Still, we'd like to remove pencil strokes and things like that.
		if (lighter_black_pixels < 5) {
			return QRect();
		}
	}
	
	BinaryImage region(area.size());
	rasterOp<RopSrc>(region, region.rect(), img, area.topLeft());
	
	int non_garbage_pixels = 0;
	int top = area.bottom();
	int bottom = area.top();
	
	ConnCompEraserExt eraser(region.release(), CONN8);
	for (ConnComp cc; !(cc = eraser.nextConnComp()).isNull(); ) {
		if (cc.pixCount() < 10 || (cc.width() < 5 && cc.height() < 5)) {
			// Too small, probably garbage.
			continue;
		}
		
		bool const long_hline = (
			(cc.width() > area.width() * 0.8)
			&& (cc.width() > cc.height() * 20)
		);
		
		bool const short_vline = (
			(cc.height() > cc.width() * 2)
			&& (cc.height() < cc.width() * 15)
		);
		
		bool const big_and_dark = isBigAndDark(cc);
		
		if (!(short_vline || long_hline || big_and_dark)) {
			BinaryImage const cc_img(eraser.computeConnCompImage());
			
			if (!isWindingComponent(cc_img)) {
				// This connected component doesn't wind that much.
				// It must be a speckle or a stroke.  Actually, it
				// can also be one of the following symbols:
				// I, 1, l, /, \, ...
				// We don't really care about most of them, except
				// for '1', because page numbers such as 1, 11, 111
				// may be misclassified as garbage.  That's why we
				// protect components with high height / width ratio.
				// The enclosing 'if' provides protection.
				continue;
			}
		}
		
		top = std::min(top, area.top() + cc.rect().top());
		bottom = std::max(bottom, area.top() + cc.rect().bottom());
		non_garbage_pixels += cc.pixCount();
	}
	
	if (top > bottom || non_garbage_pixels <= total_black_pixels * 0.3) {
		return QRect();
	} else {
		QRect new_area(area);
		new_area.setTop(top);
		new_area.setBottom(bottom);
		return new_area;
	}
}

} // namespace select_content
