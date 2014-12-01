/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2014 Bolton Software Ltd.
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

#include "synergy/ToolApp.h"
#include "arch/Arch.h"
#include "base/String.h"

#include <iostream>
#include <sstream>
	
//#define PREMIUM_AUTH_URL "http://localhost/synergy/premium/json/auth/"
#define PREMIUM_AUTH_URL "https://synergy-project.org/premium/json/auth/"

enum {
	kErrorOk,
	kErrorArgs,
	kErrorException,
	kErrorUnknown
};

UInt32
CToolApp::run(int argc, char** argv)
{
	if (argc <= 1) {
		std::cerr << "no args" << std::endl;
		return kErrorArgs;
	}

	try {
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--premium-auth") == 0) {
				premiumAuth();
				return kErrorOk;
			}
			else {
				std::cerr << "unknown arg: " << argv[i] << std::endl;
				return kErrorArgs;
			}
		}
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
		return kErrorException;
	}
	catch (...) {
		std::cerr << "unknown error" << std::endl;
		return kErrorUnknown;
	}

	return kErrorOk;
}

void
CToolApp::premiumAuth()
{
	CString credentials;
	std::cin >> credentials;

	size_t separator = credentials.find(':');
	CString email = credentials.substr(0, separator);
	CString password = credentials.substr(separator + 1, credentials.length());
			
	std::stringstream ss;
	ss << PREMIUM_AUTH_URL;
	ss << "?email=" << ARCH->internet().urlEncode(email);
	ss << "&password=" << password;

	std::cout << ARCH->internet().get(ss.str()) << std::endl;
}
