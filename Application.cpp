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

#include "Application.h.moc"
#include "NewOpenProjectDialog.h"
#include "ProjectCreationContext.h"
#include "ProjectOpeningContext.h"

Application::Application(int& argc, char** argv)
:	QApplication(argc, argv),
	m_pNewOpenProjectDialog(0)
{
}

void
Application::showNewOpenProjectDialog()
{
	if (!m_pNewOpenProjectDialog) {
		m_pNewOpenProjectDialog = new NewOpenProjectDialog;
		connect(
			m_pNewOpenProjectDialog, SIGNAL(newProject()),
			this, SLOT(newProject())
		);
		connect(
			m_pNewOpenProjectDialog, SIGNAL(openProject()),
			this, SLOT(openProject())
		);
	}
	
	m_pNewOpenProjectDialog->show();
}

void
Application::newProject()
{
	m_pNewOpenProjectDialog->hide();
	ProjectCreationContext* context = new ProjectCreationContext();
	connect(
		context, SIGNAL(destroyed(QObject*)),
		this, SLOT(projectContextDestroyed())
	);
}

void
Application::openProject()
{
	m_pNewOpenProjectDialog->hide();
	ProjectOpeningContext* context = new ProjectOpeningContext();
	connect(
		context, SIGNAL(destroyed(QObject*)),
		this, SLOT(projectContextDestroyed())
	);
	context->openProject();
}

void
Application::projectContextDestroyed()
{
	if (topLevelWidgets().size() == 1) {
		showNewOpenProjectDialog();
	}
}
