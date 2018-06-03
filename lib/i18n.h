// git-gui gettext support
// Copyright (C) 2018 Johannes Sixt

#pragma once

#include <initializer_list>
#include <string>

std::string formatMessage(std::string msg, std::initializer_list<std::string> args);

template<class... ARGS>
std::string mc(const char* text, ARGS&&... args)
{
	if (sizeof...(ARGS) > 0)
		return formatMessage(text, { std::forward<ARGS>(args)... });
	else
		return text;
}
