// git-gui gettext support
// Copyright (C) 2018 Johannes Sixt

#include "libintl.h"
#include <stdexcept>

using namespace std;

string formatMessage(string msg, initializer_list<std::string> args)
{
	string::size_type pos = 0;
	for (const auto& arg: args)
	{
		pos = msg.find("%s", pos);
		if (pos == string::npos)
			throw logic_error("too few inserts in format string: " + msg);
		msg.replace(pos, 2, arg);
		pos += arg.length();
	}
	if (msg.find("%s", pos) != string::npos)
		throw logic_error("too many inserts in format string: " + msg);

	return msg;
}
