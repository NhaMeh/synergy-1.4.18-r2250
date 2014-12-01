/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2008 Volker Lanz (vl@fidra.de)
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define TRAY_RETRY_COUNT 10
#define TRAY_RETRY_WAIT 2000

#include "QSynergyApplication.h"
#include "MainWindow.h"
#include "AppConfig.h"
#include "SetupWizard.h"

#include <QtCore>
#include <QtGui>
#include <QSettings>
#include <QMessageBox>

#if defined(Q_OS_MAC)
#include <Carbon/Carbon.h>

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1090
#include "AXDatabaseCleaner.h"
#endif

#endif

class QThreadImpl : public QThread
{
public:
	static void msleep(unsigned long msecs)
	{
		QThread::msleep(msecs);
	}
};

int waitForTray();

#if defined(Q_OS_MAC)
bool checkMacAssistiveDevices();
#endif

int main(int argc, char* argv[])
{
	QCoreApplication::setOrganizationName("Synergy");
	QCoreApplication::setOrganizationDomain("http://synergy-project.org/");
	QCoreApplication::setApplicationName("Synergy");

	QSynergyApplication app(argc, argv);

#if defined(Q_OS_MAC)

	if (app.applicationDirPath().startsWith("/Volumes/")) {
		QMessageBox::information(
			NULL, "Synergy",
			"Please drag Synergy to the Applications folder, and open it from there.");
		return 1;
	}
	
	if (!checkMacAssistiveDevices())
	{
		return 1;
	}
#endif

	if (!waitForTray())
	{
		return -1;
	}

#ifndef Q_OS_WIN
	QApplication::setQuitOnLastWindowClosed(false);
#endif

	QSettings settings;
	AppConfig appConfig(&settings);

	app.switchTranslator(appConfig.language());

	MainWindow mainWindow(settings, appConfig);
	SetupWizard setupWizard(mainWindow, true);

	if (appConfig.wizardShouldRun())
	{
		setupWizard.show();
	}
	else
	{
		mainWindow.open();
	}

	return app.exec();
}

int waitForTray()
{
	// on linux, the system tray may not be available immediately after logging in,
	// so keep retrying but give up after a short time.
	int trayAttempts = 0;
	while (true)
	{
		if (QSystemTrayIcon::isSystemTrayAvailable())
		{
			break;
		}

		if (++trayAttempts > TRAY_RETRY_COUNT)
		{
			QMessageBox::critical(NULL, "Synergy",
				QObject::tr("System tray is unavailable, quitting."));
			return false;
		}

		QThreadImpl::msleep(TRAY_RETRY_WAIT);
	}
	return true;
}

#if defined(Q_OS_MAC)
bool settingsExist()
{
	QSettings settings;

	//FIXME: check settings existance properly
	int port = settings.value("port", -1).toInt();

	return port == -1 ? false : true;
}

bool checkMacAssistiveDevices()
{
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1090 // mavericks
	// new in mavericks, applications are trusted individually
	// with use of the accessibility api. this call will show a
	// prompt which can show the security/privacy/accessibility
	// tab, with a list of allowed applications. synergy should
	// show up there automatically, but will be unchecked.

	const void* keys[] = { kAXTrustedCheckOptionPrompt };
	const void* trueValue[] = { kCFBooleanTrue };
	const void* falseValue[] = { kCFBooleanFalse };
	CFDictionaryRef optionsWithPrompt = CFDictionaryCreate(NULL, keys, trueValue, 1, NULL, NULL);
	CFDictionaryRef optionsWithoutPrompt = CFDictionaryCreate(NULL, keys, falseValue, 1, NULL, NULL);
	bool result;

	result = AXIsProcessTrustedWithOptions(optionsWithoutPrompt);

	// Synergy is not checked in accessibility
	if (!result) {
		// if setting doesn't exist, just skip helper tool
		if (!settingsExist()) {
			result = AXIsProcessTrustedWithOptions(optionsWithPrompt);
		}
		else {
			int reply;
			reply = QMessageBox::question(
				NULL,
				"Synergy",
				"Synergy requires access to Assistive Devices, but was not allowed.\n\nShould Synergy attempt to fix this?",
				QMessageBox::Yes|QMessageBox::Default,
				QMessageBox::No|QMessageBox::Escape);

			if (reply == QMessageBox::Yes) {
				// call privilege help tool
				AXDatabaseCleaner axdc;
				result = axdc.loadPrivilegeHelper();
				result = axdc.xpcConnect();

				QMessageBox box;
				box.setModal(false);
				box.setStandardButtons(0);
				box.setText("Please wait.");
				box.resize(150, 10);
				box.show();

				const char* command = "sudo sqlite3 /Library/Application\\ Support/com.apple.TCC/TCC.db 'delete from access where client like \"%Synergy.app%\"'";
				result = axdc.privilegeCommand(command);

				box.hide();

				if (result) {
					QMessageBox::information(
						NULL, "Synergy",
						"Synergy helper tool is complete. Please tick the checkbox in Accessibility.");
				}
			}

			result = AXIsProcessTrustedWithOptions(optionsWithPrompt);
		}
	}

	CFRelease(optionsWithoutPrompt);
	CFRelease(optionsWithPrompt);

	return result;

#else

	// now deprecated in mavericks.
	bool result = AXAPIEnabled();
	if (!result) {
		QMessageBox::information(
			NULL, "Synergy",
			"Please enable access to assistive devices "
			"(System Preferences), then re-open Synergy.");
	}
	return result;

#endif
}
#endif
