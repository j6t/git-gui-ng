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
	void apply_theme_config();

	void error_popup(const std::string& msg);
	std::string ask_popup(const std::string& msg);

	std::string M1T(std::string key) const;
	std::string M1B(std::string key) const;
	static void bind_button3(const std::string& w, std::function<void(int,int)> cb);
	static void bind_button3_xyXY(const std::string& w, std::function<void(int,int,int,int)> cb);

	// about
	static void do_about();

	// database
	void do_stats();
	void do_gc();
	void do_fsck_objects();
	void hint_gc();

	Repo repo;
	std::string ui_index = ".vpane.files.index.list";
	std::string ui_workdir = ".vpane.files.workdir.list";
	std::string ui_diff = ".vpane.lower.diff.body.t";
	std::string ui_comm = ".vpane.lower.commarea.buffer.frame.t";
	std::string ui_coml = ".vpane.lower.commarea.buffer.header.l";
	std::string m1t_pfx;
	std::string m1b_pfx;
	std::string tools_menubar;
	std::string cursor_ptr = "arrow";

	static const std::string appname;
	static const std::string appvers;
	static const std::string copyright;
};
