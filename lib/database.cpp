// git-gui object database management support
// Copyright (C) 2006, 2007 Shawn Pearce

#include "../git-gui.h"
#include "i18n.h"
#include <cpptk.h>
#include <map>
#include <boost/lexical_cast.hpp>

using namespace std::literals;
using namespace Tk;

void GitGui::do_stats()
{
	std::map<std::string,std::string> stats;
	"set fd [git_read count-objects -v]"_tcl;
	std::string line;
	while (!(line = "gets $fd"_tcls).empty()) {
		auto pos = line.find(": ");
		if (pos != std::string::npos && pos > 0)
			stats.emplace(line.substr(0, pos), line.substr(pos + 2));
	}
	"close $fd"_tcl;

	uintmax_t packed_sz = 0;
	auto dir = repo.gitdir() / "objects"s / "pack";
	boost::system::error_code ec;
	for (fs::directory_entry& e: fs::directory_iterator(dir, ec))
	{
		try {
			if (e.status().type() == fs::regular_file) {
				packed_sz += fs::file_size(e.path());
			}
		} catch (const fs::filesystem_error&) { /* ignore */ }
	}
	if (packed_sz > 0) {
		stats.emplace("size-pack"s, boost::lexical_cast<std::string>(packed_sz / 1024));
	}

	auto w = ".stats_view"s;
	eval("Dialog " + w);
	wm(withdraw, w);
	std::string rx(winfo(rootx, "."));
	std::string ry(winfo(rooty, "."));
	wm(geometry, w, "+" + rx + "+" + ry);

	frame(w + ".buttons"s);
	button(w + ".buttons.close"s) -text(mc("Close"))
		-defaultstate(active)
		-command([=]() { destroy(w); });
	button(w + ".buttons.gc"s) -text(mc("Compress Database"))
		-defaultstate(normal)
		-command([=]() { destroy(w); do_gc(); });
	pack(w + ".buttons.close"s) -side(right);
	pack(w + ".buttons.gc"s) -side(left);
	pack(w + ".buttons"s) -side(bottom) -fill(x) -pady(10) -padx(10);

	labelframe(w + ".stat"s) -text(mc("Database Statistics"));
	struct { std::string name, label, unit; } opts[] = {
		{"count",           mc("Number of loose objects")},
		{"size",            mc("Disk space used by loose objects"), " KiB"},
		{"in-pack",         mc("Number of packed objects")},
		{"packs",           mc("Number of packs")},
		{"size-pack",       mc("Disk space used by packed objects"), " KiB"},
		{"prune-packable",  mc("Packed objects waiting for pruning")},
		{"garbage",         mc("Garbage files")},
	};
	for (const auto& s: opts)
	{
		auto i = stats.find(s.name);
		if (i == stats.end())
			continue;
		auto value = i->second + s.unit;

		label(w + ".stat.l_"s + s.name) -text(mc("%s:", s.label)) -anchor(Tk::w);
		label(w + ".stat.v_"s + s.name) -text(value) -anchor(Tk::w);
		grid(configure, w + ".stat.l_"s + s.name, w + ".stat.v_"s + s.name)
			-sticky("we"s) -padx("{0 5}"s);
	}
	pack(w + ".stat"s) -pady(10) -padx(10);

	bind(w, "<Visibility>"s, [=]() { grab(w); focus(w + ".buttons.close"s); });
	bind(w, "<Key-Escape>"s, [=]() { destroy(w); });
	bind(w, "<Key-Return>"s, [=]() { destroy(w); });
	wm(title, w, mc("%s (%s): Database Statistics", appname, repo.name()));
	wm(deiconify, w);
	tkwait(window, w);
}

void GitGui::do_gc()
{
	R"tcl(
	set w [console::new {gc} [mc "Compressing the object database"]]
	console::chain $w {
		{exec git pack-refs --prune}
		{exec git reflog expire --all}
		{exec git repack -a -d -l}
		{exec git rerere gc}
	}
	)tcl"_tcl;
}

void GitGui::do_fsck_objects()
{
	R"tcl(
	set w [console::new {fsck-objects} \
		[mc "Verifying the object database with fsck-objects"]]
	set cmd [list git fsck-objects]
	lappend cmd --full
	lappend cmd --cache
	lappend cmd --strict
	console::exec $w $cmd
	)tcl"_tcl;
}

void GitGui::hint_gc()
{
	auto isWin = "is_Windows"_tcli;
	auto ndirs = isWin ? 4 : 1;
	auto limit = isWin ? 1 : 8;

	auto objdir = repo.gitdir() / "objects"s;
	int count = 0;
	for (int digit = '0'; digit < '0'+ndirs; digit++)
	{
		try {
			auto p = objdir / ("4"s + char(digit));
			for (fs::directory_entry& f: fs::directory_iterator(p))
				++count, (void)f;
		} catch (const fs::filesystem_error&) { /* ignore */ }
	}

	if (count >= limit * ndirs) {
		auto objects_current = count * 256/ndirs;
		if (ask_popup(
			mc("This repository currently has approximately %s loose objects.\n\n"
"To maintain optimal performance it is strongly recommended that you compress the database.\n\n"
"Compress the database now?", boost::lexical_cast<std::string>(objects_current))) == "yes") {
			do_gc();
		}
	}
}
