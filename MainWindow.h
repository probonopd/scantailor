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

#ifndef MAINWINDOW_H_
#define MAINWINDOW_H_

#include "ui_MainWindow.h"
#include "FilterUiInterface.h"
#include "NonCopyable.h"
#include "IntrusivePtr.h"
#include "BackgroundTask.h"
#include "FilterResult.h"
#include "PageSequence.h"
#include "ImageFileInfo.h"
#include <QMainWindow>
#include <QString>
#include <QPointer>
#include <memory>

class AbstractFilter;
class FilterOptionsWidget;
class PageInfo;
class QStackedLayout;
class WorkerThread;
class ProjectReader;
class DebugImages;
class QLineF;
class QLayout;

class MainWindow : public QMainWindow, private FilterUiInterface, private Ui::MainWindow
{
	DECLARE_NON_COPYABLE(MainWindow)
	Q_OBJECT
public:
	MainWindow(std::vector<ImageFileInfo> const& files, QString const& out_dir);
	
	MainWindow(QString const& project_file, ProjectReader const& project_reader);
	
	virtual ~MainWindow();
private slots:
	void pageChanged(int page);
	
	void filterSelectionChanged(QItemSelection const& selected);
	
	void workerThreadReady();
	
	void filterOptionsChanged();
	
	void filterResult(BackgroundTaskPtr const& task, FilterResultPtr const& result);
	
	void debugToggled(bool enabled);
	
	void saveProjectTriggered();
	
	void saveProjectAsTriggered();
	
	void pageSequenceModified();
private:
	class FilterListModel;
	
	typedef IntrusivePtr<AbstractFilter> FilterPtr;
	
	virtual void setOptionsWidget(FilterOptionsWidget* widget);
	
	virtual void setImageWidget(
		QWidget* widget, DebugImages const* debug_images = 0);
	
	void construct();
	
	void removeWidgetsFromLayout(QLayout* layout, bool delete_widgets);
	
	void loadImage();
	
	void loadImage(PageInfo const& page);
	
	void syncPageSequence();
	
	void updateWindowTitle();
	
	bool saveProjectWithFeedback(QString const& project_file);
	
	IntrusivePtr<PageSequence> m_ptrPages;
	PageSequenceSnapshot m_frozenPages;
	QString m_outDir;
	QString m_projectFile;
	WorkerThread* m_pWorkerThread;
	QStackedLayout* m_pImageFrameLayout;
	QStackedLayout* m_pOptionsFrameLayout;
	QPointer<FilterOptionsWidget> m_ptrOptionsWidget;
	std::auto_ptr<FilterListModel> m_ptrFilterListModel;
	BackgroundTaskPtr m_ptrCurTask;
	int m_curFilter;
	int m_ignoreSelectionChanges;
	int m_ignorePageChanges;
	bool m_workerThreadReady;
	bool m_debug;
	bool m_projectModified;
};

#endif
