// git-guing: the application
// Copyright (C) 2018 Johannes Sixt

#include "git-gui.h"
#include "git-gui-config.h"
#include "cpptk.h"
#include "lib/i18n.h"
#include <algorithm>
#include <iostream>

#include "lib/blame.h"
#include "lib/branch.h"
#include "lib/branch_checkout.h"
#include "lib/branch_create.h"
#include "lib/branch_delete.h"
#include "lib/branch_rename.h"
#include "lib/browser.h"
#include "lib/checkout_op.h"
#include "lib/choose_font.h"
#include "lib/choose_repository.h"
#include "lib/choose_rev.h"
#include "lib/class.h"
#include "lib/commit.h"
#include "lib/console.h"
#include "lib/date.h"
#include "lib/diff.h"
#include "lib/encoding.h"
#include "lib/error.h"
#include "lib/index.h"
#include "lib/line.h"
#include "lib/logo.h"
#include "lib/merge.h"
#include "lib/mergetool.h"
#include "lib/option.h"
#include "lib/remote.h"
#include "lib/remote_add.h"
#include "lib/remote_branch_delete.h"
#include "lib/repo.h"
#include "lib/search.h"
#include "lib/shortcut.h"
#include "lib/spellcheck.h"
#include "lib/sshkey.h"
#include "lib/status_bar.h"
#include "lib/themed.h"
#include "lib/tools.h"
#include "lib/tools_dlg.h"
#include "lib/transport.h"
#include "lib/win32.h"

using namespace Tk;
using namespace Tk::literals;
using namespace std::literals;

const std::string GitGui::appname = "Git Gui";
const std::string GitGui::appvers = GITGUING_VERSION;
const std::string GitGui::copyright = u8R"copy(
Copyright © 2006-2010 Shawn Pearce, et. al.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.)copy";


void GitGui::check_for_trace(std::vector<std::string>& argv)
{
	auto pos = std::find(argv.begin(), argv.end(), "--trace");
	if (pos != argv.end()) {
		argv.erase(pos);
		"set _trace 1"_tcl;
		R"tcl(if {[tk windowingsystem] eq "win32"} { console show })tcl"_tcl;
	} else {
		"set _trace 0"_tcl;
	}
}

std::string GitGui::find_subcommand(fs::path argv0, std::vector<std::string>& argv)
{
	std::string subcommand = argv0.filename().string();
	if (subcommand.substr(0, 4) == "git-")
		subcommand.erase(0, 4);

	if (subcommand.substr(0, 3) == "gui") {
		if (!argv.empty()) {
			subcommand = argv.front();
			argv.erase(argv.begin());
		} else {
			subcommand = "gui";
		}
	}
	return subcommand;
}

void GitGui::determine_features(const std::string& subcommand, std::vector<std::string>& argv)
{
	"enable_option multicommit"_tcl;
	"enable_option branch"_tcl;
	"enable_option transport"_tcl;
	"disable_option bare"_tcl;

	if (subcommand == "browser" || subcommand == "blame")
	{
		"enable_option bare"_tcl;

		"disable_option multicommit"_tcl;
		"disable_option branch"_tcl;
		"disable_option transport"_tcl;
	}
	else if (subcommand == "citool")
	{
		"enable_option singlecommit"_tcl;
		"enable_option retcode"_tcl;

		"disable_option multicommit"_tcl;
		"disable_option branch"_tcl;
		"disable_option transport"_tcl;

		auto i = argv.begin();
		for (; i != argv.end(); ++i)
		{
			if (*i == "--amend")
			{
				"enable_option initialamend"_tcl;
			}
			else if (*i == "--nocommit")
			{
				"enable_option nocommit"_tcl;
				"enable_option nocommitmsg"_tcl;
			}
			else if (*i == "--commitmsg")
			{
				"disable_option nocommitmsg"_tcl;
			}
			else
			{
				break;
			}
		}
		argv.erase(argv.begin(), i);
	}
}

//////////////////////////////////////////////////////////////////////
//
// repository setup

// returns true if the repository picker is needed
bool GitGui::discover_gitdir_prefix()
{

	fs::path _gitdir, _prefix;
	auto gitdir_env = getenv("GIT_DIR");
	if (gitdir_env)
	{
		_gitdir = gitdir_env;
	}
	else
	{
		try {
			// beware that from the .git dir this sets _gitdir to .
			// and _prefix to the empty string
			_gitdir = "git rev-parse --git-dir"_tcls;
			_prefix = "git rev-parse --show-prefix"_tcls;
		} catch (TkError&) {
			return true;
		}
	}

	// we expand the _gitdir when it's just a single dot (i.e. when we're being
	// run from the .git dir itself) lest the routines to find the worktree
	// get confused
	if (_gitdir == ".")
		_gitdir = fs::current_path();

	repo.set_gitdir(_gitdir.lexically_normal());
	repo.set_prefix(std::move(_prefix));
	return false;
}

bool GitGui::discover_worktree()
{
	// v1.7.0 introduced --show-toplevel to return the canonical work-tree
	fs::path _gitworktree = "git rev-parse --show-toplevel"_tcls;

	if (!repo.prefix().empty()) {
		fs::path cdup = _gitworktree;
		if (_gitworktree.empty()) {
			for (const auto& el: repo.prefix()) {
				if (el != ".")	// trailing slash produces this
					cdup /= "..";
			}
		}
		boost::system::error_code ec{};
		fs::current_path(cdup, ec);
		if (ec) {
			"catch {wm withdraw .}"_tcl;
			error_popup(mc("Cannot move to top of working directory:")
				+ "\n\n" + ec.message());
			return false;
		}
		_gitworktree = fs::current_path();
	} else if (!"is_enabled bare"_tcli) {
		if ("is_bare"_tcli) {
			"catch {wm withdraw .}"_tcl;
			error_popup(mc("Cannot use bare repository:")
				+ "\n\n" + repo.gitdir().string());
			return false;
		}
		if (_gitworktree.empty())
			_gitworktree = repo.gitdir().parent_path();
		boost::system::error_code ec{};
		fs::current_path(_gitworktree, ec);
		if (ec) {
			"catch {wm withdraw .}"_tcl;
			error_popup(mc("No working directory")
				+ " " + _gitworktree.string()
				+ "\n\n" + ec.message());
			return false;
		}
		_gitworktree = fs::current_path();
	}
	repo.set_worktree(std::move(_gitworktree));
	return true;
}

int GitGui::usage(const char* argv0, const std::string& args)
{
	std::string s = mc("usage:");
	s += " ";
	s += argv0;
	s += " " + args;
	if ("tk windowingsystem"_tcls == "win32") {
		wm(withdraw, ".");
		tk_messageBox() -icon(info) -messagetext(s)
			-title(mc("Usage"));
	} else {
		std::cerr << s << std::endl;
	}
	return 2;
}

// -- Not a normal commit type invocation?  Do that instead!
int GitGui::do_blame_browser(const char* argv0,
			const std::vector<std::string>& argv, bool is_blame)
{
	std::string args;
	if (is_blame)
		args = "blame [--line=<num>] rev? path";
	else
		args = "browser rev? path";
	auto report_usage = [&]{ return usage(argv0, args); };

	if (argv.empty())
		return report_usage();

	fs::path path;
	std::string head, jump_spec;
	bool is_path = false;
	for (const auto& a: argv)
	{
		auto p = repo.prefix() / a;

		if (is_path || fs::exists(p)) {
			if (!path.empty())
				return report_usage();
			path = p.lexically_normal();
			// this did not remove trailing "/" or "/."
			if (path.filename() == "." && path.has_parent_path())
				path.remove_filename();
			break;
		} else if (a == "--") {
			if (!path.empty()) {
				if (!head.empty())
					return report_usage();
				head = path.string();
				path.clear();
			}
			is_path = true;
		} else if (a.substr(0, 7) == "--line=") {
			if (!jump_spec.empty() || !head.empty())
				return report_usage();
			jump_spec = a.substr(7);
		} else if (head.empty()) {
			head = a;
			is_path = true;
		} else {
			return report_usage();
		}
	}

	if (!head.empty() && path.empty()) {
		if (head[0] == '/') {
			path = fs::path(head).lexically_normal();
			head.clear();
		} else {
			path = (repo.prefix() / head).lexically_normal();
			head.clear();
		}
	}

	if (head.empty()) {
		"load_current_branch"_tcl;
	} else {
		if (head.size() < 40 && head.find_first_not_of("0123456789abcdef") == std::string::npos) {
			try {
				head = std::string(eval("git rev-parse --verify \"" + head + '"'));
			} catch (TkError& e) {
				std::string err = e.what();
				if ("tk windowingsystem"_tcls == "win32") {
					tk_messageBox() -icon(error) -title(mc("Error")) -messagetext(err);
				} else {
					std::cerr << err << std::endl;
				}
				return 2;
			}
		}
		"current_branch"_tclv = head;
	}

	wm(deiconify, ".");
	if (!is_blame) {
		if (!jump_spec.empty())
			return report_usage();
		if (head.empty()) {
			if (!path.empty() && fs::is_directory(path)) {
				head = "current_branch"_tclvs;
			} else {
				head = path.string();
				path.clear();
			}
		}
		eval("browser::new \"" + head + "\" \"" + path.string() + '"');
	}
	else
	{
		if (head.empty() && !fs::exists(path)) {
			"catch {wm withdraw .}"_tcl;
			tk_messageBox()
				-icon(error)
				-messagetype(ok)
				-title(mc("git-gui: fatal error"))
				-messagetext(mc("fatal: cannot stat path %s: No such file or directory", path.string()));
			return 2;
		}
		eval("blame::new \"" + head + "\" \"" + path.string() + "\" \"" + jump_spec + '"');
	}
	runEventLoop();
	return 1;
}

int GitGui::do_subcommand(const std::string& subcommand, const char* argv0,
			std::vector<std::string>& argv)
{
	if (subcommand == "browser")
		return do_blame_browser(argv0, argv, false);
	if (subcommand == "blame")
		return do_blame_browser(argv0, argv, true);
	if (subcommand == "citool" || subcommand == "gui")
	{
		if (!argv.empty()) {
			return usage(argv0, subcommand);
		}
		// fall through to setup UI for commits
	}
	else
	{
		auto s = mc("usage:") + " " + argv0 + " [{blame|browser|citool}]";
		if ("tk windowingsystem"_tcls == "win32") {
			wm(withdraw, ".");
			tk_messageBox() -icon(error) -messagetext(s)
				-title(mc("Usage"));
		} else {
			std::cerr << s << std::endl;
		}
		return 2;
	}
	return 0;
}

void GitGui::apply_theme_config()
{
	bool usettk = !!"::repo_config(gui.usettk)"_tclvi;
	useTtk(usettk);
	if (usettk) {
		"set ::use_ttk 1"_tcl;
		"set ::NS ttk"_tcl;
		bind(winfo(wndclass, "."s), "<<ThemeChanged>>"s, "InitTheme"s);
		"pave_toplevel ."_tcl;
	} else {
		"set ::use_ttk 0"_tcl;
		"set ::NS {}"_tcl;
	}
}

std::string GitGui::M1T(std::string key) const
{
	return m1t_pfx + std::move(key);
}

std::string GitGui::M1B(std::string key) const
{
	return m1b_pfx + "Key-"s + std::move(key) + ">";
}

template<class... ARGS>
static void bind_button3(const std::string& w, ARGS&&... args)
{
	if ("is_MacOSX"_tcli) {
		// Mac OS X sends Button-2 on right click through three-button mouse,
		// or through trackpad right-clicking (two-finger touch + click).
		bind(w, "<Any-Button-2>", args...);
		bind(w, "<Control-Button-1>", args...);
	}
	bind(w, "<Any-Button-3>", std::forward<ARGS>(args)...);
}

void GitGui::bind_button3(const std::string& w, std::function<void(int,int)> cb)
{
	::bind_button3(w, std::move(cb), event_X, event_Y);
}

void GitGui::bind_button3_xyXY(const std::string& w, std::function<void(int,int,int,int)> cb)
{
	::bind_button3(w, std::move(cb), event_x, event_y, event_X, event_Y);
}

int GitGui::main(const char* argv0, std::vector<std::string> argv)
{
	Tk::init(argv0);

	setlocale (LC_ALL, "");
	bindtextdomain("git-gui", LOCALEDIR);
	textdomain("git-gui");

	R"tcl(
######################################################################
##
## Tcl/Tk sanity check

if {[catch {package require Tcl 8.4} err]
 || [catch {package require Tk  8.4} err]
} {
	catch {wm withdraw .}
	tk_messageBox \
		-icon error \
		-type ok \
		-title "git-gui: fatal error" \
		-message $err
	exit 1
}

catch {rename send {}} ; # What an evil concept...

######################################################################
##
## Internationalization (i18n) through msgcat and gettext. See
## http://www.gnu.org/software/gettext/manual/html_node/Tcl.html

package require msgcat

# Check for Windows 7 MUI language pack (missed by msgcat < 1.4.4)
if {[tk windowingsystem] eq "win32"
	&& [package vcompare [package provide msgcat] 1.4.4] < 0
} then {
	proc _mc_update_locale {} {
		set key {HKEY_CURRENT_USER\Control Panel\Desktop}
		if {![catch {
			package require registry
			set uilocale [registry get $key "PreferredUILanguages"]
			msgcat::ConvertLocale [string map {- _} [lindex $uilocale 0]]
		} uilocale]} {
			if {[string length $uilocale] > 0} {
				msgcat::mclocale $uilocale
			}
		}
	}
	_mc_update_locale
}

proc _mc_trim {fmt} {
	set cmk [string first @@ $fmt]
	if {$cmk > 0} {
		return [string range $fmt 0 [expr {$cmk - 1}]]
	}
	return $fmt
}

proc mc {en_fmt args} {
	set fmt [_mc_trim [::msgcat::mc $en_fmt]]
	if {[catch {set msg [eval [list format $fmt] $args]} err]} {
		set msg [eval [list format [_mc_trim $en_fmt]] $args]
	}
	return $msg
}

proc strcat {args} {
	return [join $args {}]
}

######################################################################
##
## On Mac, bring the current Wish process window to front

if {[tk windowingsystem] eq "aqua"} {
	catch {
		exec osascript -e [format {
			tell application "System Events"
				set frontmost of processes whose unix id is %d to true
			end tell
		} [pid]]
	}
}

######################################################################
##
## read only globals

set _appname {Git Gui}
set _isbare {}
set _gitexec {}
set _githtmldir {}
set _reponame {}
set _iscygwin {}
set _search_path {}
set _shellpath {@@SHELL_PATH@@}
	)tcl"_tcl;

	check_for_trace(argv);

	R"tcl(
# variable for the last merged branch (useful for a default when deleting
# branches).
set _last_merged_branch {}

proc shellpath {} {
	global _shellpath env
	if {[string match @@* $_shellpath]} {
		if {[info exists env(SHELL)]} {
			return $env(SHELL)
		} else {
			return /bin/sh
		}
	}
	return $_shellpath
}

proc appname {} {
	global _appname
	return $_appname
}

proc gitdir {args} {
	global _gitdir
	if {$args eq {}} {
		return $_gitdir
	}
	return [eval [list file join $_gitdir] $args]
}

proc gitexec {args} {
	global _gitexec
	if {$_gitexec eq {}} {
		if {[catch {set _gitexec [git --exec-path]} err]} {
			error "Git not installed?\n\n$err"
		}
		if {[is_Cygwin]} {
			set _gitexec [exec cygpath \
				--windows \
				--absolute \
				$_gitexec]
		} else {
			set _gitexec [file normalize $_gitexec]
		}
	}
	if {$args eq {}} {
		return $_gitexec
	}
	return [eval [list file join $_gitexec] $args]
}

proc githtmldir {args} {
	global _githtmldir
	if {$_githtmldir eq {}} {
		if {[catch {set _githtmldir [git --html-path]}]} {
			# Git not installed or option not yet supported
			return {}
		}
		if {[is_Cygwin]} {
			set _githtmldir [exec cygpath \
				--windows \
				--absolute \
				$_githtmldir]
		} else {
			set _githtmldir [file normalize $_githtmldir]
		}
	}
	if {$args eq {}} {
		return $_githtmldir
	}
	return [eval [list file join $_githtmldir] $args]
}

proc reponame {} {
	return $::_reponame
}

proc is_MacOSX {} {
	if {[tk windowingsystem] eq {aqua}} {
		return 1
	}
	return 0
}

proc is_Windows {} {
	if {$::tcl_platform(platform) eq {windows}} {
		return 1
	}
	return 0
}

proc is_Cygwin {} {
	global _iscygwin
	if {$_iscygwin eq {}} {
		if {$::tcl_platform(platform) eq {windows}} {
			if {[catch {set p [exec cygpath --windir]} err]} {
				set _iscygwin 0
			} else {
				set _iscygwin 1
				# Handle MSys2 which is only cygwin when MSYSTEM is MSYS.
				if {[info exists ::env(MSYSTEM)] && $::env(MSYSTEM) ne "MSYS"} {
					set _iscygwin 0
				}
			}
		} else {
			set _iscygwin 0
		}
	}
	return $_iscygwin
}

proc is_enabled {option} {
	global enabled_options
	if {[catch {set on $enabled_options($option)}]} {return 0}
	return $on
}

proc enable_option {option} {
	global enabled_options
	set enabled_options($option) 1
}

proc disable_option {option} {
	global enabled_options
	set enabled_options($option) 0
}

######################################################################
##
## config

proc is_many_config {name} {
	switch -glob -- $name {
	gui.recentrepo -
	remote.*.fetch -
	remote.*.push
		{return 1}
	*
		{return 0}
	}
}

proc is_config_true {name} {
	global repo_config
	if {[catch {set v $repo_config($name)}]} {
		return 0
	}
	set v [string tolower $v]
	if {$v eq {} || $v eq {true} || $v eq {1} || $v eq {yes} || $v eq {on}} {
		return 1
	} else {
		return 0
	}
}

proc is_config_false {name} {
	global repo_config
	if {[catch {set v $repo_config($name)}]} {
		return 0
	}
	set v [string tolower $v]
	if {$v eq {false} || $v eq {0} || $v eq {no} || $v eq {off}} {
		return 1
	} else {
		return 0
	}
}

proc get_config {name} {
	global repo_config
	if {[catch {set v $repo_config($name)}]} {
		return {}
	} else {
		return $v
	}
}

proc is_bare {} {
	global _isbare
	global _gitdir
	global _gitworktree

	if {$_isbare eq {}} {
		if {[catch {
			set _bare [git rev-parse --is-bare-repository]
			switch  -- $_bare {
			true { set _isbare 1 }
			false { set _isbare 0}
			default { throw }
			}
		}]} {
			if {[is_config_true core.bare]
				|| ($_gitworktree eq {}
					&& [lindex [file split $_gitdir] end] ne {.git})} {
				set _isbare 1
			} else {
				set _isbare 0
			}
		}
	}
	return $_isbare
}

######################################################################
##
## handy utils

proc _trace_exec {cmd} {
	if {!$::_trace} return
	set d {}
	foreach v $cmd {
		if {$d ne {}} {
			append d { }
		}
		if {[regexp {[ \t\r\n'"$?*]} $v]} {
			set v [sq $v]
		}
		append d $v
	}
	puts stderr $d
}

#'"  fix poor old emacs font-lock mode

proc _git_cmd {name} {
	global _git_cmd_path

	if {[catch {set v $_git_cmd_path($name)}]} {
		switch -- $name {
		  version   -
		--version   -
		--exec-path { return [list $::_git $name] }
		}

		set p [gitexec git-$name$::_search_exe]
		if {[file exists $p]} {
			set v [list $p]
		} elseif {[is_Windows] && [file exists [gitexec git-$name]]} {
			# Try to determine what sort of magic will make
			# git-$name go and do its thing, because native
			# Tcl on Windows doesn't know it.
			#
			set p [gitexec git-$name]
			set f [open $p r]
			set s [gets $f]
			close $f

			switch -glob -- [lindex $s 0] {
			#!*sh     { set i sh     }
			#!*perl   { set i perl   }
			#!*python { set i python }
			default   { error "git-$name is not supported: $s" }
			}

			upvar #0 _$i interp
			if {![info exists interp]} {
				set interp [_which $i]
			}
			if {$interp eq {}} {
				error "git-$name requires $i (not in PATH)"
			}
			set v [concat [list $interp] [lrange $s 1 end] [list $p]]
		} else {
			# Assume it is builtin to git somehow and we
			# aren't actually able to see a file for it.
			#
			set v [list $::_git $name]
		}
		set _git_cmd_path($name) $v
	}
	return $v
}

proc _which {what args} {
	global env _search_exe _search_path

	if {$_search_path eq {}} {
		if {[is_Cygwin] && [regexp {^(/|\.:)} $env(PATH)]} {
			set _search_path [split [exec cygpath \
				--windows \
				--path \
				--absolute \
				$env(PATH)] {;}]
			set _search_exe .exe
		} elseif {[is_Windows]} {
			set gitguidir [file dirname [info script]]
			regsub -all ";" $gitguidir "\\;" gitguidir
			set env(PATH) "$gitguidir;$env(PATH)"
			set _search_path [split $env(PATH) {;}]
			set _search_exe .exe
		} else {
			set _search_path [split $env(PATH) :]
			set _search_exe {}
		}
	}

	if {[is_Windows] && [lsearch -exact $args -script] >= 0} {
		set suffix {}
	} else {
		set suffix $_search_exe
	}

	foreach p $_search_path {
		set p [file join $p $what$suffix]
		if {[file exists $p]} {
			return [file normalize $p]
		}
	}
	return {}
}

# Test a file for a hashbang to identify executable scripts on Windows.
proc is_shellscript {filename} {
	if {![file exists $filename]} {return 0}
	set f [open $filename r]
	fconfigure $f -encoding binary
	set magic [read $f 2]
	close $f
	return [expr {$magic eq "#!"}]
}

# Run a command connected via pipes on stdout.
# This is for use with textconv filters and uses sh -c "..." to allow it to
# contain a command with arguments. On windows we must check for shell
# scripts specifically otherwise just call the filter command.
proc open_cmd_pipe {cmd path} {
	global env
	if {![file executable [shellpath]]} {
		set exe [auto_execok [lindex $cmd 0]]
		if {[is_shellscript [lindex $exe 0]]} {
			set run [linsert [auto_execok sh] end -c "$cmd \"\$0\"" $path]
		} else {
			set run [concat $exe [lrange $cmd 1 end] $path]
		}
	} else {
		set run [list [shellpath] -c "$cmd \"\$0\"" $path]
	}
	return [open |$run r]
}

proc _lappend_nice {cmd_var} {
	global _nice
	upvar $cmd_var cmd

	if {![info exists _nice]} {
		set _nice [_which nice]
		if {[catch {exec $_nice git version}]} {
			set _nice {}
		} elseif {[is_Windows] && [file dirname $_nice] ne [file dirname $::_git]} {
			set _nice {}
		}
	}
	if {$_nice ne {}} {
		lappend cmd $_nice
	}
}

proc git {args} {
	set fd [eval [list git_read] $args]
	fconfigure $fd -translation binary -encoding utf-8
	set result [string trimright [read $fd] "\n"]
	close $fd
	if {$::_trace} {
		puts stderr "< $result"
	}
	return $result
}

proc _open_stdout_stderr {cmd} {
	_trace_exec $cmd
	if {[catch {
			set fd [open [concat [list | ] $cmd] r]
		} err]} {
		if {   [lindex $cmd end] eq {2>@1}
		    && $err eq {can not find channel named "1"}
			} {
			# Older versions of Tcl 8.4 don't have this 2>@1 IO
			# redirect operator.  Fallback to |& cat for those.
			# The command was not actually started, so its safe
			# to try to start it a second time.
			#
			set fd [open [concat \
				[list | ] \
				[lrange $cmd 0 end-1] \
				[list |& cat] \
				] r]
		} else {
			error $err
		}
	}
	fconfigure $fd -eofchar {}
	return $fd
}

proc git_read {args} {
	set opt [list]

	while {1} {
		switch -- [lindex $args 0] {
		--nice {
			_lappend_nice opt
		}

		--stderr {
			lappend args 2>@1
		}

		default {
			break
		}

		}

		set args [lrange $args 1 end]
	}

	set cmdp [_git_cmd [lindex $args 0]]
	set args [lrange $args 1 end]

	return [_open_stdout_stderr [concat $opt $cmdp $args]]
}

proc git_write {args} {
	set opt [list]

	while {1} {
		switch -- [lindex $args 0] {
		--nice {
			_lappend_nice opt
		}

		default {
			break
		}

		}

		set args [lrange $args 1 end]
	}

	set cmdp [_git_cmd [lindex $args 0]]
	set args [lrange $args 1 end]

	_trace_exec [concat $opt $cmdp $args]
	return [open [concat [list | ] $opt $cmdp $args] w]
}

proc githook_read {hook_name args} {
	set pchook [gitdir hooks $hook_name]
	lappend args 2>@1

	# On Windows [file executable] might lie so we need to ask
	# the shell if the hook is executable.  Yes that's annoying.
	#
	if {[is_Windows]} {
		upvar #0 _sh interp
		if {![info exists interp]} {
			set interp [_which sh]
		}
		if {$interp eq {}} {
			error "hook execution requires sh (not in PATH)"
		}

		set scr {if test -x "$1";then exec "$@";fi}
		set sh_c [list $interp -c $scr $interp $pchook]
		return [_open_stdout_stderr [concat $sh_c $args]]
	}

	if {[file executable $pchook]} {
		return [_open_stdout_stderr [concat [list $pchook] $args]]
	}

	return {}
}

proc kill_file_process {fd} {
	set process [pid $fd]

	catch {
		if {[is_Windows]} {
			exec taskkill /pid $process
		} else {
			exec kill $process
		}
	}
}

proc gitattr {path attr default} {
	if {[catch {set r [git check-attr $attr -- $path]}]} {
		set r unspecified
	} else {
		set r [join [lrange [split $r :] 2 end] :]
		regsub {^ } $r {} r
	}
	if {$r eq {unspecified}} {
		return $default
	}
	return $r
}

proc sq {value} {
	regsub -all ' $value "'\\''" value
	return "'$value'"
}

proc load_current_branch {} {
	global current_branch is_detached

	set fd [open [gitdir HEAD] r]
	if {[gets $fd ref] < 1} {
		set ref {}
	}
	close $fd

	set pfx {ref: refs/heads/}
	set len [string length $pfx]
	if {[string equal -length $len $pfx $ref]} {
		# We're on a branch.  It might not exist.  But
		# HEAD looks good enough to be a branch.
		#
		set current_branch [string range $ref $len end]
		set is_detached 0
	} else {
		# Assume this is a detached head.
		#
		set current_branch HEAD
		set is_detached 1
	}
}

auto_load tk_optionMenu
rename tk_optionMenu real__tkOptionMenu
proc tk_optionMenu {w varName args} {
	set m [eval real__tkOptionMenu $w $varName $args]
	$m configure -font font_ui
	$w configure -font font_ui
	return $m
}

proc rmsel_tag {text} {
	$text tag conf sel \
		-background [$text cget -background] \
		-foreground [$text cget -foreground] \
		-borderwidth 0
	$text tag conf in_sel -background lightgray
	bind $text <Motion> break
	return $text
}

wm withdraw .
set root_exists 0
bind . <Visibility> {
	bind . <Visibility> {}
	set root_exists 1
}

if {[is_Windows]} {
	# TODO: wm iconbitmap . -default $oguilib/git-gui.ico
	set ::tk::AlwaysShowSelection 1
	bind . <Control-F2> {console show}

	# Spoof an X11 display for SSH
	if {![info exists env(DISPLAY)]} {
		set env(DISPLAY) :9999
	}
} else {
	catch {
		image create photo gitlogo -width 16 -height 16

		gitlogo put #33CC33 -to  7  0  9  2
		gitlogo put #33CC33 -to  4  2 12  4
		gitlogo put #33CC33 -to  7  4  9  6
		gitlogo put #CC3333 -to  4  6 12  8
		gitlogo put gray26  -to  4  9  6 10
		gitlogo put gray26  -to  3 10  6 12
		gitlogo put gray26  -to  8  9 13 11
		gitlogo put gray26  -to  8 11 10 12
		gitlogo put gray26  -to 11 11 13 14
		gitlogo put gray26  -to  3 12  5 14
		gitlogo put gray26  -to  5 13
		gitlogo put gray26  -to 10 13
		gitlogo put gray26  -to  4 14 12 15
		gitlogo put gray26  -to  5 15 11 16
		gitlogo redither

		image create photo gitlogo32 -width 32 -height 32
		gitlogo32 copy gitlogo -zoom 2 2

		wm iconphoto . -default gitlogo gitlogo32
	}
}

######################################################################
##
## config defaults

set cursor_ptr arrow
font create font_ui
if {[lsearch -exact [font names] TkDefaultFont] != -1} {
	eval [linsert [font actual TkDefaultFont] 0 font configure font_ui]
	eval [linsert [font actual TkFixedFont] 0 font create font_diff]
} else {
	font create font_diff -family Courier -size 10
	catch {
		label .dummy
		eval font configure font_ui [font actual [.dummy cget -font]]
		destroy .dummy
	}
}

font create font_uiitalic
font create font_uibold
font create font_diffbold
font create font_diffitalic

foreach class {Button Checkbutton Entry Label
		Labelframe Listbox Message
		Radiobutton Spinbox Text} {
	option add *$class.font font_ui
}
if {![is_MacOSX]} {
	option add *Menu.font font_ui
	option add *Entry.borderWidth 1 startupFile
	option add *Entry.relief sunken startupFile
	option add *RadioButton.anchor w startupFile
}
unset class

if {[is_Windows] || [is_MacOSX]} {
	option add *Menu.tearOff 0
}

if {[is_MacOSX]} {
	set M1B M1
	set M1T Cmd
} else {
	set M1B Control
	set M1T Ctrl
}
	)tcl"_tcl;

	m1t_pfx = "M1T"_tclvs + '-';
	m1b_pfx = "<" + "M1B"_tclvs + '-';

	R"tcl(
proc bind_button3 {w cmd} {
	bind $w <Any-Button-3> $cmd
	if {[is_MacOSX]} {
		# Mac OS X sends Button-2 on right click through three-button mouse,
		# or through trackpad right-clicking (two-finger touch + click).
		bind $w <Any-Button-2> $cmd
		bind $w <Control-Button-1> $cmd
	}
}

proc apply_config {} {
	global repo_config font_descs

	foreach option $font_descs {
		set name [lindex $option 0]
		set font [lindex $option 1]
		if {[catch {
			set need_weight 1
			foreach {cn cv} $repo_config(gui.$name) {
				if {$cn eq {-weight}} {
					set need_weight 0
				}
				font configure $font $cn $cv
			}
			if {$need_weight} {
				font configure $font -weight normal
			}
			} err]} {
			error_popup [strcat [mc "Invalid font specified in %s:" "gui.$name"] "\n\n$err"]
		}
		foreach {cn cv} [font configure $font] {
			font configure ${font}bold $cn $cv
			font configure ${font}italic $cn $cv
		}
		font configure ${font}bold -weight bold
		font configure ${font}italic -slant italic
	}
}

set default_config(branch.autosetupmerge) true
set default_config(merge.tool) {}
set default_config(mergetool.keepbackup) true
set default_config(merge.diffstat) true
set default_config(merge.summary) false
set default_config(merge.verbosity) 2
set default_config(user.name) {}
set default_config(user.email) {}

set default_config(gui.encoding) [encoding system]
set default_config(gui.matchtrackingbranch) false
set default_config(gui.textconv) true
set default_config(gui.pruneduringfetch) false
set default_config(gui.trustmtime) false
set default_config(gui.fastcopyblame) false
set default_config(gui.maxrecentrepo) 10
set default_config(gui.copyblamethreshold) 40
set default_config(gui.blamehistoryctx) 7
set default_config(gui.diffcontext) 5
set default_config(gui.diffopts) {}
set default_config(gui.commitmsgwidth) 75
set default_config(gui.newbranchtemplate) {}
set default_config(gui.spellingdictionary) {}
set default_config(gui.fontui) [font configure font_ui]
set default_config(gui.fontdiff) [font configure font_diff]
# TODO: this option should be added to the git-config documentation
set default_config(gui.maxfilesdisplayed) 5000
set default_config(gui.usettk) 1
set default_config(gui.warndetachedcommit) 1
set default_config(gui.tabsize) 8
set font_descs {
	{fontui   font_ui   {mc "Main Font"}}
	{fontdiff font_diff {mc "Diff/Console Font"}}
}
set default_config(gui.stageuntracked) ask
set default_config(gui.displayuntracked) true

######################################################################
##
## find git

set _git  [_which git]
if {$_git eq {}} {
	catch {wm withdraw .}
	tk_messageBox \
		-icon error \
		-type ok \
		-title [mc "git-gui: fatal error"] \
		-message [mc "Cannot find git in PATH."]
	exit 1
}

######################################################################
##
## version check

if {[catch {set _git_version [git --version]} err]} {
	catch {wm withdraw .}
	tk_messageBox \
		-icon error \
		-type ok \
		-title [mc "git-gui: fatal error"] \
		-message "Cannot determine Git version:

$err

[appname] requires Git 1.5.0 or later."
	exit 1
}
if {![regsub {^git version } $_git_version {} _git_version]} {
	catch {wm withdraw .}
	tk_messageBox \
		-icon error \
		-type ok \
		-title [mc "git-gui: fatal error"] \
		-message [strcat [mc "Cannot parse Git version string:"] "\n\n$_git_version"]
	exit 1
}

proc get_trimmed_version {s} {
    set r {}
    foreach x [split $s -._] {
        if {[string is integer -strict $x]} {
            lappend r $x
        } else {
            break
        }
    }
    return [join $r .]
}
set _real_git_version $_git_version
set _git_version [get_trimmed_version $_git_version]

if {![regexp {^[1-9]+(\.[0-9]+)+$} $_git_version]} {
	catch {wm withdraw .}
	if {[tk_messageBox \
		-icon warning \
		-type yesno \
		-default no \
		-title "[appname]: warning" \
		 -message [mc "Git version cannot be determined.

%s claims it is version '%s'.

%s requires at least Git 1.5.0 or later.

Assume '%s' is version 1.5.0?
" $_git $_real_git_version [appname] $_real_git_version]] eq {yes}} {
		set _git_version 1.5.0
	} else {
		exit 1
	}
}
unset _real_git_version

proc git-version {args} {
	global _git_version

	switch [llength $args] {
	0 {
		return $_git_version
	}

	2 {
		set op [lindex $args 0]
		set vr [lindex $args 1]
		set cm [package vcompare $_git_version $vr]
		return [expr $cm $op 0]
	}

	4 {
		set type [lindex $args 0]
		set name [lindex $args 1]
		set parm [lindex $args 2]
		set body [lindex $args 3]

		if {($type ne {proc} && $type ne {method})} {
			error "Invalid arguments to git-version"
		}
		if {[llength $body] < 2 || [lindex $body end-1] ne {default}} {
			error "Last arm of $type $name must be default"
		}

		foreach {op vr cb} [lrange $body 0 end-2] {
			if {[git-version $op $vr]} {
				return [uplevel [list $type $name $parm $cb]]
			}
		}

		return [uplevel [list $type $name $parm [lindex $body end]]]
	}

	default {
		error "git-version >= x"
	}

	}
}

if {[git-version < 1.5]} {
	catch {wm withdraw .}
	tk_messageBox \
		-icon error \
		-type ok \
		-title [mc "git-gui: fatal error"] \
		-message "[appname] requires Git 1.5.0 or later.

You are using [git-version]:

[git --version]"
	exit 1
}
	)tcl"_tcl;

	eval(lib_class);		// must be the first one
	eval(lib_blame);
	eval(lib_branch);
	eval(lib_branch_checkout);
	eval(lib_branch_create);
	eval(lib_branch_delete);
	eval(lib_branch_rename);
	eval(lib_browser);
	eval(lib_checkout_op);
	eval(lib_choose_font);
	eval(lib_choose_repository);
	eval(lib_choose_rev);
	eval(lib_commit);
	eval(lib_console);
	eval(lib_date);
	eval(lib_diff);
	eval(lib_encoding);
	eval(lib_error);
	eval(lib_index);
	eval(lib_line);
	eval(lib_logo);
	eval(lib_merge);
	eval(lib_mergetool);
	eval(lib_option);
	eval(lib_remote);
	eval(lib_remote_add);
	eval(lib_remote_branch_delete);
	eval(lib_search);
	eval(lib_shortcut);
	eval(lib_spellcheck);
	eval(lib_sshkey);
	eval(lib_status_bar);
	eval(lib_themed);
	eval(lib_tools);
	eval(lib_tools_dlg);
	eval(lib_transport);
	eval(lib_win32);

	R"tcl(
######################################################################
##
## config file parsing

git-version proc _parse_config {arr_name args} {
	>= 1.5.3 {
		upvar $arr_name arr
		array unset arr
		set buf {}
		catch {
			set fd_rc [eval \
				[list git_read config] \
				$args \
				[list --null --list]]
			fconfigure $fd_rc -translation binary -encoding utf-8
			set buf [read $fd_rc]
			close $fd_rc
		}
		foreach line [split $buf "\0"] {
			if {[regexp {^([^\n]+)\n(.*)$} $line line name value]} {
				if {[is_many_config $name]} {
					lappend arr($name) $value
				} else {
					set arr($name) $value
				}
			} elseif {[regexp {^([^\n]+)$} $line line name]} {
				# no value given, but interpreting them as
				# boolean will be handled as true
				set arr($name) {}
			}
		}
	}
	default {
		upvar $arr_name arr
		array unset arr
		catch {
			set fd_rc [eval [list git_read config --list] $args]
			while {[gets $fd_rc line] >= 0} {
				if {[regexp {^([^=]+)=(.*)$} $line line name value]} {
					if {[is_many_config $name]} {
						lappend arr($name) $value
					} else {
						set arr($name) $value
					}
				} elseif {[regexp {^([^=]+)$} $line line name]} {
					# no value given, but interpreting them as
					# boolean will be handled as true
					set arr($name) {}
				}
			}
			close $fd_rc
		}
	}
}

proc load_config {include_global} {
	global repo_config global_config system_config default_config

	if {$include_global} {
		_parse_config system_config --system
		_parse_config global_config --global
	}
	_parse_config repo_config

	foreach name [array names default_config] {
		if {[catch {set v $system_config($name)}]} {
			set system_config($name) $default_config($name)
		}
	}
	foreach name [array names system_config] {
		if {[catch {set v $global_config($name)}]} {
			set global_config($name) $system_config($name)
		}
		if {[catch {set v $repo_config($name)}]} {
			set repo_config($name) $system_config($name)
		}
	}
}
	)tcl"_tcl;

	auto subcommand = find_subcommand(argv0, argv);
	determine_features(subcommand, argv);

	R"tcl(
######################################################################
##
## execution environment

set have_tk85 [expr {[package vcompare $tk_version "8.5"] >= 0}]

# Suggest our implementation of askpass, if none is set
if {![info exists env(SSH_ASKPASS)]} {
	set env(SSH_ASKPASS) [gitexec git-gui--askpass]
}
	)tcl"_tcl;

	bool picked = discover_gitdir_prefix();
	if (picked)
	{
		"load_config 1"_tcl;
		"apply_config"_tcl;
		apply_theme_config();
		"choose_repository::pick"_tcl;
		repo.set_gitdir("_gitdir"_tclvs);
		repo.set_prefix("_prefix"_tclvs);
	}

	// _gitdir exists, so try loading the config
	"load_config 0"_tcl;
	"apply_config"_tcl;
	apply_theme_config();

	if (!discover_worktree())
		return 1;
	repo.init_name();

	R"tcl(
set env(GIT_DIR) $_gitdir
set env(GIT_WORK_TREE) $_gitworktree

######################################################################
##
## global init

set current_diff_path {}
set current_diff_side {}
set diff_actions [list]

set HEAD {}
set PARENT {}
set MERGE_HEAD [list]
set commit_type {}
set empty_tree {}
set current_branch {}
set is_detached 0
set current_diff_path {}
set is_3way_diff 0
set is_submodule_diff 0
set is_conflict_diff 0
set selected_commit_type new
set diff_empty_count 0

set nullid "0000000000000000000000000000000000000000"
set nullid2 "0000000000000000000000000000000000000001"

######################################################################
##
## task management

set rescan_active 0
set diff_active 0
set last_clicked {}

set disable_on_lock [list]
set index_lock_type none

proc lock_index {type} {
	global index_lock_type disable_on_lock

	if {$index_lock_type eq {none}} {
		set index_lock_type $type
		foreach w $disable_on_lock {
			uplevel #0 $w disabled
		}
		return 1
	} elseif {$index_lock_type eq "begin-$type"} {
		set index_lock_type $type
		return 1
	}
	return 0
}

proc unlock_index {} {
	global index_lock_type disable_on_lock

	set index_lock_type none
	foreach w $disable_on_lock {
		uplevel #0 $w normal
	}
}

######################################################################
##
## status

proc repository_state {ctvar hdvar mhvar} {
	global current_branch
	upvar $ctvar ct $hdvar hd $mhvar mh

	set mh [list]

	load_current_branch
	if {[catch {set hd [git rev-parse --verify HEAD]}]} {
		set hd {}
		set ct initial
		return
	}

	set merge_head [gitdir MERGE_HEAD]
	if {[file exists $merge_head]} {
		set ct merge
		set fd_mh [open $merge_head r]
		while {[gets $fd_mh line] >= 0} {
			lappend mh $line
		}
		close $fd_mh
		return
	}

	set ct normal
}

proc PARENT {} {
	global PARENT empty_tree

	set p [lindex $PARENT 0]
	if {$p ne {}} {
		return $p
	}
	if {$empty_tree eq {}} {
		set empty_tree [git mktree << {}]
	}
	return $empty_tree
}

proc force_amend {} {
	global selected_commit_type
	global HEAD PARENT MERGE_HEAD commit_type

	repository_state newType newHEAD newMERGE_HEAD
	set HEAD $newHEAD
	set PARENT $newHEAD
	set MERGE_HEAD $newMERGE_HEAD
	set commit_type $newType

	set selected_commit_type amend
	do_select_commit_type
}

proc rescan {after {honor_trustmtime 1}} {
	global HEAD PARENT MERGE_HEAD commit_type
	global ui_index ui_workdir ui_comm
	global rescan_active file_states
	global repo_config

	if {$rescan_active > 0 || ![lock_index read]} return

	repository_state newType newHEAD newMERGE_HEAD
	if {[string match amend* $commit_type]
		&& $newType eq {normal}
		&& $newHEAD eq $HEAD} {
	} else {
		set HEAD $newHEAD
		set PARENT $newHEAD
		set MERGE_HEAD $newMERGE_HEAD
		set commit_type $newType
	}

	array unset file_states

	if {!$::GITGUI_BCK_exists &&
		(![$ui_comm edit modified]
		|| [string trim [$ui_comm get 0.0 end]] eq {})} {
		if {[string match amend* $commit_type]} {
		} elseif {[load_message GITGUI_MSG utf-8]} {
		} elseif {[run_prepare_commit_msg_hook]} {
		} elseif {[load_message MERGE_MSG]} {
		} elseif {[load_message SQUASH_MSG]} {
		}
		$ui_comm edit reset
		$ui_comm edit modified false
	}

	if {$honor_trustmtime && $repo_config(gui.trustmtime) eq {true}} {
		rescan_stage2 {} $after
	} else {
		set rescan_active 1
		ui_status [mc "Refreshing file status..."]
		set fd_rf [git_read update-index \
			-q \
			--unmerged \
			--ignore-missing \
			--refresh \
			]
		fconfigure $fd_rf -blocking 0 -translation binary
		fileevent $fd_rf readable \
			[list rescan_stage2 $fd_rf $after]
	}
}

if {[is_Cygwin]} {
	set is_git_info_exclude {}
	proc have_info_exclude {} {
		global is_git_info_exclude

		if {$is_git_info_exclude eq {}} {
			if {[catch {exec test -f [gitdir info exclude]}]} {
				set is_git_info_exclude 0
			} else {
				set is_git_info_exclude 1
			}
		}
		return $is_git_info_exclude
	}
} else {
	proc have_info_exclude {} {
		return [file readable [gitdir info exclude]]
	}
}

proc rescan_stage2 {fd after} {
	global rescan_active buf_rdi buf_rdf buf_rlo

	if {$fd ne {}} {
		read $fd
		if {![eof $fd]} return
		close $fd
	}

	if {[package vcompare $::_git_version 1.6.3] >= 0} {
		set ls_others [list --exclude-standard]
	} else {
		set ls_others [list --exclude-per-directory=.gitignore]
		if {[have_info_exclude]} {
			lappend ls_others "--exclude-from=[gitdir info exclude]"
		}
		set user_exclude [get_config core.excludesfile]
		if {$user_exclude ne {} && [file readable $user_exclude]} {
			lappend ls_others "--exclude-from=[file normalize $user_exclude]"
		}
	}

	set buf_rdi {}
	set buf_rdf {}
	set buf_rlo {}

	set rescan_active 2
	ui_status [mc "Scanning for modified files ..."]
	if {[git-version >= "1.7.2"]} {
		set fd_di [git_read diff-index --cached --ignore-submodules=dirty -z [PARENT]]
	} else {
		set fd_di [git_read diff-index --cached -z [PARENT]]
	}
	set fd_df [git_read diff-files -z]

	fconfigure $fd_di -blocking 0 -translation binary -encoding binary
	fconfigure $fd_df -blocking 0 -translation binary -encoding binary

	fileevent $fd_di readable [list read_diff_index $fd_di $after]
	fileevent $fd_df readable [list read_diff_files $fd_df $after]

	if {[is_config_true gui.displayuntracked]} {
		set fd_lo [eval git_read ls-files --others -z $ls_others]
		fconfigure $fd_lo -blocking 0 -translation binary -encoding binary
		fileevent $fd_lo readable [list read_ls_others $fd_lo $after]
		incr rescan_active
	}
}

proc load_message {file {encoding {}}} {
	global ui_comm

	set f [gitdir $file]
	if {[file isfile $f]} {
		if {[catch {set fd [open $f r]}]} {
			return 0
		}
		fconfigure $fd -eofchar {}
		if {$encoding ne {}} {
			fconfigure $fd -encoding $encoding
		}
		set content [string trim [read $fd]]
		close $fd
		regsub -all -line {[ \r\t]+$} $content {} content
		$ui_comm delete 0.0 end
		$ui_comm insert end $content
		return 1
	}
	return 0
}

proc run_prepare_commit_msg_hook {} {
	global pch_error

	# prepare-commit-msg requires PREPARE_COMMIT_MSG exist.  From git-gui
	# it will be .git/MERGE_MSG (merge), .git/SQUASH_MSG (squash), or an
	# empty file but existent file.

	set fd_pcm [open [gitdir PREPARE_COMMIT_MSG] a]

	if {[file isfile [gitdir MERGE_MSG]]} {
		set pcm_source "merge"
		set fd_mm [open [gitdir MERGE_MSG] r]
		fconfigure $fd_mm -encoding utf-8
		puts -nonewline $fd_pcm [read $fd_mm]
		close $fd_mm
	} elseif {[file isfile [gitdir SQUASH_MSG]]} {
		set pcm_source "squash"
		set fd_sm [open [gitdir SQUASH_MSG] r]
		fconfigure $fd_sm -encoding utf-8
		puts -nonewline $fd_pcm [read $fd_sm]
		close $fd_sm
	} else {
		set pcm_source ""
	}

	close $fd_pcm

	set fd_ph [githook_read prepare-commit-msg \
			[gitdir PREPARE_COMMIT_MSG] $pcm_source]
	if {$fd_ph eq {}} {
		catch {file delete [gitdir PREPARE_COMMIT_MSG]}
		return 0;
	}

	ui_status [mc "Calling prepare-commit-msg hook..."]
	set pch_error {}

	fconfigure $fd_ph -blocking 0 -translation binary -eofchar {}
	fileevent $fd_ph readable \
		[list prepare_commit_msg_hook_wait $fd_ph]

	return 1;
}

proc prepare_commit_msg_hook_wait {fd_ph} {
	global pch_error

	append pch_error [read $fd_ph]
	fconfigure $fd_ph -blocking 1
	if {[eof $fd_ph]} {
		if {[catch {close $fd_ph}]} {
			ui_status [mc "Commit declined by prepare-commit-msg hook."]
			hook_failed_popup prepare-commit-msg $pch_error
			catch {file delete [gitdir PREPARE_COMMIT_MSG]}
			exit 1
		} else {
			load_message PREPARE_COMMIT_MSG
		}
		set pch_error {}
		catch {file delete [gitdir PREPARE_COMMIT_MSG]}
		return
        }
	fconfigure $fd_ph -blocking 0
	catch {file delete [gitdir PREPARE_COMMIT_MSG]}
}

proc read_diff_index {fd after} {
	global buf_rdi

	append buf_rdi [read $fd]
	set c 0
	set n [string length $buf_rdi]
	while {$c < $n} {
		set z1 [string first "\0" $buf_rdi $c]
		if {$z1 == -1} break
		incr z1
		set z2 [string first "\0" $buf_rdi $z1]
		if {$z2 == -1} break

		incr c
		set i [split [string range $buf_rdi $c [expr {$z1 - 2}]] { }]
		set p [string range $buf_rdi $z1 [expr {$z2 - 1}]]
		merge_state \
			[encoding convertfrom utf-8 $p] \
			[lindex $i 4]? \
			[list [lindex $i 0] [lindex $i 2]] \
			[list]
		set c $z2
		incr c
	}
	if {$c < $n} {
		set buf_rdi [string range $buf_rdi $c end]
	} else {
		set buf_rdi {}
	}

	rescan_done $fd buf_rdi $after
}

proc read_diff_files {fd after} {
	global buf_rdf

	append buf_rdf [read $fd]
	set c 0
	set n [string length $buf_rdf]
	while {$c < $n} {
		set z1 [string first "\0" $buf_rdf $c]
		if {$z1 == -1} break
		incr z1
		set z2 [string first "\0" $buf_rdf $z1]
		if {$z2 == -1} break

		incr c
		set i [split [string range $buf_rdf $c [expr {$z1 - 2}]] { }]
		set p [string range $buf_rdf $z1 [expr {$z2 - 1}]]
		merge_state \
			[encoding convertfrom utf-8 $p] \
			?[lindex $i 4] \
			[list] \
			[list [lindex $i 0] [lindex $i 2]]
		set c $z2
		incr c
	}
	if {$c < $n} {
		set buf_rdf [string range $buf_rdf $c end]
	} else {
		set buf_rdf {}
	}

	rescan_done $fd buf_rdf $after
}

proc read_ls_others {fd after} {
	global buf_rlo

	append buf_rlo [read $fd]
	set pck [split $buf_rlo "\0"]
	set buf_rlo [lindex $pck end]
	foreach p [lrange $pck 0 end-1] {
		set p [encoding convertfrom utf-8 $p]
		if {[string index $p end] eq {/}} {
			set p [string range $p 0 end-1]
		}
		merge_state $p ?O
	}
	rescan_done $fd buf_rlo $after
}

proc rescan_done {fd buf after} {
	global rescan_active current_diff_path
	global file_states repo_config
	upvar $buf to_clear

	if {![eof $fd]} return
	set to_clear {}
	close $fd
	if {[incr rescan_active -1] > 0} return

	prune_selection
	unlock_index
	display_all_files
	if {$current_diff_path ne {}} { reshow_diff $after }
	if {$current_diff_path eq {}} { select_first_diff $after }
}

proc prune_selection {} {
	global file_states selected_paths

	foreach path [array names selected_paths] {
		if {[catch {set still_here $file_states($path)}]} {
			unset selected_paths($path)
		}
	}
}

######################################################################
##
## ui helpers

proc mapicon {w state path} {
	global all_icons

	if {[catch {set r $all_icons($state$w)}]} {
		puts "error: no icon for $w state={$state} $path"
		return file_plain
	}
	return $r
}

proc mapdesc {state path} {
	global all_descs

	if {[catch {set r $all_descs($state)}]} {
		puts "error: no desc for state={$state} $path"
		return $state
	}
	return $r
}

proc ui_status {msg} {
	global main_status
	if {[info exists main_status]} {
		$main_status show $msg
	}
}

proc ui_ready {{test {}}} {
	global main_status
	if {[info exists main_status]} {
		$main_status show [mc "Ready."] $test
	}
}

proc escape_path {path} {
	regsub -all {\\} $path "\\\\" path
	regsub -all "\n" $path "\\n" path
	return $path
}

proc short_path {path} {
	return [escape_path [lindex [file split $path] end]]
}

set next_icon_id 0
set null_sha1 [string repeat 0 40]

proc merge_state {path new_state {head_info {}} {index_info {}}} {
	global file_states next_icon_id null_sha1

	set s0 [string index $new_state 0]
	set s1 [string index $new_state 1]

	if {[catch {set info $file_states($path)}]} {
		set state __
		set icon n[incr next_icon_id]
	} else {
		set state [lindex $info 0]
		set icon [lindex $info 1]
		if {$head_info eq {}}  {set head_info  [lindex $info 2]}
		if {$index_info eq {}} {set index_info [lindex $info 3]}
	}

	if     {$s0 eq {?}} {set s0 [string index $state 0]} \
	elseif {$s0 eq {_}} {set s0 _}

	if     {$s1 eq {?}} {set s1 [string index $state 1]} \
	elseif {$s1 eq {_}} {set s1 _}

	if {$s0 eq {A} && $s1 eq {_} && $head_info eq {}} {
		set head_info [list 0 $null_sha1]
	} elseif {$s0 ne {_} && [string index $state 0] eq {_}
		&& $head_info eq {}} {
		set head_info $index_info
	} elseif {$s0 eq {_} && [string index $state 0] ne {_}} {
		set index_info $head_info
		set head_info {}
	}

	set file_states($path) [list $s0$s1 $icon \
		$head_info $index_info \
		]
	return $state
}

proc display_file_helper {w path icon_name old_m new_m} {
	global file_lists

	if {$new_m eq {_}} {
		set lno [lsearch -sorted -exact $file_lists($w) $path]
		if {$lno >= 0} {
			set file_lists($w) [lreplace $file_lists($w) $lno $lno]
			incr lno
			$w conf -state normal
			$w delete $lno.0 [expr {$lno + 1}].0
			$w conf -state disabled
		}
	} elseif {$old_m eq {_} && $new_m ne {_}} {
		lappend file_lists($w) $path
		set file_lists($w) [lsort -unique $file_lists($w)]
		set lno [lsearch -sorted -exact $file_lists($w) $path]
		incr lno
		$w conf -state normal
		$w image create $lno.0 \
			-align center -padx 5 -pady 1 \
			-name $icon_name \
			-image [mapicon $w $new_m $path]
		$w insert $lno.1 "[escape_path $path]\n"
		$w conf -state disabled
	} elseif {$old_m ne $new_m} {
		$w conf -state normal
		$w image conf $icon_name -image [mapicon $w $new_m $path]
		$w conf -state disabled
	}
}

proc display_file {path state} {
	global file_states selected_paths
	global ui_index ui_workdir

	set old_m [merge_state $path $state]
	set s $file_states($path)
	set new_m [lindex $s 0]
	set icon_name [lindex $s 1]

	set o [string index $old_m 0]
	set n [string index $new_m 0]
	if {$o eq {U}} {
		set o _
	}
	if {$n eq {U}} {
		set n _
	}
	display_file_helper	$ui_index $path $icon_name $o $n

	if {[string index $old_m 0] eq {U}} {
		set o U
	} else {
		set o [string index $old_m 1]
	}
	if {[string index $new_m 0] eq {U}} {
		set n U
	} else {
		set n [string index $new_m 1]
	}
	display_file_helper	$ui_workdir $path $icon_name $o $n

	if {$new_m eq {__}} {
		unset file_states($path)
		catch {unset selected_paths($path)}
	}
}

proc display_all_files_helper {w path icon_name m} {
	global file_lists

	lappend file_lists($w) $path
	set lno [expr {[lindex [split [$w index end] .] 0] - 1}]
	$w image create end \
		-align center -padx 5 -pady 1 \
		-name $icon_name \
		-image [mapicon $w $m $path]
	$w insert end "[escape_path $path]\n"
}

set files_warning 0
proc display_all_files {} {
	global ui_index ui_workdir
	global file_states file_lists
	global last_clicked
	global files_warning

	$ui_index conf -state normal
	$ui_workdir conf -state normal

	$ui_index delete 0.0 end
	$ui_workdir delete 0.0 end
	set last_clicked {}

	set file_lists($ui_index) [list]
	set file_lists($ui_workdir) [list]

	set to_display [lsort [array names file_states]]
	set display_limit [get_config gui.maxfilesdisplayed]
	set displayed 0
	foreach path $to_display {
		set s $file_states($path)
		set m [lindex $s 0]
		set icon_name [lindex $s 1]

		if {$displayed > $display_limit && [string index $m 1] eq {O} } {
			if {!$files_warning} {
				# do not repeatedly warn:
				set files_warning 1
				info_popup [mc "Display limit (gui.maxfilesdisplayed = %s) reached, not showing all %s files." \
					$display_limit [llength $to_display]]
			}
			continue
		}

		set s [string index $m 0]
		if {$s ne {U} && $s ne {_}} {
			display_all_files_helper $ui_index $path \
				$icon_name $s
		}

		if {[string index $m 0] eq {U}} {
			set s U
		} else {
			set s [string index $m 1]
		}
		if {$s ne {_}} {
			display_all_files_helper $ui_workdir $path \
				$icon_name $s
			incr displayed
		}
	}

	$ui_index conf -state disabled
	$ui_workdir conf -state disabled
}

######################################################################
##
## icons

set filemask {
#define mask_width 14
#define mask_height 15
static unsigned char mask_bits[] = {
   0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f,
   0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f,
   0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f};
}

image create bitmap file_plain -background white -foreground black -data {
#define plain_width 14
#define plain_height 15
static unsigned char plain_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x02, 0x05, 0x02, 0x09, 0x02, 0x1f, 0x02, 0x10,
   0x02, 0x10, 0x02, 0x10, 0x02, 0x10, 0x02, 0x10, 0x02, 0x10, 0x02, 0x10,
   0x02, 0x10, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_mod -background white -foreground blue -data {
#define mod_width 14
#define mod_height 15
static unsigned char mod_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x7a, 0x05, 0x02, 0x09, 0x7a, 0x1f, 0x02, 0x10,
   0xfa, 0x17, 0x02, 0x10, 0xfa, 0x17, 0x02, 0x10, 0xfa, 0x17, 0x02, 0x10,
   0xfa, 0x17, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_fulltick -background white -foreground "#007000" -data {
#define file_fulltick_width 14
#define file_fulltick_height 15
static unsigned char file_fulltick_bits[] = {
   0xfe, 0x01, 0x02, 0x1a, 0x02, 0x0c, 0x02, 0x0c, 0x02, 0x16, 0x02, 0x16,
   0x02, 0x13, 0x00, 0x13, 0x86, 0x11, 0x8c, 0x11, 0xd8, 0x10, 0xf2, 0x10,
   0x62, 0x10, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_question -background white -foreground black -data {
#define file_question_width 14
#define file_question_height 15
static unsigned char file_question_bits[] = {
   0xfe, 0x01, 0x02, 0x02, 0xe2, 0x04, 0xf2, 0x09, 0x1a, 0x1b, 0x0a, 0x13,
   0x82, 0x11, 0xc2, 0x10, 0x62, 0x10, 0x62, 0x10, 0x02, 0x10, 0x62, 0x10,
   0x62, 0x10, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_removed -background white -foreground red -data {
#define file_removed_width 14
#define file_removed_height 15
static unsigned char file_removed_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x02, 0x05, 0x02, 0x09, 0x02, 0x1f, 0x02, 0x10,
   0x1a, 0x16, 0x32, 0x13, 0xe2, 0x11, 0xc2, 0x10, 0xe2, 0x11, 0x32, 0x13,
   0x1a, 0x16, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_merge -background white -foreground blue -data {
#define file_merge_width 14
#define file_merge_height 15
static unsigned char file_merge_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x62, 0x05, 0x62, 0x09, 0x62, 0x1f, 0x62, 0x10,
   0xfa, 0x11, 0xf2, 0x10, 0x62, 0x10, 0x02, 0x10, 0xfa, 0x17, 0x02, 0x10,
   0xfa, 0x17, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

image create bitmap file_statechange -background white -foreground green -data {
#define file_statechange_width 14
#define file_statechange_height 15
static unsigned char file_statechange_bits[] = {
   0xfe, 0x01, 0x02, 0x03, 0x02, 0x05, 0x02, 0x09, 0x02, 0x1f, 0x62, 0x10,
   0x62, 0x10, 0xba, 0x11, 0xba, 0x11, 0x62, 0x10, 0x62, 0x10, 0x02, 0x10,
   0x02, 0x10, 0x02, 0x10, 0xfe, 0x1f};
} -maskdata $filemask

	)tcl"_tcl;
	"ui_index"_tclv = ui_index;
	"ui_workdir"_tclv =  ui_workdir;
	R"tcl(

set all_icons(_$ui_index)   file_plain
set all_icons(A$ui_index)   file_plain
set all_icons(M$ui_index)   file_fulltick
set all_icons(D$ui_index)   file_removed
set all_icons(U$ui_index)   file_merge
set all_icons(T$ui_index)   file_statechange

set all_icons(_$ui_workdir) file_plain
set all_icons(M$ui_workdir) file_mod
set all_icons(D$ui_workdir) file_question
set all_icons(U$ui_workdir) file_merge
set all_icons(O$ui_workdir) file_plain
set all_icons(T$ui_workdir) file_statechange

set max_status_desc 0
foreach i {
		{__ {mc "Unmodified"}}

		{_M {mc "Modified, not staged"}}
		{M_ {mc "Staged for commit"}}
		{MM {mc "Portions staged for commit"}}
		{MD {mc "Staged for commit, missing"}}

		{_T {mc "File type changed, not staged"}}
		{MT {mc "File type changed, old type staged for commit"}}
		{AT {mc "File type changed, old type staged for commit"}}
		{T_ {mc "File type changed, staged"}}
		{TM {mc "File type change staged, modification not staged"}}
		{TD {mc "File type change staged, file missing"}}

		{_O {mc "Untracked, not staged"}}
		{A_ {mc "Staged for commit"}}
		{AM {mc "Portions staged for commit"}}
		{AD {mc "Staged for commit, missing"}}

		{_D {mc "Missing"}}
		{D_ {mc "Staged for removal"}}
		{DO {mc "Staged for removal, still present"}}

		{_U {mc "Requires merge resolution"}}
		{U_ {mc "Requires merge resolution"}}
		{UU {mc "Requires merge resolution"}}
		{UM {mc "Requires merge resolution"}}
		{UD {mc "Requires merge resolution"}}
		{UT {mc "Requires merge resolution"}}
	} {
	set text [eval [lindex $i 1]]
	if {$max_status_desc < [string length $text]} {
		set max_status_desc [string length $text]
	}
	set all_descs([lindex $i 0]) $text
}
unset i

######################################################################
##
## util

proc scrollbar2many {list mode args} {
	foreach w $list {eval $w $mode $args}
}

proc many2scrollbar {list mode sb top bottom} {
	$sb set $top $bottom
	foreach w $list {$w $mode moveto $top}
}
	)tcl"_tcl;

	auto incr_font_size = [](const std::string& font, int amt) {
		int sz = fonts(configure, font) -size();
		sz += amt;
		fonts(configure, font) -size(sz);
		fonts(configure, font + "bold"s) -size(sz);
		fonts(configure, font + "italic"s) -size(sz);
	};

	R"tcl(
######################################################################
##
## ui commands

set starting_gitk_msg [mc "Starting gitk... please wait..."]

proc do_gitk {revs {is_submodule false}} {
	global current_diff_path file_states current_diff_side ui_index
	global _gitdir _gitworktree

	# -- Always start gitk through whatever we were loaded with.  This
	#    lets us bypass using shell process on Windows systems.
	#
	set exe [_which gitk -script]
	set cmd [list [info nameofexecutable] $exe]
	if {$exe eq {}} {
		error_popup [mc "Couldn't find gitk in PATH"]
	} else {
		global env

		set pwd [pwd]

		if {!$is_submodule} {
			if {![is_bare]} {
				cd $_gitworktree
			}
		} else {
			cd $current_diff_path
			if {$revs eq {--}} {
				set s $file_states($current_diff_path)
				set old_sha1 {}
				set new_sha1 {}
				switch -glob -- [lindex $s 0] {
				M_ { set old_sha1 [lindex [lindex $s 2] 1] }
				_M { set old_sha1 [lindex [lindex $s 3] 1] }
				MM {
					if {$current_diff_side eq $ui_index} {
						set old_sha1 [lindex [lindex $s 2] 1]
						set new_sha1 [lindex [lindex $s 3] 1]
					} else {
						set old_sha1 [lindex [lindex $s 3] 1]
					}
				}
				}
				set revs $old_sha1...$new_sha1
			}
			# GIT_DIR and GIT_WORK_TREE for the submodule are not the ones
			# we've been using for the main repository, so unset them.
			# TODO we could make life easier (start up faster?) for gitk
			# by setting these to the appropriate values to allow gitk
			# to skip the heuristics to find their proper value
			unset env(GIT_DIR)
			unset env(GIT_WORK_TREE)
		}
		eval exec $cmd $revs "--" "--" &

		set env(GIT_DIR) $_gitdir
		set env(GIT_WORK_TREE) $_gitworktree
		cd $pwd

		ui_status $::starting_gitk_msg
		after 10000 {
			ui_ready $starting_gitk_msg
		}
	}
}

proc do_git_gui {} {
	global current_diff_path

	# -- Always start git gui through whatever we were loaded with.  This
	#    lets us bypass using shell process on Windows systems.
	#
	set exe [list [_which git]]
	if {$exe eq {}} {
		error_popup [mc "Couldn't find git gui in PATH"]
	} else {
		global env
		global _gitdir _gitworktree

		# see note in do_gitk about unsetting these vars when
		# running tools in a submodule
		unset env(GIT_DIR)
		unset env(GIT_WORK_TREE)

		set pwd [pwd]
		cd $current_diff_path

		eval exec $exe gui &

		set env(GIT_DIR) $_gitdir
		set env(GIT_WORK_TREE) $_gitworktree
		cd $pwd

		ui_status $::starting_gitk_msg
		after 10000 {
			ui_ready $starting_gitk_msg
		}
	}
}

proc do_explore {} {
	global _gitworktree
	set explorer {}
	if {[is_Cygwin] || [is_Windows]} {
		set explorer "explorer.exe"
	} elseif {[is_MacOSX]} {
		set explorer "open"
	} else {
		# freedesktop.org-conforming system is our best shot
		set explorer "xdg-open"
	}
	eval exec $explorer [list [file nativename $_gitworktree]] &
}

set is_quitting 0
set ret_code    1

proc terminate_me {win} {
	global ret_code
	if {$win ne {.}} return
	exit $ret_code
}

proc do_quit {{rc {1}}} {
	global ui_comm is_quitting repo_config commit_type
	global GITGUI_BCK_exists GITGUI_BCK_i
	global ui_comm_spell
	global ret_code use_ttk

	if {$is_quitting} return
	set is_quitting 1

	if {[winfo exists $ui_comm]} {
		# -- Stash our current commit buffer.
		#
		set save [gitdir GITGUI_MSG]
		if {$GITGUI_BCK_exists && ![$ui_comm edit modified]} {
			file rename -force [gitdir GITGUI_BCK] $save
			set GITGUI_BCK_exists 0
		} else {
			set msg [string trim [$ui_comm get 0.0 end]]
			regsub -all -line {[ \r\t]+$} $msg {} msg
			if {(![string match amend* $commit_type]
				|| [$ui_comm edit modified])
				&& $msg ne {}} {
				catch {
					set fd [open $save w]
					fconfigure $fd -encoding utf-8
					puts -nonewline $fd $msg
					close $fd
				}
			} else {
				catch {file delete $save}
			}
		}

		# -- Cancel our spellchecker if its running.
		#
		if {[info exists ui_comm_spell]} {
			$ui_comm_spell stop
		}

		# -- Remove our editor backup, its not needed.
		#
		after cancel $GITGUI_BCK_i
		if {$GITGUI_BCK_exists} {
			catch {file delete [gitdir GITGUI_BCK]}
		}

		# -- Stash our current window geometry into this repository.
		#
		set cfg_wmstate [wm state .]
		if {[catch {set rc_wmstate $repo_config(gui.wmstate)}]} {
			set rc_wmstate {}
		}
		if {$cfg_wmstate ne $rc_wmstate} {
			catch {git config gui.wmstate $cfg_wmstate}
		}
		if {$cfg_wmstate eq {zoomed}} {
			# on Windows wm geometry will lie about window
			# position (but not size) when window is zoomed
			# restore the window before querying wm geometry
			wm state . normal
		}
		set cfg_geometry [list]
		lappend cfg_geometry [wm geometry .]
		if {$use_ttk} {
			lappend cfg_geometry [.vpane sashpos 0]
			lappend cfg_geometry [.vpane.files sashpos 0]
		} else {
			lappend cfg_geometry [lindex [.vpane sash coord 0] 0]
			lappend cfg_geometry [lindex [.vpane.files sash coord 0] 1]
		}
		if {[catch {set rc_geometry $repo_config(gui.geometry)}]} {
			set rc_geometry {}
		}
		if {$cfg_geometry ne $rc_geometry} {
			catch {git config gui.geometry $cfg_geometry}
		}
	}

	set ret_code $rc

	# Briefly enable send again, working around Tk bug
	# http://sourceforge.net/tracker/?func=detail&atid=112997&aid=1821174&group_id=12997
	tk appname [appname]

	destroy .
}

proc do_rescan {} {
	rescan ui_ready
}

proc ui_do_rescan {} {
	rescan {force_first_diff ui_ready}
}

proc do_commit {} {
	commit_tree
}

proc next_diff {{after {}}} {
	global next_diff_p next_diff_w next_diff_i
	show_diff $next_diff_p $next_diff_w {} {} $after
}

proc find_anchor_pos {lst name} {
	set lid [lsearch -sorted -exact $lst $name]

	if {$lid == -1} {
		set lid 0
		foreach lname $lst {
			if {$lname >= $name} break
			incr lid
		}
	}

	return $lid
}

proc find_file_from {flist idx delta path mmask} {
	global file_states

	set len [llength $flist]
	while {$idx >= 0 && $idx < $len} {
		set name [lindex $flist $idx]

		if {$name ne $path && [info exists file_states($name)]} {
			set state [lindex $file_states($name) 0]

			if {$mmask eq {} || [regexp $mmask $state]} {
				return $idx
			}
		}

		incr idx $delta
	}

	return {}
}

proc find_next_diff {w path {lno {}} {mmask {}}} {
	global next_diff_p next_diff_w next_diff_i
	global file_lists ui_index ui_workdir

	set flist $file_lists($w)
	if {$lno eq {}} {
		set lno [find_anchor_pos $flist $path]
	} else {
		incr lno -1
	}

	if {$mmask ne {} && ![regexp {(^\^)|(\$$)} $mmask]} {
		if {$w eq $ui_index} {
			set mmask "^$mmask"
		} else {
			set mmask "$mmask\$"
		}
	}

	set idx [find_file_from $flist $lno 1 $path $mmask]
	if {$idx eq {}} {
		incr lno -1
		set idx [find_file_from $flist $lno -1 $path $mmask]
	}

	if {$idx ne {}} {
		set next_diff_w $w
		set next_diff_p [lindex $flist $idx]
		set next_diff_i [expr {$idx+1}]
		return 1
	} else {
		return 0
	}
}

proc next_diff_after_action {w path {lno {}} {mmask {}}} {
	global current_diff_path

	if {$path ne $current_diff_path} {
		return {}
	} elseif {[find_next_diff $w $path $lno $mmask]} {
		return {next_diff;}
	} else {
		return {reshow_diff;}
	}
}

proc select_first_diff {after} {
	global ui_workdir

	if {[find_next_diff $ui_workdir {} 1 {^_?U}] ||
	    [find_next_diff $ui_workdir {} 1 {[^O]$}]} {
		next_diff $after
	} else {
		uplevel #0 $after
	}
}

proc force_first_diff {after} {
	global ui_workdir current_diff_path file_states

	if {[info exists file_states($current_diff_path)]} {
		set state [lindex $file_states($current_diff_path) 0]
	} else {
		set state {OO}
	}

	set reselect 0
	if {[string first {U} $state] >= 0} {
		# Already a conflict, do nothing
	} elseif {[find_next_diff $ui_workdir $current_diff_path {} {^_?U}]} {
		set reselect 1
	} elseif {[string index $state 1] ne {O}} {
		# Already a diff & no conflicts, do nothing
	} elseif {[find_next_diff $ui_workdir $current_diff_path {} {[^O]$}]} {
		set reselect 1
	}

	if {$reselect} {
		next_diff $after
	} else {
		uplevel #0 $after
	}
}

proc toggle_or_diff {mode w args} {
	global file_states file_lists current_diff_path ui_index ui_workdir
	global last_clicked selected_paths

	if {$mode eq "click"} {
		foreach {x y} $args break
		set pos [split [$w index @$x,$y] .]
		foreach {lno col} $pos break
	} else {
		if {$mode eq "toggle"} {
			if {$w eq $ui_workdir} {
				do_add_selection
				set last_clicked {}
				return
			}
			if {$w eq $ui_index} {
				do_unstage_selection
				set last_clicked {}
				return
			}
		}

		if {$last_clicked ne {}} {
			set lno [lindex $last_clicked 1]
		} else {
			if {![info exists file_lists]
				|| ![info exists file_lists($w)]
				|| [llength $file_lists($w)] == 0} {
				set last_clicked {}
				return
			}
			set lno [expr {int([lindex [$w tag ranges in_diff] 0])}]
		}
		if {$mode eq "toggle"} {
			set col 0; set y 2
		} else {
			incr lno [expr {$mode eq "up" ? -1 : 1}]
			set col 1
		}
	}

	if {![info exists file_lists]
		|| ![info exists file_lists($w)]
		|| [llength $file_lists($w)] < $lno - 1} {
		set path {}
	} else {
		set path [lindex $file_lists($w) [expr {$lno - 1}]]
	}
	if {$path eq {}} {
		set last_clicked {}
		return
	}

	set last_clicked [list $w $lno]
	focus $w
	array unset selected_paths
	$ui_index tag remove in_sel 0.0 end
	$ui_workdir tag remove in_sel 0.0 end

	# Determine the state of the file
	if {[info exists file_states($path)]} {
		set state [lindex $file_states($path) 0]
	} else {
		set state {__}
	}

	# Restage the file, or simply show the diff
	if {$col == 0 && $y > 1} {
		# Conflicts need special handling
		if {[string first {U} $state] >= 0} {
			# $w must always be $ui_workdir, but...
			if {$w ne $ui_workdir} { set lno {} }
			merge_stage_workdir $path $lno
			return
		}

		if {[string index $state 1] eq {O}} {
			set mmask {}
		} else {
			set mmask {[^O]}
		}

		set after [next_diff_after_action $w $path $lno $mmask]

		if {$w eq $ui_index} {
			update_indexinfo \
				"Unstaging [short_path $path] from commit" \
				[list $path] \
				[concat $after [list ui_ready]]
		} elseif {$w eq $ui_workdir} {
			update_index \
				"Adding [short_path $path]" \
				[list $path] \
				[concat $after [list ui_ready]]
		}
	} else {
		set selected_paths($path) 1
		show_diff $path $w $lno
	}
}

proc add_one_to_selection {w x y} {
	global file_lists last_clicked selected_paths

	set lno [lindex [split [$w index @$x,$y] .] 0]
	set path [lindex $file_lists($w) [expr {$lno - 1}]]
	if {$path eq {}} {
		set last_clicked {}
		return
	}

	if {$last_clicked ne {}
		&& [lindex $last_clicked 0] ne $w} {
		array unset selected_paths
		[lindex $last_clicked 0] tag remove in_sel 0.0 end
	}

	set last_clicked [list $w $lno]
	if {[catch {set in_sel $selected_paths($path)}]} {
		set in_sel 0
	}
	if {$in_sel} {
		unset selected_paths($path)
		$w tag remove in_sel $lno.0 [expr {$lno + 1}].0
	} else {
		set selected_paths($path) 1
		$w tag add in_sel $lno.0 [expr {$lno + 1}].0
	}
}

proc add_range_to_selection {w x y} {
	global file_lists last_clicked selected_paths

	if {[lindex $last_clicked 0] ne $w} {
		toggle_or_diff click $w $x $y
		return
	}

	set lno [lindex [split [$w index @$x,$y] .] 0]
	set lc [lindex $last_clicked 1]
	if {$lc < $lno} {
		set begin $lc
		set end $lno
	} else {
		set begin $lno
		set end $lc
	}

	foreach path [lrange $file_lists($w) \
		[expr {$begin - 1}] \
		[expr {$end - 1}]] {
		set selected_paths($path) 1
	}
	$w tag add in_sel $begin.0 [expr {$end + 1}].0
}

proc show_more_context {} {
	global repo_config
	if {$repo_config(gui.diffcontext) < 99} {
		incr repo_config(gui.diffcontext)
		reshow_diff
	}
}

proc show_less_context {} {
	global repo_config
	if {$repo_config(gui.diffcontext) > 1} {
		incr repo_config(gui.diffcontext) -1
		reshow_diff
	}
}

######################################################################
##
## ui construction

set ui_comm {}
	)tcl"_tcl;

	// -- Menu Bar
	//
	auto mbar = ".mbar"s;
	menu(mbar) -tearoff(0);
	if ("is_MacOSX"_tcli) {
		// -- Apple Menu (Mac OS X only)
		//
		mbar << add(cascade) -menulabel("Apple") -submenu(mbar + ".apple"s);
		menu(mbar + ".apple"s);
	}
	mbar << add(cascade) -menulabel(mc("Repository")) -submenu(mbar + ".repository"s);
	mbar << add(cascade) -menulabel(mc("Edit")) -submenu(mbar + ".edit"s);
	if ("is_enabled branch"_tcli) {
		mbar << add(cascade) -menulabel(mc("Branch")) -submenu(mbar + ".branch"s);
	}
	if ("is_enabled multicommit"_tcli || "is_enabled singlecommit"_tcli) {
		mbar << add(cascade) -menulabel(mc("Commit@@noun")) -submenu(mbar + ".commit"s);
	}
	if ("is_enabled transport"_tcli) {
		mbar << add(cascade) -menulabel(mc("Merge")) -submenu(mbar + ".merge"s);
		mbar << add(cascade) -menulabel(mc("Remote")) -submenu(mbar + ".remote"s);
	}
	if ("is_enabled multicommit"_tcli || "is_enabled singlecommit"_tcli) {
		mbar << add(cascade) -menulabel(mc("Tools")) -submenu(mbar + ".tools"s);
	}

	// -- Repository Menu
	//
	auto mbarrepo = mbar + ".repository"s;
	menu(mbarrepo);

	if (!"is_bare"_tcli) {
		mbarrepo << add(command)
			-menulabel(mc("Explore Working Copy"))
			-command("do_explore"s);
	}

	if ("is_Windows"_tcli) {
		mbarrepo << add(command)
			-menulabel(mc("Git Bash"))
			-command("eval exec [auto_execok start] "
					"[list \"Git Bash\" bash --login -l &]"_tcl);
	}

	if ("is_Windows"_tcli || !"is_bare"_tcli) {
		mbarrepo << add(separator);
	}

	mbarrepo << add(command)
		-menulabel(mc("Browse Current Branch's Files"))
		-command("browser::new $current_branch"s);
	"set ui_browse_current [.mbar.repository index last]"_tcl;
	mbarrepo << add(command)
		-menulabel(mc("Browse Branch Files..."))
		-command("browser_open::dialog"s);
	mbarrepo << add(separator);

	mbarrepo << add(command)
		-menulabel(mc("Visualize Current Branch's History"))
		-command("do_gitk $current_branch"s);
	"set ui_visualize_current [.mbar.repository index last]"_tcl;
	mbarrepo << add(command)
		-menulabel(mc("Visualize All Branch History"))
		-command("do_gitk --all"s);
	mbarrepo << add(separator);

	R"tcl(
proc current_branch_write {args} {
	global current_branch
	.mbar.repository entryconf $::ui_browse_current \
		-label [mc "Browse %s's Files" $current_branch]
	.mbar.repository entryconf $::ui_visualize_current \
		-label [mc "Visualize %s's History" $current_branch]
}
trace add variable current_branch write current_branch_write
	)tcl"_tcl;

	if ("is_enabled multicommit"_tcli) {
		mbarrepo << add(command) -menulabel(mc("Database Statistics"))
			-command([&]() { do_stats(); });

		mbarrepo << add(command) -menulabel(mc("Compress Database"))
			-command([&]() { do_gc(); });

		mbarrepo << add(command) -menulabel(mc("Verify Database"))
			-command([&]() { do_fsck_objects(); });

		mbarrepo << add(separator);

		if ("is_Cygwin"_tcli) {
			mbarrepo << add(command)
				-menulabel(mc("Create Desktop Icon"))
				-command("do_cygwin_shortcut"s);
		} else if ("is_Windows"_tcli) {
			mbarrepo << add(command)
				-menulabel(mc("Create Desktop Icon"))
				-command("do_windows_shortcut"s);
		} else if ("is_MacOSX"_tcli) {
			mbarrepo << add(command)
				-menulabel(mc("Create Desktop Icon"))
				-command("do_macosx_app"s);
		}
	}

	if ("is_MacOSX"_tcli) {
		"proc ::tk::mac::Quit {args} { do_quit }"_tcl;
	} else {
		mbarrepo << add(command) -menulabel(mc("Quit"))
			-command("do_quit"s)
			-accelerator(M1T("Q"));
	}

	// -- Edit Menu
	//
	auto mbaredit = mbar + ".edit"s;
	menu(mbaredit);
	mbaredit << add(command) -menulabel(mc("Undo"))
		-command("catch {[focus] edit undo}")
		-accelerator(M1T("Z"));
	mbaredit << add(command) -menulabel(mc("Redo"))
		-command("catch {[focus] edit redo}")
		-accelerator(M1T("Y"));
	mbaredit << add(separator);
	mbaredit << add(command) -menulabel(mc("Cut"))
		-command("catch {tk_textCut [focus]}")
		-accelerator(M1T("X"));
	mbaredit << add(command) -menulabel(mc("Copy"))
		-command("catch {tk_textCopy [focus]}")
		-accelerator(M1T("C"));
	mbaredit << add(command) -menulabel(mc("Paste"))
		-command("catch {tk_textPaste [focus]; [focus] see insert}")
		-accelerator(M1T("V"));
	mbaredit << add(command) -menulabel(mc("Delete"))
		-command("catch {[focus] delete sel.first sel.last}")
		-accelerator("Del");
	mbaredit << add(separator);
	mbaredit << add(command) -menulabel(mc("Select All"))
		-command("catch {[focus] tag add sel 0.0 end}")
		-accelerator(M1T("A"));

	// -- Branch Menu
	//
	if ("is_enabled branch"_tcli) {
		auto mbarbranch = mbar + ".branch"s;
		menu(mbarbranch);

		mbarbranch << add(command) -menulabel(mc("Create..."))
			-command("branch_create::dialog"s)
			-accelerator(M1T("N"));
		"lappend disable_on_lock [list .mbar.branch entryconf "
			"[.mbar.branch index last] -state]"_tcl;

		mbarbranch << add(command) -menulabel(mc("Checkout..."))
			-command("branch_checkout::dialog"s)
			-accelerator(M1T("O"));
		"lappend disable_on_lock [list .mbar.branch entryconf "
			"[.mbar.branch index last] -state]"_tcl;

		mbarbranch << add(command) -menulabel(mc("Rename..."))
			-command("branch_rename::dialog"s);
		"lappend disable_on_lock [list .mbar.branch entryconf "
			"[.mbar.branch index last] -state]"_tcl;

		mbarbranch << add(command) -menulabel(mc("Delete..."))
			-command("branch_delete::dialog"s);
		"lappend disable_on_lock [list .mbar.branch entryconf "
			"[.mbar.branch index last] -state]"_tcl;

		mbarbranch << add(command) -menulabel(mc("Reset..."))
			-command("merge::reset_hard"s);
		"lappend disable_on_lock [list .mbar.branch entryconf "
			"[.mbar.branch index last] -state]"_tcl;
	}

	// -- Commit Menu
	//
	R"tcl(
proc commit_btn_caption {} {
	if {[is_enabled nocommit]} {
		return [mc "Done"]
	} else {
		return [mc Commit@@verb]
	}
}
	)tcl"_tcl;

	if ("is_enabled multicommit"_tcli || "is_enabled singlecommit"_tcli) {
		auto mbarcommit = mbar + ".commit"s;
		menu(mbarcommit);

		if (!"is_enabled nocommit"_tcli) {
			mbarcommit << add(radiobutton)
				-menulabel(mc("New Commit"))
				-command("do_select_commit_type"s)
				-variable("selected_commit_type"s)
				-value("new"s);
			"lappend disable_on_lock "
				"[list .mbar.commit entryconf [.mbar.commit index last] -state]"_tcl;

			mbarcommit << add(radiobutton)
				-menulabel(mc("Amend Last Commit"))
				-command("do_select_commit_type"s)
				-variable("selected_commit_type"s)
				-value("amend"s);
			"lappend disable_on_lock "
				"[list .mbar.commit entryconf [.mbar.commit index last] -state]"_tcl;

			mbarcommit << add(separator);
		}

		mbarcommit << add(command) -menulabel(mc("Rescan"))
			-command("ui_do_rescan"s)
			-accelerator("F5"s);
		"lappend disable_on_lock "
			"[list .mbar.commit entryconf [.mbar.commit index last] -state]"_tcl;

		mbarcommit << add(command) -menulabel(mc("Stage To Commit"))
			-command("do_add_selection"s)
			-accelerator(M1T("T"));
		"lappend disable_on_lock "
			"[list .mbar.commit entryconf [.mbar.commit index last] -state]"_tcl;

		mbarcommit << add(command) -menulabel(mc("Stage Changed Files To Commit"))
			-command("do_add_all")
			-accelerator(M1T("I"));
		"lappend disable_on_lock "
			"[list .mbar.commit entryconf [.mbar.commit index last] -state]"_tcl;

		mbarcommit << add(command) -menulabel(mc("Unstage From Commit"))
			-command("do_unstage_selection"s)
			-accelerator(M1T("U"));
		"lappend disable_on_lock "
			"[list .mbar.commit entryconf [.mbar.commit index last] -state]"_tcl;

		mbarcommit << add(command) -menulabel(mc("Revert Changes"))
			-command("do_revert_selection"s)
			-accelerator(M1T("J"));
		"lappend disable_on_lock "
			"[list .mbar.commit entryconf [.mbar.commit index last] -state]"_tcl;

		mbarcommit << add(separator);

		mbarcommit << add(command) -menulabel(mc("Show Less Context"))
			-command("show_less_context"s)
			-accelerator(M1T("-"));

		mbarcommit << add(command) -menulabel(mc("Show More Context"))
			-command("show_more_context"s)
			-accelerator(M1T("="));

		mbarcommit << add(separator);

		if (!"is_enabled nocommitmsg"_tcli) {
			mbarcommit << add(command) -menulabel(mc("Sign Off"))
				-command("do_signoff"s)
				-accelerator(M1T("S"));
		}

		mbarcommit << add(command) -menulabel("commit_btn_caption"_tcls)
			-command("do_commit"s)
			-accelerator(M1T("Return"));
		"lappend disable_on_lock "
			"[list .mbar.commit entryconf [.mbar.commit index last] -state]"_tcl;
	}

	// -- Merge Menu
	//
	if ("is_enabled branch"_tcli) {
		auto mbarmerge = mbar + ".merge"s;
		menu(mbarmerge);
		mbarmerge << add(command) -menulabel(mc("Local Merge..."))
			-command("merge::dialog"s)
			-accelerator(M1T("M"));
		"lappend disable_on_lock "
			"[list .mbar.merge entryconf [.mbar.merge index last] -state]"_tcl;
		mbarmerge << add(command) -menulabel(mc("Abort Merge..."))
			-command("merge::reset_hard");
		"lappend disable_on_lock "
			"[list .mbar.merge entryconf [.mbar.merge index last] -state]"_tcl;
	}

	// -- Transport Menu
	//
	if ("is_enabled transport"_tcli) {
		auto mbarremote = mbar + ".remote"s;
		menu(mbarremote);

		mbarremote << add(command)
			-menulabel(mc("Add..."))
			-command("remote_add::dialog"s)
			-accelerator(M1T("A"));
		mbarremote << add(command)
			-menulabel(mc("Push..."))
			-command("do_push_anywhere"s)
			-accelerator(M1T("P"));
		mbarremote << add(command)
			-menulabel(mc("Delete Branch..."))
			-command("remote_branch_delete::dialog"s);
	}

	if ("is_MacOSX"_tcli) {
		"proc ::tk::mac::ShowPreferences {} {do_options}"_tcl;
	} else {
		// -- Edit Menu
		//
		mbaredit << add(separator);
		mbaredit << add(command) -menulabel(mc("Options..."))
			-command("do_options"s);
	}

	// -- Tools Menu
	//
	if ("is_enabled multicommit"_tcli || "is_enabled singlecommit"_tcli) {
		"set tools_menubar .mbar.tools"_tcl;
		tools_menubar = mbar + ".tools"s;
		menu(tools_menubar);
		tools_menubar << add(separator);
		tools_menubar << add(command) -menulabel(mc("Add...")) -command("tools_add::dialog"s);
		tools_menubar << add(command) -menulabel(mc("Remove...")) -command("tools_remove::dialog"s);
		R"tcl(
		set tools_tailcnt 3
		if {[array names repo_config guitool.*.cmd] ne {}} {
			tools_populate_all
		}
		)tcl"_tcl;
	}

	// -- Help Menu
	//
	auto mbarhelp = mbar + ".help"s;
	mbar << add(cascade) -menulabel(mc("Help")) -submenu(mbarhelp);
	menu(mbarhelp);
	if ("is_MacOSX"_tcli) {
		".mbar.apple" << add(command) -menulabel(mc("About %s", appname))
			-command(do_about);
		".mbar.apple" << add(separator);
	} else {
		mbarhelp << add(command) -menulabel(mc("About %s", appname))
			-command(do_about);
	}
	"."s << configure() -submenu(mbar);

	fs::path doc_path = "githtmldir"_tcls;
	if (!doc_path.empty()) {
		doc_path /= "index.html"s;
	}

	std::string doc_url;
	if (fs::is_regular_file(doc_path)) {
		doc_url = "file:" + doc_path.generic_string();
	} else {
		doc_url = "http://www.kernel.org/pub/software/scm/git/docs/";
	}

	mbarhelp << add(command) -menulabel(mc("Online Documentation"))
		-command([=]() { eval("git \"web--browse\" " + doc_url); });

	mbarhelp << add(command) -menulabel(mc("Show SSH Key"))
		-command("do_ssh_key"s);

	// -- Standard bindings
	//
	wmprotocol("."s, "WM_DELETE_WINDOW"s, "do_quit"s);
	bind("all"s, M1B("q"), "do_quit"s);
	bind("all"s, M1B("Q"), "do_quit"s);
	bind("all"s, M1B("w"), [](const std::string& W) { destroy(winfo(toplevel, W)); }, event_W);
	bind("all"s, M1B("W"), [](const std::string& W) { destroy(winfo(toplevel, W)); }, event_W);

	if (int done = do_subcommand(subcommand, argv0, argv))
		return done-1;

	// -- Branch Control
	//
	frame(".branch"s);
	if (!useTtk())
		".branch"s << configure() -borderwidth(1) -relief("sunken"s);
	label(".branch.l1"s)
		-text(mc("Current Branch:"))
		-anchor(w)
		-justify(left);
	label(".branch.cb"s)
		-textvariable("current_branch"s)
		-anchor(w)
		-justify(left);
	pack(".branch.l1"s) -side(left);
	pack(".branch.cb"s) -side(left) -fill(x);
	pack(".branch"s) -side(top) -fill(x);

	// -- Main Window Layout
	//
	panedwindow(".vpane"s) -orient(horizontal);
	panedwindow(".vpane.files"s) -orient(vertical);
	if (useTtk()) {
		".vpane"s << add(".vpane.files"s);
	} else {
		".vpane"s << add(".vpane.files"s) -sticky("nsew"s) -height(100) -width(200);
	}
	pack(".vpane"s) -anchor(n) -side(top) -fill(both) -expand(1);

	// -- Working Directory File List
	//
	textframe(".vpane.files.workdir"s) -height(100) -width(200);
	tlabel(".vpane.files.workdir.title"s) -text(mc("Unstaged Changes"))
		-background("lightsalmon"s) -foreground("black"s);
	ttext(ui_workdir) -background("white"s) -foreground("black"s)
		-borderwidth(0)
		-width(20) -height(10)
		-wrap("none"s)
		-takefocus(1) -highlightthickness(1)
		-cursor(cursor_ptr)
		-xscrollcommand(".vpane.files.workdir.sx set"s)
		-yscrollcommand(".vpane.files.workdir.sy set"s)
		-state(disabled);
	scrollbar(".vpane.files.workdir.sx"s) -orient(horizontal)
		-command([&](const std::vector<std::string>& cmd) { ui_workdir << xview(cmd); });
	scrollbar(".vpane.files.workdir.sy"s) -orient(vertical)
		-command([&](const std::vector<std::string>& cmd) { ui_workdir << yview(cmd); });
	pack(".vpane.files.workdir.title"s) -side(top) -fill(Tk::x);
	pack(".vpane.files.workdir.sx"s) -side(bottom) -fill(Tk::x);
	pack(".vpane.files.workdir.sy"s) -side(right) -fill(Tk::y);
	pack(ui_workdir) -side(left) -fill(both) -expand(1);

	// -- Index File List
	//
	textframe(".vpane.files.index"s) -height(100) -width(200);
	tlabel(".vpane.files.index.title"s)
		-text(mc("Staged Changes (Will Commit)"))
		-background("lightgreen"s) -foreground("black"s);
	ttext(ui_index) -background("white"s) -foreground("black"s)
		-borderwidth(0)
		-width(20) -height(10)
		-wrap(none)
		-takefocus(1) -highlightthickness(1)
		-cursor(cursor_ptr)
		-xscrollcommand(".vpane.files.index.sx set"s)
		-yscrollcommand(".vpane.files.index.sy set"s)
		-state(disabled);
	scrollbar(".vpane.files.index.sx"s) -orient(horizontal)
		-command([&](const std::vector<std::string>& cmd) { ui_index << xview(cmd); });
	scrollbar(".vpane.files.index.sy"s) -orient(vertical)
		-command([&](const std::vector<std::string>& cmd) { ui_index << yview(cmd); });
	pack(".vpane.files.index.title"s) -side(top) -fill(Tk::x);
	pack(".vpane.files.index.sx"s) -side(bottom) -fill(Tk::x);
	pack(".vpane.files.index.sy"s) -side(right) -fill(Tk::y);
	pack(ui_index) -side(left) -fill(both) -expand(1);

	// -- Insert the workdir and index into the panes
	//
	".vpane.files"s << add(".vpane.files.workdir"s);
	".vpane.files"s << add(".vpane.files.index"s);
	if (!useTtk()) {
		".vpane.files"s << paneconfigure(".vpane.files.workdir") -sticky("news"s);
		".vpane.files"s << paneconfigure(".vpane.files.index") -sticky("news"s);
	}

	for (const auto& i: { ui_index, ui_workdir }) {
		eval("rmsel_tag "s + i);
		i << tag(configure, "in_diff"s) -background(std::string(i << tag(cget, "in_sel"s, background)));
	}

	// -- Diff and Commit Area
	//
	panedwindow(".vpane.lower"s) -orient(vertical);
	frame(".vpane.lower.commarea"s);
	frame(".vpane.lower.diff"s) -relief(sunken) -borderwidth(1) -height(500);
	".vpane.lower"s << add(".vpane.lower.diff"s);
	".vpane.lower"s << add(".vpane.lower.commarea"s);
	".vpane"s << add(".vpane.lower"s);
	if (useTtk()) {
		".vpane.lower pane .vpane.lower.diff"_tcl -weight(1);
		".vpane.lower pane .vpane.lower.commarea"_tcl -weight(0);
	} else {
		".vpane.lower"s << paneconfigure(".vpane.lower.diff"s) -stretch("always"s);
		".vpane.lower"s << paneconfigure(".vpane.lower.commarea"s) -stretch("never"s);
	}

	// -- Commit Area Buttons
	//
	frame(".vpane.lower.commarea.buttons"s);
	label(".vpane.lower.commarea.buttons.l"s) -text({})
		-anchor(Tk::w)
		-justify(left);
	pack(".vpane.lower.commarea.buttons.l"s) -side(top) -fill(Tk::x);
	pack(".vpane.lower.commarea.buttons"s) -side(left) -fill(Tk::y);

	button(".vpane.lower.commarea.buttons.rescan"s) -text(mc("Rescan"))
		-command("ui_do_rescan"s);
	pack(".vpane.lower.commarea.buttons.rescan"s) -side(top) -fill(Tk::x);
	"lappend disable_on_lock "
		"{.vpane.lower.commarea.buttons.rescan conf -state}"_tcl;

	button(".vpane.lower.commarea.buttons.incall"s) -text(mc("Stage Changed"))
		-command("do_add_all"s);
	pack(".vpane.lower.commarea.buttons.incall"s) -side(top) -fill(Tk::x);
	"lappend disable_on_lock "
		"{.vpane.lower.commarea.buttons.incall conf -state}"_tcl;

	if (!"is_enabled nocommitmsg"_tcli) {
		button(".vpane.lower.commarea.buttons.signoff"s) -text(mc("Sign Off"))
			-command("do_signoff"s);
		pack(".vpane.lower.commarea.buttons.signoff"s) -side(top) -fill(Tk::x);
	}

	button(".vpane.lower.commarea.buttons.commit"s) -text("commit_btn_caption"_tcls)
		-command("do_commit"s);
	pack(".vpane.lower.commarea.buttons.commit"s) -side(top) -fill(Tk::x);
	"lappend disable_on_lock "
		"{.vpane.lower.commarea.buttons.commit conf -state}"_tcl;

	if (!"is_enabled nocommit"_tcli) {
		button(".vpane.lower.commarea.buttons.push"s) -text(mc("Push"))
			-command("do_push_anywhere"s);
		pack(".vpane.lower.commarea.buttons.push"s) -side(top) -fill(Tk::x);
	}

	// -- Commit Message Buffer
	//
	frame(".vpane.lower.commarea.buffer"s);
	frame(".vpane.lower.commarea.buffer.header"s);
	eval("set ui_comm " + ui_comm);
	eval("set ui_coml " + ui_coml);

	if (!"is_enabled nocommit"_tcli) {
		radiobutton(".vpane.lower.commarea.buffer.header.new"s)
			-text(mc("New Commit"))
			-command("do_select_commit_type"s)
			-variable("selected_commit_type"s)
			-value("new"s);
		"lappend disable_on_lock "
			"[list .vpane.lower.commarea.buffer.header.new conf -state]"_tcl;
		radiobutton(".vpane.lower.commarea.buffer.header.amend"s)
			-text(mc("Amend Last Commit"))
			-command("do_select_commit_type"s)
			-variable("selected_commit_type"s)
			-value("amend"s);
		"lappend disable_on_lock "
			"[list .vpane.lower.commarea.buffer.header.amend conf -state]"_tcl;
	}

	label(ui_coml)
		-anchor(Tk::w)
		-justify(left);
	R"tcl(
proc trace_commit_type {varname args} {
	global ui_coml commit_type
	switch -glob -- $commit_type {
	initial       {set txt [mc "Initial Commit Message:"]}
	amend         {set txt [mc "Amended Commit Message:"]}
	amend-initial {set txt [mc "Amended Initial Commit Message:"]}
	amend-merge   {set txt [mc "Amended Merge Commit Message:"]}
	merge         {set txt [mc "Merge Commit Message:"]}
	*             {set txt [mc "Commit Message:"]}
	}
	$ui_coml conf -text $txt
}
trace add variable commit_type write trace_commit_type
	)tcl"_tcl;
	pack(ui_coml) -side(left) -fill(Tk::x);

	if (!"is_enabled nocommit"_tcli) {
		pack(".vpane.lower.commarea.buffer.header.amend"s) -side(right);
		pack(".vpane.lower.commarea.buffer.header.new"s) -side(right);
	}

	textframe(".vpane.lower.commarea.buffer.frame"s);
	ttext(ui_comm) -background("white"s) -foreground("black"s)
		-borderwidth(1)
		-undo(true)
		-maxundo(20)
		-autoseparators(true)
		-takefocus(1)
		-highlightthickness(1)
		-relief(sunken)
		-width("expr {$repo_config(gui.commitmsgwidth)}"_tcli) -height(9) -wrap(none)
		-font("font_diff"s)
		-yscrollcommand(".vpane.lower.commarea.buffer.frame.sby set"s);
	scrollbar(".vpane.lower.commarea.buffer.frame.sby"s)
		-command([&](const std::vector<std::string>& cmd) { ui_comm << yview(cmd); });

	pack(".vpane.lower.commarea.buffer.frame.sby"s) -side(right) -fill(Tk::y);
	pack(ui_comm) -side(left) -fill(Tk::y);
	pack(".vpane.lower.commarea.buffer.header"s) -side(top) -fill(Tk::x);
	pack(".vpane.lower.commarea.buffer.frame"s) -side(left) -fill(Tk::y);
	pack(".vpane.lower.commarea.buffer"s) -side(left) -fill(Tk::y);

	// -- Commit Message Buffer Context Menu
	//
	auto ctxm = ".vpane.lower.commarea.buffer.ctxm"s;
	menu(ctxm) -tearoff(0);
	ctxm << add(command)
		-menulabel(mc("Cut"))
		-command([&]() { tk_textCut(ui_comm); });
	ctxm << add(command)
		-menulabel(mc("Copy"))
		-command([&]() { tk_textCopy(ui_comm); });
	ctxm << add(command)
		-menulabel(mc("Paste"))
		-command([&]() { tk_textPaste(ui_comm); });
	ctxm << add(command)
		-menulabel(mc("Delete"))
		-command([&]() { try { ui_comm << deletetext("sel.first"s, "sel.last"s); } catch (const TkError&) {} });
	ctxm << add(separator);
	ctxm << add(command)
		-menulabel(mc("Select All"))
		-command([&]() { focus(ui_comm); ui_comm << tag(add, "sel"s, txt(0,0), Tk::end); });
	ctxm << add(command)
		-menulabel(mc("Copy All"))
		-command([&]() {
			ui_comm << tag(add, "sel"s, txt(0,0), Tk::end);
			tk_textCopy(ui_comm);
			ui_comm << tag(Tk::remove, "sel"s, txt(0,0), Tk::end);
		});
	ctxm << add(separator);
	ctxm << add(command)
		-menulabel(mc("Sign Off"))
		-command("do_signoff"s);
	eval("set ui_comm_ctxm "s + ctxm);

	// -- Diff Header
	//
	R"tcl(
proc trace_current_diff_path {varname args} {
	global current_diff_path diff_actions file_states
	if {$current_diff_path eq {}} {
		set s {}
		set f {}
		set p {}
		set o disabled
	} else {
		set p $current_diff_path
		set s [mapdesc [lindex $file_states($p) 0] $p]
		set f [mc "File:"]
		set p [escape_path $p]
		set o normal
	}

	.vpane.lower.diff.header.status configure -text $s
	.vpane.lower.diff.header.file configure -text $f
	.vpane.lower.diff.header.path configure -text $p
	foreach w $diff_actions {
		uplevel #0 $w $o
	}
}
trace add variable current_diff_path write trace_current_diff_path
	)tcl"_tcl;

	gold_frame(".vpane.lower.diff.header"s);
	tlabel(".vpane.lower.diff.header.status"s)
		-background("gold"s)
		-foreground("black"s)
		-width("expr {$max_status_desc}"_tcli)
		-anchor(Tk::w)
		-justify(left);
	tlabel(".vpane.lower.diff.header.file"s)
		-background("gold"s)
		-foreground("black"s)
		-anchor(Tk::w)
		-justify(left);
	tlabel(".vpane.lower.diff.header.path"s)
		-background("gold"s)
		-foreground("black"s)
		-anchor(Tk::w)
		-justify(left);
	pack(".vpane.lower.diff.header.status"s) -side(left);
	pack(".vpane.lower.diff.header.file"s) -side(left);
	pack(".vpane.lower.diff.header.path"s) -fill(Tk::x);
	ctxm = ".vpane.lower.diff.header.ctxm"s;
	menu(ctxm) -tearoff(0);
	ctxm << add(command)
		-menulabel(mc("Copy"))
		-command(R"tcl(
			clipboard clear
			clipboard append \
				-format STRING \
				-type STRING \
				-- $current_diff_path
		 )tcl"s);
	eval("lappend diff_actions [list "s + ctxm + " entryconf ["s + ctxm + " index last] -state]"s);
	bind_button3(".vpane.lower.diff.header.path"s, [=](int X, int Y) { tk_popup(ctxm, X, Y); });

	// -- Diff Body
	//
	textframe(".vpane.lower.diff.body"s);
	eval("set ui_diff "s + ui_diff);
	ttext(ui_diff) -background("white"s) -foreground("black"s)
		-borderwidth(0)
		-width(80) -height(5) -wrap(none)
		-font("font_diff"s)
		-takefocus(1) -highlightthickness(1)
		-xscrollcommand(".vpane.lower.diff.body.sbx set"s)
		-yscrollcommand(".vpane.lower.diff.body.sby set"s)
		-state(disabled);
	"catch {$ui_diff configure -tabstyle wordprocessor}"_tcl;
	scrollbar(".vpane.lower.diff.body.sbx"s) -orient(horizontal)
		-command([&](const std::vector<std::string>& cmd) { ui_diff << xview(cmd); });
	scrollbar(".vpane.lower.diff.body.sby"s) -orient(vertical)
		-command([&](const std::vector<std::string>& cmd) { ui_diff << yview(cmd); });
	pack(".vpane.lower.diff.body.sbx"s) -side(bottom) -fill(Tk::x);
	pack(".vpane.lower.diff.body.sby"s) -side(right) -fill(Tk::y);
	pack(ui_diff) -side(left) -fill(both) -expand(1);
	pack(".vpane.lower.diff.header"s) -side(top) -fill(Tk::x);
	pack(".vpane.lower.diff.body"s) -side(bottom) -fill(both) -expand(1);

	auto confcolor = [&](const std::string& n, const std::string& c) {
		ui_diff << tag(configure, "clr4"s  + n) -background(c);
		ui_diff << tag(configure, "clri4"s + n) -foreground(c);
		ui_diff << tag(configure, "clr3"s  + n) -foreground(c);
		ui_diff << tag(configure, "clri3"s + n) -background(c);
	};
	confcolor("0"s, "black"s);
	confcolor("1"s, "red4"s);
	confcolor("2"s, "green4"s);
	confcolor("3"s, "yellow4"s);
	confcolor("4"s, "blue4"s);
	confcolor("5"s, "magenta4"s);
	confcolor("6"s, "cyan4"s);
	confcolor("7"s, "grey60"s);
	ui_diff << tag(configure, "clr1"s) -font("font_diffbold"s);
	ui_diff << tag(configure, "clr4"s) -underline(1);

	ui_diff << tag(configure, "d_info"s) -foreground("blue"s) -font("font_diffbold"s);

	ui_diff << tag(configure, "d_cr"s) -elide(true);
	ui_diff << tag(configure, "d_@"s) -font("font_diffbold"s);
	ui_diff << tag(configure, "d_+"s) -foreground("#00a000"s);
	ui_diff << tag(configure, "d_-"s) -foreground("red"s);

	ui_diff << tag(configure, "d_++"s) -foreground("#00a000"s);
	ui_diff << tag(configure, "d_--"s) -foreground("red"s);
	ui_diff << tag(configure, "d_+s"s)
		-foreground("#00a000"s)
		-background("#e2effa"s);
	ui_diff << tag(configure, "d_-s"s)
		-foreground("red"s)
		-background("#e2effa"s);
	ui_diff << tag(configure, "d_s+"s)
		-foreground("#00a000"s)
		-background("ivory1"s);
	ui_diff << tag(configure, "d_s-"s)
		-foreground("red"s)
		-background("ivory1"s);

	ui_diff << tag(configure, "d<"s)
		-foreground("orange"s)
		-font("font_diffbold"s);
	ui_diff << tag(configure, "d="s)
		-foreground("orange"s)
		-font("font_diffbold"s);
	ui_diff << tag(configure, "d>"s)
		-foreground("orange"s)
		-font("font_diffbold"s);

	ui_diff << tag(raise, "sel"s);

	// -- Diff Body Context Menu
	//

	auto add_diff_actions_index_last = [&](const std::string& ctxm) {
		eval("lappend diff_actions [list "s + ctxm + " entryconf ["s + ctxm + " index last] -state]"s);
	};

	auto create_common_diff_popup = [&](const std::string& ctxm) {
		ctxm << add(command)
			-menulabel(mc("Refresh"))
			-command("reshow_diff"s);
		add_diff_actions_index_last(ctxm);
		ctxm << add(command)
			-menulabel(mc("Copy"))
			-command("tk_textCopy $ui_diff"s);
		add_diff_actions_index_last(ctxm);
		ctxm << add(command)
			-menulabel(mc("Select All"))
			-command([=]() {
				focus(ui_diff);
				ui_diff << tag(add, "sel"s, txt(0,0), Tk::end);
			});
		add_diff_actions_index_last(ctxm);
		ctxm << add(command)
			-menulabel(mc("Copy All"))
			-command([=]() {
				ui_diff << tag(add, "sel"s, txt(0,0), Tk::end);
				tk_textCopy(ui_diff);
				ui_diff << tag(Tk::remove, "sel"s, txt(0,0), Tk::end);
			});
		add_diff_actions_index_last(ctxm);
		ctxm << add(separator);
		ctxm << add(command)
			-menulabel(mc("Decrease Font Size"))
			-command([&]() { incr_font_size("font_diff"s, -1); });
		add_diff_actions_index_last(ctxm);
		ctxm << add(command)
			-menulabel(mc("Increase Font Size"))
			-command([&]() { incr_font_size("font_diff"s, 1); });
		add_diff_actions_index_last(ctxm);
		ctxm << add(separator);
		auto emenu  = ctxm + ".enc"s;
		menu(emenu);
		eval("build_encoding_menu "s + emenu + " [list force_diff_encoding]"s);
		ctxm << add(cascade)
			-menulabel(mc("Encoding"))
			-submenu(emenu);
		add_diff_actions_index_last(ctxm);
		ctxm << add(separator);
		ctxm << add(command) -menulabel(mc("Options..."))
			-command("do_options"s);
	};

	ctxm = ".vpane.lower.diff.body.ctxm"s;
	menu(ctxm) -tearoff(0);
	ctxm << add(command)
		-menulabel(mc("Apply/Reverse Hunk"))
		-command("apply_hunk $cursorX $cursorY"s);
	std::string ui_diff_applyhunk = eval(ctxm + " index last"s);
	add_diff_actions_index_last(ctxm);
	ctxm << add(command)
		-menulabel(mc("Apply/Reverse Line"))
		-command("apply_range_or_line $cursorX $cursorY; do_rescan"s);
	std::string ui_diff_applyline = eval(ctxm + " index last"s);
	add_diff_actions_index_last(ctxm);
	ctxm << add(separator);
	ctxm << add(command)
		-menulabel(mc("Show Less Context"))
		-command("show_less_context"s);
	add_diff_actions_index_last(ctxm);
	ctxm << add(command)
		-menulabel(mc("Show More Context"))
		-command("show_more_context"s);
	add_diff_actions_index_last(ctxm);
	ctxm << add(separator);
	create_common_diff_popup(ctxm);

	auto ctxmmg = ".vpane.lower.diff.body.ctxmmg"s;
	menu(ctxmmg) -tearoff(0);
	ctxmmg << add(command)
		-menulabel(mc("Run Merge Tool"))
		-command("merge_resolve_tool"s);
	add_diff_actions_index_last(ctxmmg);
	ctxmmg << add(separator);
	ctxmmg << add(command)
		-menulabel(mc("Use Remote Version"))
		-command("merge_resolve_one 3"s);
	add_diff_actions_index_last(ctxmmg);
	ctxmmg << add(command)
		-menulabel(mc("Use Local Version"))
		-command("merge_resolve_one 2"s);
	add_diff_actions_index_last(ctxmmg);
	ctxmmg << add(command)
		-menulabel(mc("Revert To Base"))
		-command("merge_resolve_one 1"s);
	add_diff_actions_index_last(ctxmmg);
	ctxmmg << add(separator);
	ctxmmg << add(command)
		-menulabel(mc("Show Less Context"))
		-command("show_less_context"s);
	add_diff_actions_index_last(ctxmmg);
	ctxmmg << add(command)
		-menulabel(mc("Show More Context"))
		-command("show_more_context"s);
	add_diff_actions_index_last(ctxmmg);
	ctxmmg << add(separator);
	create_common_diff_popup(ctxmmg);

	auto ctxmsm = ".vpane.lower.diff.body.ctxmsm"s;
	menu(ctxmsm) -tearoff(0);
	ctxmsm << add(command)
		-menulabel(mc("Visualize These Changes In The Submodule"))
		-command("do_gitk -- true"s);
	add_diff_actions_index_last(ctxmsm);
	ctxmsm << add(command)
		-menulabel(mc("Visualize Current Branch History In The Submodule"))
		-command("do_gitk {} true"s);
	add_diff_actions_index_last(ctxmsm);
	ctxmsm << add(command)
		-menulabel(mc("Visualize All Branch History In The Submodule"))
		-command("do_gitk --all true"s);
	add_diff_actions_index_last(ctxmsm);
	ctxmsm << add(separator);
	ctxmsm << add(command)
		-menulabel(mc("Start git gui In The Submodule"))
		-command("do_git_gui"s);
	add_diff_actions_index_last(ctxmsm);
	ctxmsm << add(separator);
	create_common_diff_popup(ctxmsm);

	R"tcl(
proc has_textconv {path} {
	if {[is_config_false gui.textconv]} {
		return 0
	}
	set filter [gitattr $path diff set]
	set textconv [get_config [join [list diff $filter textconv] .]]
	if {$filter ne {set} && $textconv ne {}} {
		return 1
	} else {
		return 0
	}
}
	)tcl"_tcl;

	auto popup_diff_menu = [=](int x, int y, int X, int Y) {
		"::cursorX"_tclv = x;
		"::cursorY"_tclv = y;
		std::string diffstate;
		if ("info exists file_states($current_diff_path)"_tcli) {
			diffstate = "lindex $file_states($current_diff_path) 0"_tcls;
		} else {
			diffstate = "__"s;
		}
		if (diffstate.find('U') != std::string::npos) {
			tk_popup(ctxmmg, X, Y);
		} else if ("::is_submodule_diff"_tclvi) {
			tk_popup(ctxmsm, X, Y);
		} else {
			bool has_range = !"$::ui_diff tag nextrange sel 0.0"_tcls.empty();
			std::string l, st, t;
			if (ui_index == "::current_diff_side"_tclvs) {
				l = mc("Unstage Hunk From Commit");
				if (has_range) {
					t = mc("Unstage Lines From Commit");
				} else {
					t = mc("Unstage Line From Commit");
				}
			} else {
				l = mc("Stage Hunk For Commit");
				if (has_range) {
					t = mc("Stage Lines For Commit");
				} else {
					t = mc("Stage Line For Commit");
				}
			}
			if ("::is_3way_diff"_tclvi
				|| "current_diff_path"_tclvs.empty()
				|| "__"s == diffstate
				|| "_O"s == diffstate
				|| diffstate.at(1) == 'T'
				|| diffstate.at(0) == 'T'
				|| "has_textconv $current_diff_path]"_tcli) {
				st = "disabled"s;
			} else {
				st = "normal"s;
			}
			ctxm << entryconfigure(ui_diff_applyhunk) -state(s) -menulabel(l);
			ctxm << entryconfigure(ui_diff_applyline) -state(s) -menulabel(t);
			tk_popup(ctxm, X, Y);
		}
	};
	bind_button3_xyXY(ui_diff, popup_diff_menu);

	R"tcl(
# -- Status Bar
#
set main_status [::status_bar::new .status]
pack .status -anchor w -side bottom -fill x
$main_status show [mc "Initializing..."]

# -- Load geometry
#
proc on_ttk_pane_mapped {w pane pos} {
	bind $w <Map> {}
	after 0 [list after idle [list $w sashpos $pane $pos]]
}
proc on_tk_pane_mapped {w pane x y} {
	bind $w <Map> {}
	after 0 [list after idle [list $w sash place $pane $x $y]]
}
proc on_application_mapped {} {
	global repo_config use_ttk
	bind . <Map> {}
	set gm $repo_config(gui.geometry)
	if {$use_ttk} {
		bind .vpane <Map> \
		    [list on_ttk_pane_mapped %W 0 [lindex $gm 1]]
		bind .vpane.files <Map> \
		    [list on_ttk_pane_mapped %W 0 [lindex $gm 2]]
	} else {
		bind .vpane <Map> \
		    [list on_tk_pane_mapped %W 0 \
			 [lindex $gm 1] \
			 [lindex [.vpane sash coord 0] 1]]
		bind .vpane.files <Map> \
		    [list on_tk_pane_mapped %W 0 \
			 [lindex [.vpane.files sash coord 0] 0] \
			 [lindex $gm 2]]
	}
	wm geometry . [lindex $gm 0]
}
if {[info exists repo_config(gui.geometry)]} {
	bind . <Map> [list on_application_mapped]
	wm geometry . [lindex $repo_config(gui.geometry) 0]
}

# -- Load window state
#
if {[info exists repo_config(gui.wmstate)]} {
	catch {wm state . $repo_config(gui.wmstate)}
}
	)tcl"_tcl;

	// -- Key Bindings
	//
	bind(ui_comm, M1B("Return"), "do_commit;break"s);
	bind(ui_comm, M1B("t"), "do_add_selection;break"s);
	bind(ui_comm, M1B("T"), "do_add_selection;break"s);
	bind(ui_comm, M1B("u"), "do_unstage_selection;break"s);
	bind(ui_comm, M1B("U"), "do_unstage_selection;break"s);
	bind(ui_comm, M1B("j"), "do_revert_selection;break"s);
	bind(ui_comm, M1B("J"), "do_revert_selection;break"s);
	bind(ui_comm, M1B("i"), "do_add_all;break"s);
	bind(ui_comm, M1B("I"), "do_add_all;break"s);
	bind(ui_comm, M1B("x"), "tk_textCut %W;break"s);
	bind(ui_comm, M1B("X"), "tk_textCut %W;break"s);
	bind(ui_comm, M1B("c"), "tk_textCopy %W;break"s);
	bind(ui_comm, M1B("C"), "tk_textCopy %W;break"s);
	bind(ui_comm, M1B("v"), "tk_textPaste %W; %W see insert; break"s);
	bind(ui_comm, M1B("V"), "tk_textPaste %W; %W see insert; break"s);
	bind(ui_comm, M1B("a"), "%W tag add sel 0.0 end;break"s);
	bind(ui_comm, M1B("A"), "%W tag add sel 0.0 end;break"s);
	bind(ui_comm, M1B("minus"), "show_less_context;break"s);
	bind(ui_comm, M1B("KP_Subtract"), "show_less_context;break"s);
	bind(ui_comm, M1B("equal"), "show_more_context;break"s);
	bind(ui_comm, M1B("plus"), "show_more_context;break"s);
	bind(ui_comm, M1B("KP_Add"), "show_more_context;break"s);

	R"tcl(
bind $ui_diff <$M1B-Key-x> {tk_textCopy %W;break}
bind $ui_diff <$M1B-Key-X> {tk_textCopy %W;break}
bind $ui_diff <$M1B-Key-c> {tk_textCopy %W;break}
bind $ui_diff <$M1B-Key-C> {tk_textCopy %W;break}
bind $ui_diff <$M1B-Key-v> {break}
bind $ui_diff <$M1B-Key-V> {break}
bind $ui_diff <$M1B-Key-a> {%W tag add sel 0.0 end;break}
bind $ui_diff <$M1B-Key-A> {%W tag add sel 0.0 end;break}
bind $ui_diff <$M1B-Key-j> {do_revert_selection;break}
bind $ui_diff <$M1B-Key-J> {do_revert_selection;break}
bind $ui_diff <Key-Up>     {catch {%W yview scroll -1 units};break}
bind $ui_diff <Key-Down>   {catch {%W yview scroll  1 units};break}
bind $ui_diff <Key-Left>   {catch {%W xview scroll -1 units};break}
bind $ui_diff <Key-Right>  {catch {%W xview scroll  1 units};break}
bind $ui_diff <Key-k>         {catch {%W yview scroll -1 units};break}
bind $ui_diff <Key-j>         {catch {%W yview scroll  1 units};break}
bind $ui_diff <Key-h>         {catch {%W xview scroll -1 units};break}
bind $ui_diff <Key-l>         {catch {%W xview scroll  1 units};break}
bind $ui_diff <Control-Key-b> {catch {%W yview scroll -1 pages};break}
bind $ui_diff <Control-Key-f> {catch {%W yview scroll  1 pages};break}
bind $ui_diff <Button-1>   {focus %W}
	)tcl"_tcl;

	if ("is_enabled branch"_tcli) {
		bind("."s, M1B("n"), "branch_create::dialog"s);
		bind("."s, M1B("N"), "branch_create::dialog"s);
		bind("."s, M1B("o"), "branch_checkout::dialog"s);
		bind("."s, M1B("O"), "branch_checkout::dialog"s);
		bind("."s, M1B("m"), "merge::dialog"s);
		bind("."s, M1B("M"), "merge::dialog"s);
	}
	if ("is_enabled transport"_tcli) {
		bind("."s, M1B("p"), "do_push_anywhere"s);
		bind("."s, M1B("P"), "do_push_anywhere"s);
	}

	bind("."s, "<Key-F5>"s, "ui_do_rescan"s);
	bind("."s, M1B("r"), "ui_do_rescan"s);
	bind("."s, M1B("R"), "ui_do_rescan"s);
	bind("."s, M1B("s"), "do_signoff"s);
	bind("."s, M1B("S"), "do_signoff"s);
	bind("."s, M1B("t"), "toggle_or_diff toggle %W"s);
	bind("."s, M1B("T"), "toggle_or_diff toggle %W"s);
	bind("."s, M1B("u"), "toggle_or_diff toggle %W"s);
	bind("."s, M1B("U"), "toggle_or_diff toggle %W"s);
	bind("."s, M1B("j"), "do_revert_selection"s);
	bind("."s, M1B("J"), "do_revert_selection"s);
	bind("."s, M1B("i"), "do_add_all"s);
	bind("."s, M1B("I"), "do_add_all"s);
	bind("."s, M1B("minus"), "show_less_context;break"s);
	bind("."s, M1B("KP_Subtract"), "show_less_context;break"s);
	bind("."s, M1B("equal"), "show_more_context;break"s);
	bind("."s, M1B("plus"), "show_more_context;break"s);
	bind("."s, M1B("KP_Add"), "show_more_context;break"s);
	bind("."s, M1B("Return"), "do_commit"s);
	bind("."s, M1B("KP_Enter"), "do_commit"s);
	for (const auto& i: { ui_index, ui_workdir }) {
		bind(i, "<Button-1>"s,          "toggle_or_diff click %W %x %y; break"s);
		bind(i, m1b_pfx + "Button-1>"s, "add_one_to_selection %W %x %y; break"s);
		bind(i, "<Shift-Button-1>"s,    "add_range_to_selection %W %x %y; break"s);
		bind(i, "<Key-Up>"s,            "toggle_or_diff up %W; break"s);
		bind(i, "<Key-Down>"s,          "toggle_or_diff down %W; break"s);
	}

	R"tcl(
set file_lists($ui_index) [list]
set file_lists($ui_workdir) [list]

wm title . "[appname] ([reponame]) [file normalize $_gitworktree]"
focus -force $ui_comm

# -- Warn the user about environmental problems.  Cygwin's Tcl
#    does *not* pass its env array onto any processes it spawns.
#    This means that git processes get none of our environment.
#
if {[is_Cygwin]} {
	set ignored_env 0
	set suggest_user {}
	set msg [mc "Possible environment issues exist.

The following environment variables are probably
going to be ignored by any Git subprocess run
by %s:

" [appname]]
	foreach name [array names env] {
		switch -regexp -- $name {
		{^GIT_INDEX_FILE$} -
		{^GIT_OBJECT_DIRECTORY$} -
		{^GIT_ALTERNATE_OBJECT_DIRECTORIES$} -
		{^GIT_DIFF_OPTS$} -
		{^GIT_EXTERNAL_DIFF$} -
		{^GIT_PAGER$} -
		{^GIT_TRACE$} -
		{^GIT_CONFIG$} -
		{^GIT_(AUTHOR|COMMITTER)_DATE$} {
			append msg " - $name\n"
			incr ignored_env
		}
		{^GIT_(AUTHOR|COMMITTER)_(NAME|EMAIL)$} {
			append msg " - $name\n"
			incr ignored_env
			set suggest_user $name
		}
		}
	}
	if {$ignored_env > 0} {
		append msg [mc "
This is due to a known issue with the
Tcl binary distributed by Cygwin."]

		if {$suggest_user ne {}} {
			append msg [mc "

A good replacement for %s
is placing values for the user.name and
user.email settings into your personal
~/.gitconfig file.
" $suggest_user]
		}
		warn_popup $msg
	}
	unset ignored_env msg suggest_user name
}

# -- Only initialize complex UI if we are going to stay running.
#
if {[is_enabled transport]} {
	load_all_remotes

	set n [.mbar.remote index end]
	populate_remotes_menu
	set n [expr {[.mbar.remote index end] - $n}]
	if {$n > 0} {
		if {[.mbar.remote type 0] eq "tearoff"} { incr n }
		.mbar.remote insert $n separator
	}
	unset n
}

if {[winfo exists $ui_comm]} {
	set GITGUI_BCK_exists [load_message GITGUI_BCK utf-8]

	# -- If both our backup and message files exist use the
	#    newer of the two files to initialize the buffer.
	#
	if {$GITGUI_BCK_exists} {
		set m [gitdir GITGUI_MSG]
		if {[file isfile $m]} {
			if {[file mtime [gitdir GITGUI_BCK]] > [file mtime $m]} {
				catch {file delete [gitdir GITGUI_MSG]}
			} else {
				$ui_comm delete 0.0 end
				$ui_comm edit reset
				$ui_comm edit modified false
				catch {file delete [gitdir GITGUI_BCK]}
				set GITGUI_BCK_exists 0
			}
		}
		unset m
	}

	proc backup_commit_buffer {} {
		global ui_comm GITGUI_BCK_exists

		set m [$ui_comm edit modified]
		if {$m || $GITGUI_BCK_exists} {
			set msg [string trim [$ui_comm get 0.0 end]]
			regsub -all -line {[ \r\t]+$} $msg {} msg

			if {$msg eq {}} {
				if {$GITGUI_BCK_exists} {
					catch {file delete [gitdir GITGUI_BCK]}
					set GITGUI_BCK_exists 0
				}
			} elseif {$m} {
				catch {
					set fd [open [gitdir GITGUI_BCK] w]
					fconfigure $fd -encoding utf-8
					puts -nonewline $fd $msg
					close $fd
					set GITGUI_BCK_exists 1
				}
			}

			$ui_comm edit modified false
		}

		set ::GITGUI_BCK_i [after 2000 backup_commit_buffer]
	}

	backup_commit_buffer

	# -- If the user has aspell available we can drive it
	#    in pipe mode to spellcheck the commit message.
	#
	set spell_cmd [list |]
	set spell_dict [get_config gui.spellingdictionary]
	lappend spell_cmd aspell
	if {$spell_dict ne {}} {
		lappend spell_cmd --master=$spell_dict
	}
	lappend spell_cmd --mode=none
	lappend spell_cmd --encoding=utf-8
	lappend spell_cmd pipe
	if {$spell_dict eq {none}
	 || [catch {set spell_fd [open $spell_cmd r+]} spell_err]} {
		bind_button3 $ui_comm [list tk_popup $ui_comm_ctxm %X %Y]
	} else {
		set ui_comm_spell [spellcheck::init \
			$spell_fd \
			$ui_comm \
			$ui_comm_ctxm \
		]
	}
	unset -nocomplain spell_cmd spell_fd spell_err spell_dict
}

lock_index begin-read
if {![winfo ismapped .]} {
	wm deiconify .
}
after 1 {
	if {[is_enabled initialamend]} {
		force_amend
	} else {
		do_rescan
	}

	if {[is_enabled nocommitmsg]} {
		$ui_comm configure -state disabled -background gray
	}
}
	)tcl"_tcl;
	if ("is_enabled multicommit"_tcli && !"is_config_false gui.gcwarning"_tcli) {
		after(1000, [&]() { hint_gc(); });
	}
	R"tcl(
if {[is_enabled retcode]} {
	bind . <Destroy> {+terminate_me %W}
}
	)tcl"_tcl;
	if (picked && "is_config_true gui.autoexplore"_tcli) {
		"do_explore"_tcl;
	}

	runEventLoop();
	return 0;
}

int main(int argc, char** argv)
{
	GitGui app;

	try {
		return app.main(argv[0], {argv+1, argv+argc});
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
	}
	return 1;
}
