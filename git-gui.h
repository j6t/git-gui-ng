// git-guing: the application
// Copyright (C) 2018 Johannes Sixt

#pragma once

#include "lib/repo.h"
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
	bool discover_gitdir_prefix();
	bool discover_worktree();
	int usage(const char* argv0, const std::string& args);
	int do_blame_browser(const char* argv0,
			const std::vector<std::string>& argv, bool is_blame);
	int do_subcommand(const std::string& subcommand, const char* argv0,
			std::vector<std::string>& argv);

	static void do_about();

	Repo repo;
	static const std::string appname;
	static const std::string appvers;
	static const std::string copyright;
};
