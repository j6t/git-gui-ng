// git-guing: the application
// Copyright (C) 2018 Johannes Sixt

#pragma once

#include <string>
#include <vector>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

class GitGui
{
public:
	int main(const char* argv0, std::vector<std::string> argv);

private:
	void check_for_trace(std::vector<std::string>& argv);
	std::string find_subcommand(fs::path argv0, std::vector<std::string>& argv);
	void determine_features(const std::string& subcommand, std::vector<std::string>& argv);
};
