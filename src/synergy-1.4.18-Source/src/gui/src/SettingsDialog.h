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

#if !defined(SETTINGSDIALOG_H)

#define SETTINGSDIALOG_H

#include <QDialog>
#include "ui_SettingsDialogBase.h"
#include "SynergyLocale.h"

class AppConfig;

class SettingsDialog : public QDialog, public Ui::SettingsDialogBase
{
	Q_OBJECT

	public:
		SettingsDialog(QWidget* parent, AppConfig& config);
		static QString browseForSynergyc(QWidget* parent, const QString& programDir, const QString& synergycName);
		static QString browseForSynergys(QWidget* parent, const QString& programDir, const QString& synergysName);

	protected:
		void accept();
		void reject();
		void changeEvent(QEvent* event);
		AppConfig& appConfig() { return m_AppConfig; }

	private:
		AppConfig& m_AppConfig;
		SynergyLocale m_Locale;

	private slots:
		void on_m_pCheckBoxEnableCrypto_stateChanged(int );
		void on_m_pComboLanguage_currentIndexChanged(int index);
		void on_m_pCheckBoxLogToFile_stateChanged(int );
		void on_m_pButtonBrowseLog_clicked();
};

#endif
