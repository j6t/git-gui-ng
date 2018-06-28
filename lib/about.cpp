// git-gui about git-gui dialog
// Copyright (C) 2006, 2007 Shawn Pearce

#include "../git-gui.h"
#include "i18n.h"
#include <cpptk.h>

using namespace std::literals;
using namespace Tk;


static auto paddedlabel(std::string w)
{
	if (useTtk()) {
		return label(w) -style("Padded.TLabel");
	} else {
		return label(w)
			-padx(5) -pady(5)
			-justify(left)
			-anchor(Tk::w)
			-borderwidth(1)
			-relief(solid);
	}
}


void GitGui::do_about()
{
	auto tcl_patchLevel = "expr {$tcl_patchLevel}"_tcls;
	auto tk_patchLevel = "expr {$tk_patchLevel}"_tcls;

	auto w = ".about_dialog"s;
	eval("Dialog " + w);
	std::string rx("winfo rootx ."_tcl);
	std::string ry("winfo rooty ."_tcl);
	wm(geometry, w, "+" + rx + "+" + ry);

	pack(std::string(eval("git_logo " + w + ".git_logo")))
		-side(left) -fill(y) -padx(10) -pady(10);
	label(w + ".header"s) -text(mc("About %s", appname))
		-font("font_uibold") -anchor(center);
	pack(w + ".header"s) -side(top) -fill(x);

	frame(w + ".buttons"s);
	button(w + ".buttons.close"s) -text("Close")
		-defaultstate(active)
		-command([=]{ eval("destroy " + w); });
	pack(w + ".buttons.close"s) -side(right);
	pack(w + ".buttons"s) -side(bottom) -fill(x) -pady(10) -padx(10);

	paddedlabel(w + ".desc"s)
		-text(mc("git-gui - a graphical user interface for Git.") + '\n' + copyright);
	pack(w + ".desc"s) -side(top) -fill(x) -padx(5) -pady(5);

	std::string v;
	v += "git-gui version " + appvers + "\n";
	v += "git version"_tcls;
	v += "\n\n";
	if (tcl_patchLevel == tk_patchLevel) {
		v += "Tcl/Tk version ";
	} else {
		v += "Tcl version " + tcl_patchLevel;
		v +=", Tk version ";
	}
	v += tk_patchLevel;
	if ("info exists ui_comm_spell"_tcli
		&& !"$ui_comm_spell version"_tcls.empty()) {
		v += "\n";
		v += "$ui_comm_spell version"_tcls;
	}

	std::string d;
	d += "git wrapper: " + "expr {$::_git}"_tcls + "\n";
	d += "git exec dir: " + "gitexec"_tcls;

	paddedlabel(w + ".vers"s) -text(v);
	pack(w + ".vers"s) -side(top) -fill(x) -padx(5) -pady(5);

	paddedlabel(w + ".dirs"s) -text(d);
	pack(w + ".dirs"s) -side(top) -fill(x) -padx(5) -pady(5);

	menu(w + ".ctxm"s) -tearoff(0);
	(w + ".ctxm"s) << add(command)
		-menulabel("Copy")
		-command([wvers = w + ".vers"s]() {
			clipboard(clear);
			clipboard(append, wvers << cget(text))
				-format("STRING") -cliptype("STRING");
		});

	bind(w, "<Visibility>", [=]() { grab(w); focus(w + ".buttons.close"s); });
	auto dtor = [=]() { destroy(w); };
	bind(w, "<Key-Escape>", dtor);
	bind(w, "<Key-Return>", dtor);
	bind_button3(w + ".vers"s, [=](int x, int y) {
			tk_popup(w + ".ctxm"s, x, y); grab(w); focus(w);
		});
	wm(title, w, mc("About %s", appname));
	tkwait(window, w);
}
