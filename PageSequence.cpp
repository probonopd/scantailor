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

#include "PageSequence.h.moc"
#include "ImageFileInfo.h"
#include "ImageMetadata.h"
#include "ImageInfo.h"
#include "OrthogonalRotation.h"
#include <QMutexLocker>
#include <QSize>
#include <algorithm>
#include <stddef.h>
#include <assert.h>

PageSequence::PageSequence()
:	m_totalLogicalPages(0),
	m_curImage(0),
	m_curLogicalPage(0),
	m_curSubPage(LogicalPageId::SINGLE_PAGE)
{
}

PageSequence::PageSequence(std::vector<ImageInfo> const& info)
:	m_totalLogicalPages(0),
	m_curImage(0),
	m_curLogicalPage(0),
	m_curSubPage(LogicalPageId::SINGLE_PAGE)
{
	std::vector<ImageInfo>::const_iterator it(info.begin());
	std::vector<ImageInfo>::const_iterator const end(info.end());
	for (; it != end; ++it) {
		ImageInfo const& image = *it;
		m_images.push_back(
			ImageDesc(image.id(), image.metadata(), image.numSubPages())
		);
	}
	
	if (!m_images.empty() && m_images.front().numLogicalPages > 1) {
		m_curSubPage = LogicalPageId::LEFT_PAGE;
	}
}

PageSequence::PageSequence(
	std::vector<ImageFileInfo> const& files, Pages const pages)
:	m_totalLogicalPages(0),
	m_curImage(0),
	m_curLogicalPage(0),
	m_curSubPage(LogicalPageId::SINGLE_PAGE)
{
	std::vector<ImageFileInfo>::const_iterator it(files.begin());
	std::vector<ImageFileInfo>::const_iterator const end(files.end());
	for (; it != end; ++it) {
		ImageFileInfo const& file = *it;
		QString const& file_path = file.fileInfo().absoluteFilePath();
		std::vector<ImageMetadata> const& images = file.imageInfo();
		int const num_images = images.size();
		for (int i = 0; i < num_images; ++i) {
			ImageMetadata const& metadata = images[i];
			ImageId const id(file_path, i);
			m_images.push_back(ImageDesc(id, metadata, pages));
			m_totalLogicalPages += m_images.back().numLogicalPages;
		}
	}
	
	if (!m_images.empty() && m_images.front().numLogicalPages > 1) {
		m_curSubPage = LogicalPageId::LEFT_PAGE;
	}
}

PageSequence::~PageSequence()
{
}

PageSequenceSnapshot
PageSequence::snapshot(View const view) const
{
	std::vector<PageInfo> pages;
	int cur_page;
	
	if (view == PAGE_VIEW) {
		QMutexLocker locker(&m_mutex);
		
		cur_page = m_curLogicalPage;
		pages.reserve(m_totalLogicalPages);
		int const num_images = m_images.size();
		for (int i = 0; i < num_images; ++i) {
			ImageDesc const& image = m_images[i];
			assert(image.numLogicalPages >= 1 && image.numLogicalPages <= 2);
			for (int j = 0; j < image.numLogicalPages; ++j) {
				PageId const id(image.id, image.logicalPageToSubPage(j));
				PageInfo const page(id, image.metadata, image.numLogicalPages);
				pages.push_back(page);
			}
		}
	} else {
		assert(view == IMAGE_VIEW);
		
		QMutexLocker locker(&m_mutex);
		
		cur_page = m_curImage;
		int const num_images = m_images.size();
		pages.reserve(num_images);
		for (int i = 0; i < num_images; ++i) {
			ImageDesc const& image = m_images[i];
			PageId const id(image.id, LogicalPageId::SINGLE_PAGE);
			pages.push_back(PageInfo(id, image.metadata, image.numLogicalPages));
		}
	}
	
	PageSequenceSnapshot snapshot(view);
	pages.swap(snapshot.m_pages);
	snapshot.m_curPageIdx = cur_page;
	
	return snapshot;
}

void
PageSequence::setLogicalPagesInImage(ImageId const& image_id, int const num_pages)
{
	assert(num_pages >= 1 && num_pages <= 2);
	
	bool was_modified = false;
	
	{
		QMutexLocker locker(&m_mutex);
		setLogicalPagesInImageImpl(image_id, num_pages, &was_modified);
	}
	
	if (was_modified) {
		emit modified();
	}
}

void
PageSequence::autoSetLogicalPagesInImage(
	ImageId const& image_id, OrthogonalRotation const rotation)
{
	bool was_modified = false;
	
	{
		QMutexLocker locker(&m_mutex);
		autoSetLogicalPagesInImageImpl(image_id, rotation, &was_modified);
	}
	
	if (was_modified) {
		emit modified();
	}
}

int
PageSequence::adviseNumberOfLogicalPages(
	ImageMetadata const& metadata, OrthogonalRotation const rotation)
{
	QSize const size(rotation.rotate(metadata.size()));
	QSize const dpi(rotation.rotate(metadata.dpi().toSize()));
	
	if (size.width() * dpi.height() > size.height() * dpi.width()) {
		return 2;
	} else {
		return 1;
	}
}

void
PageSequence::setCurPage(PageId const& page_id)
{
	bool was_modified = false;
	
	{
		QMutexLocker locker(&m_mutex);
		setCurPageImpl(page_id, &was_modified);
	}
	
	if (was_modified) {
		emit modified();
	}
}

int
PageSequence::numImages() const
{
	QMutexLocker locker(&m_mutex);
	return m_images.size();
}

int
PageSequence::curImageIdx() const
{
	QMutexLocker locker(&m_mutex);
	return m_curImage;
}

PageInfo
PageSequence::curPage() const
{
	QMutexLocker locker(&m_mutex);
	assert((size_t)m_curLogicalPage <= m_images.size());
	ImageDesc const& image = m_images[m_curImage];
	PageId const id(image.id, m_curSubPage);
	return PageInfo(id, image.metadata, image.numLogicalPages);
}

PageInfo
PageSequence::setPrevPage(View const view)
{
	PageInfo info;
	
	bool was_modified = false;
	
	{
		QMutexLocker locker(&m_mutex);
		info = setPrevPageImpl(view, &was_modified);
	}
	
	if (was_modified) {
		emit modified();
	}
	
	return info;
}

PageInfo
PageSequence::setNextPage(View const view)
{
	PageInfo info;
	
	bool was_modified = false;
	
	{
		QMutexLocker locker(&m_mutex);
		info = setNextPageImpl(view, &was_modified);
	}
	
	if (was_modified) {
		emit modified();
	}
	
	return info;
}

void
PageSequence::setLogicalPagesInImageImpl(
	ImageId const& image_id, int const num_pages, bool* modified)
{
	int logical_pages_seen = 0;
	int const num_images = m_images.size();
	for (int i = 0; i < num_images; ++i) {
		ImageDesc& image = m_images[i];
		if (image.id == image_id) {
			int const delta = num_pages - image.numLogicalPages;
			if (delta == 0) {
				break;
			}
			
			image.numLogicalPages = num_pages;
			m_totalLogicalPages += delta;
			if (logical_pages_seen < m_curLogicalPage) {
				m_curLogicalPage += delta;
			}
			if (num_pages == 1) {
				m_curSubPage = LogicalPageId::SINGLE_PAGE;
			} else if (num_pages == 2) {
				m_curSubPage = LogicalPageId::LEFT_PAGE;
			}
			
			*modified = true;
			break;
		}
		logical_pages_seen += image.numLogicalPages;
	}
}

void
PageSequence::autoSetLogicalPagesInImageImpl(
	ImageId const& image_id, OrthogonalRotation const rotation, bool* modified)
{
	int logical_pages_seen = 0;
	int const num_images = m_images.size();
	for (int i = 0; i < num_images; ++i) {
		ImageDesc& image = m_images[i];
		if (image.id == image_id) {
			int const num_pages = adviseNumberOfLogicalPages(
				image.metadata, rotation
			);
			int const delta = num_pages - image.numLogicalPages;
			if (delta == 0) {
				break;
			}
			
			image.numLogicalPages = num_pages;
			m_totalLogicalPages += delta;
			if (logical_pages_seen < m_curLogicalPage) {
				m_curLogicalPage += delta;
			}
			if (num_pages == 1) {
				m_curSubPage = LogicalPageId::SINGLE_PAGE;
			} else if (num_pages == 2) {
				m_curSubPage = LogicalPageId::LEFT_PAGE;
			}
			
			*modified = true;
			break;
		}
		logical_pages_seen += image.numLogicalPages;
	}
}

void
PageSequence::setCurPageImpl(PageId const& page_id, bool* modified)
{
	int logical_pages_seen = 0;
	int const num_images = m_images.size();
	for (int i = 0; i < num_images; ++i) {
		ImageDesc const& image = m_images[i];
		if (image.id == page_id.imageId()) {
			LogicalPageId::SubPage sub_page = page_id.logicalPageId().subPage();
			if (m_curImage != i || sub_page != m_curSubPage) {
				*modified = true;
			}
			m_curImage = i;
			m_curSubPage = sub_page;
			int sub_page_num = page_id.logicalPageId().subPageNum();
			if (sub_page_num >= image.numLogicalPages) {
				sub_page_num = image.numLogicalPages - 1;
			}
			m_curLogicalPage = logical_pages_seen + sub_page_num;
			break;
		}
		logical_pages_seen += image.numLogicalPages;
	}
}

PageInfo
PageSequence::setPrevPageImpl(View const view, bool* modified)
{
	assert((size_t)m_curImage <= m_images.size());
	
	ImageDesc const* image = &m_images[m_curImage];
	if (view == PAGE_VIEW && m_curSubPage == LogicalPageId::RIGHT_PAGE
	    && image->numLogicalPages > 1) {
		--m_curLogicalPage;
		m_curSubPage = LogicalPageId::LEFT_PAGE;
		*modified = true;
	} else if (m_curImage > 0) {
		if (m_curSubPage == LogicalPageId::RIGHT_PAGE) {
			--m_curLogicalPage; // move to LEFT_PAGE
		}
		--m_curLogicalPage; // move to previous page
		--m_curImage;
		--image;
		if (image->numLogicalPages > 1) {
			m_curSubPage = LogicalPageId::RIGHT_PAGE;
		} else {
			m_curSubPage = LogicalPageId::SINGLE_PAGE;
		}
		*modified = true;
	}
	
	PageId const id(image->id, m_curSubPage);
	return PageInfo(id, image->metadata, image->numLogicalPages);
}

PageInfo
PageSequence::setNextPageImpl(View const view, bool* modified)
{
	assert((size_t)m_curImage <= m_images.size());
	
	ImageDesc const* image = &m_images[m_curImage];
	if (view == PAGE_VIEW && m_curSubPage == LogicalPageId::LEFT_PAGE
	    && image->numLogicalPages > 1) {
		++m_curLogicalPage;
		m_curSubPage = LogicalPageId::RIGHT_PAGE;
		*modified = true;
	} else if (m_curImage < (int)m_images.size() - 1) {
		if (m_curSubPage == LogicalPageId::LEFT_PAGE) {
			++m_curLogicalPage; // move to RIGHT_PAGE
		}
		++m_curLogicalPage; // move to the next page
		++m_curImage;
		++image;
		if (image->numLogicalPages > 1) {
			m_curSubPage = LogicalPageId::LEFT_PAGE;
		} else {
			m_curSubPage = LogicalPageId::SINGLE_PAGE;
		}
		*modified = true;
	}
	
	PageId const id(image->id, m_curSubPage);
	return PageInfo(id, image->metadata, image->numLogicalPages);
}


/*========================= PageSequenceSnapshot ========================*/

PageSequenceSnapshot::PageSequenceSnapshot(PageSequence::View const view)
:	m_curPageIdx(0),
	m_view(view)
{
}

PageSequenceSnapshot::~PageSequenceSnapshot()
{
}

PageInfo const&
PageSequenceSnapshot::curPage() const
{
	return m_pages.at(m_curPageIdx); // may throw
}

PageInfo const&
PageSequenceSnapshot::pageAt(size_t const idx) const
{
	return m_pages.at(idx); // may throw
}


/*========================= PageSequence::ImageDesc ======================*/

PageSequence::ImageDesc::ImageDesc(
	ImageId const& id, ImageMetadata const& metadata, int sub_pages)
:	id(id),
	metadata(metadata),
	numLogicalPages(sub_pages)
{
}

PageSequence::ImageDesc::ImageDesc(
	ImageId const& id, ImageMetadata const& metadata, Pages const pages)
:	id(id),
	metadata(metadata)
{
	switch (pages) {
		case ONE_PAGE:
			numLogicalPages = 1;
			break;
		case TWO_PAGES:
			numLogicalPages = 2;
			break;
		case AUTO_PAGES:
			numLogicalPages = adviseNumberOfLogicalPages(
				metadata, OrthogonalRotation()
			);
			break;
	}
}

LogicalPageId::SubPage
PageSequence::ImageDesc::logicalPageToSubPage(int const logical_page) const
{
	assert(numLogicalPages >= 1 && numLogicalPages <= 2);
	assert(logical_page >= 0 && logical_page < numLogicalPages);
	
	if (numLogicalPages == 1) {
		return LogicalPageId::SINGLE_PAGE;
	} else if (logical_page == 0) {
		return LogicalPageId::LEFT_PAGE;
	} else {
		return LogicalPageId::RIGHT_PAGE;
	}
}
