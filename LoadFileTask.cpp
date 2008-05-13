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

#include "LoadFileTask.h"
#include "filters/fix_orientation/Task.h"
#include "TaskStatus.h"
#include "FilterResult.h"
#include "ErrorWidget.h"
#include "FilterUiInterface.h"
#include "AbstractFilter.h"
#include "FilterOptionsWidget.h"
#include "PageInfo.h"
#include "Dpi.h"
#include "Dpm.h"
#include "FilterData.h"
#include "TiffReader.h"
#include "imageproc/BinaryThreshold.h"
#include <QImage>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <assert.h>

using namespace imageproc;

class LoadFileTask::ErrorResult : public FilterResult
{
public:
	ErrorResult(QString const& file_path);
	
	virtual void updateUI(FilterUiInterface* ui);
	
	virtual IntrusivePtr<AbstractFilter> filter() {
		return IntrusivePtr<AbstractFilter>();
	}
private:
	QString m_filePath;
};


LoadFileTask::LoadFileTask(PageInfo const& page,
	IntrusivePtr<fix_orientation::Task> const& next_task)
:	m_imageId(page.id()),
	m_imageMetadata(page.metadata()),
	m_ptrNextTask(next_task)
{
	assert(m_ptrNextTask);
}

LoadFileTask::~LoadFileTask()
{
}

FilterResultPtr
LoadFileTask::operator()()
{
	try {
		throwIfCancelled();
		
		QImage image;
		QFile file(m_imageId.filePath());
		if (file.open(QIODevice::ReadOnly)) {
			if (TiffReader::canRead(file)) {
				image = TiffReader::readImage(file, m_imageId.page());
			} else {
				image.load(&file, 0);
			}
		}
		
		if (image.isNull()) {
			return FilterResultPtr(new ErrorResult(m_imageId.filePath()));
		} else {
			addMissingMetadata(image);
			return m_ptrNextTask->process(*this, FilterData(image));
		}
	} catch (CancelledException const&) {
		return FilterResultPtr();
	}
}

void
LoadFileTask::addMissingMetadata(QImage& image) const
{
	if (Dpm(image.dotsPerMeterX(), image.dotsPerMeterY()).isNull()) {
		Dpm const dpm(m_imageMetadata.dpi());
		image.setDotsPerMeterX(dpm.horizontal());
		image.setDotsPerMeterY(dpm.vertical());
	}
}


/*======================= LoadFileTask::ErrorResult ======================*/

LoadFileTask::ErrorResult::ErrorResult(QString const& file_path)
:	m_filePath(file_path)
{
}

void
LoadFileTask::ErrorResult::updateUI(FilterUiInterface* ui)
{
	QString err_msg(QObject::tr("The following file could not be loaded:\n"));
	err_msg += m_filePath;
	ui->setImageWidget(new ErrorWidget(err_msg));
	ui->setOptionsWidget(new FilterOptionsWidget);
}
