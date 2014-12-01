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

#if !defined(QSYNERGYAPPLICATION__H)

#define QSYNERGYAPPLICATION__H

#include <QApplication>

class QSessionManager;

class QSynergyApplication : public QApplication
{
	public:
		QSynergyApplication(int& argc, char** argv);
		~QSynergyApplication();

	public:
		void commitData(QSessionManager& manager);
		void switchTranslator(QString lang);
		void setTranslator(QTranslator* translator);

		static QSynergyApplication* getInstance();

	private:
		QTranslator* m_Translator;

		static QSynergyApplication* s_Instance;
};

#endif

