// Functions for supporting the use of themed Tk widgets in git-gui.
// Copyright (C) 2009 Pat Thoyts <patthoyts@users.sourceforge.net>

#include "themed.h"

using namespace Tk;
using namespace std::literals;

std::string lib_themed = R"tcl(
proc ttk_get_current_theme {} {
	# Handle either current Tk or older versions of 8.5
	if {[catch {set theme [ttk::style theme use]}]} {
		set theme  $::ttk::currentTheme
	}
	return $theme
}

proc InitTheme {} {
	# Create a color label style (bg can be overridden by widget option)
	ttk::style layout Color.TLabel {
		Color.Label.border -sticky news -children {
			Color.label.fill -sticky news -children {
				Color.Label.padding -sticky news -children {
					Color.Label.label -sticky news}}}}
	eval [linsert [ttk::style configure TLabel] 0 \
			  ttk::style configure Color.TLabel]
	ttk::style configure Color.TLabel \
		-borderwidth 0 -relief flat -padding 2
	ttk::style map Color.TLabel -background {{} gold}
	# We also need a padded label.
	ttk::style configure Padded.TLabel \
		-padding {5 5} -borderwidth 1 -relief solid
	# We need a gold frame.
	ttk::style layout Gold.TFrame {
		Gold.Frame.border -sticky nswe -children {
			Gold.Frame.fill -sticky nswe}}
	ttk::style configure Gold.TFrame -background gold -relief flat
	# listboxes should have a theme border so embed in ttk::frame
	ttk::style layout SListbox.TFrame {
		SListbox.Frame.Entry.field -sticky news -border true -children {
			SListbox.Frame.padding -sticky news
		}
	}

	set theme [ttk_get_current_theme]

	if {[lsearch -exact {default alt classic clam} $theme] != -1} {
		# Simple override of standard ttk::entry to change the field
		# packground according to a state flag. We should use 'user1'
		# but not all versions of 8.5 support that so make use of 'pressed'
		# which is not normally in use for entry widgets.
		ttk::style layout Edged.Entry [ttk::style layout TEntry]
		ttk::style map Edged.Entry {*}[ttk::style map TEntry]
		ttk::style configure Edged.Entry {*}[ttk::style configure TEntry] \
			-fieldbackground lightgreen
		ttk::style map Edged.Entry -fieldbackground {
			{pressed !disabled} lightpink
		}
	} else {
		# For fancier themes, in particular the Windows ones, the field
		# element may not support changing the background color. So instead
		# override the fill using the default fill element. If we overrode
		# the vista theme field element we would loose the themed border
		# of the widget.
		catch {
			ttk::style element create color.fill from default
		}

		ttk::style layout Edged.Entry {
			Edged.Entry.field -sticky nswe -border 0 -children {
				Edged.Entry.border -sticky nswe -border 1 -children {
					Edged.Entry.padding -sticky nswe -children {
						Edged.Entry.color.fill -sticky nswe -children {
							Edged.Entry.textarea -sticky nswe
						}
					}
				}
			}
		}

		ttk::style configure Edged.Entry {*}[ttk::style configure TEntry] \
			-background lightgreen -padding 0 -borderwidth 0
		ttk::style map Edged.Entry {*}[ttk::style map TEntry] \
			-background {{pressed !disabled} lightpink}
	}

	if {[lsearch [bind . <<ThemeChanged>>] InitTheme] == -1} {
		bind . <<ThemeChanged>> +[namespace code [list InitTheme]]
	}
}

proc gold_frame {w args} {
	global use_ttk
	if {$use_ttk} {
		eval [linsert $args 0 ttk::frame $w -style Gold.TFrame]
	} else {
		eval [linsert $args 0 frame $w -background gold]
	}
}

proc tlabel {w args} {
	global use_ttk
	if {$use_ttk} {
		set cmd [list ttk::label $w -style Color.TLabel]
		foreach {k v} $args {
			switch -glob -- $k {
				-activebackground {}
				default { lappend cmd $k $v }
			}
		}
		eval $cmd
	} else {
		eval [linsert $args 0 label $w]
	}
}

# Create a toplevel for use as a dialog.
# If available, sets the EWMH dialog hint and if ttk is enabled
# place a themed frame over the surface.
proc Dialog {w args} {
	eval [linsert $args 0 toplevel $w -class Dialog]
	catch {wm attributes $w -type dialog}
	pave_toplevel $w
	return $w
}

# Tk toplevels are not themed - so pave it over with a themed frame to get
# the base color correct per theme.
proc pave_toplevel {w} {
	global use_ttk
	if {$use_ttk && ![winfo exists $w.!paving]} {
		set paving [ttk::frame $w.!paving]
		place $paving -x 0 -y 0 -relwidth 1 -relheight 1
		lower $paving
	}
}

# Create a scrolled listbox with appropriate border for the current theme.
# On many themes the border for a scrolled listbox needs to go around the
# listbox and the scrollbar.
proc slistbox {w args} {
	global use_ttk NS
	if {$use_ttk} {
		set f [ttk::frame $w -style SListbox.TFrame -padding 2]
	} else {
		set f [frame $w -relief flat]
	}
    if {[catch {
		if {$use_ttk} {
			eval [linsert $args 0 listbox $f.list -relief flat \
					  -highlightthickness 0 -borderwidth 0]
		} else {
			eval [linsert $args 0 listbox $f.list]
		}
        ${NS}::scrollbar $f.vs -command [list $f.list yview]
        $f.list configure -yscrollcommand [list $f.vs set]
        grid $f.list $f.vs -sticky news
        grid rowconfigure $f 0 -weight 1
        grid columnconfigure $f 0 -weight 1
		bind $f.list <<ListboxSelect>> \
			[list event generate $w <<ListboxSelect>>]
        interp hide {} $w
        interp alias {} $w {} $f.list
    } err]} {
        destroy $f
        return -code error $err
    }
    return $w
}

# fetch the background color from a widget.
proc get_bg_color {w} {
	global use_ttk
	if {$use_ttk} {
		set bg [ttk::style lookup [winfo class $w] -background]
	} else {
		set bg [$w cget -background]
	}
	return $bg
}

# ttk::spinbox didn't get added until 8.6
proc tspinbox {w args} {
	global use_ttk
	if {$use_ttk && [llength [info commands ttk::spinbox]] > 0} {
		eval [linsert $args 0 ttk::spinbox $w]
	} else {
		eval [linsert $args 0 spinbox $w]
	}
}

# Create a text widget with any theme specific properties.
proc ttext {w args} {
	global use_ttk
	if {$use_ttk} {
		switch -- [ttk_get_current_theme] {
			"vista" - "xpnative" {
				lappend args -highlightthickness 0 -borderwidth 0
			}
		}
	}
	set w [eval [linsert $args 0 text $w]]
	if {$use_ttk} {
		if {[winfo class [winfo parent $w]] eq "EntryFrame"} {
			bind $w <FocusIn> {[winfo parent %W] state focus}
			bind $w <FocusOut> {[winfo parent %W] state !focus}
		}
	}
	return $w
}

# themed frame suitable for surrounding a text field.
proc textframe {w args} {
	global use_ttk
	if {$use_ttk} {
		if {[catch {ttk::style layout EntryFrame}]} {
			InitEntryFrame
		}
		eval [linsert $args 0 ttk::frame $w -class EntryFrame -style EntryFrame]
	} else {
		eval [linsert $args 0 frame $w]
	}
	return $w
}

proc tentry {w args} {
	global use_ttk
	if {$use_ttk} {
		InitTheme
		ttk::entry $w -style Edged.Entry
	} else {
		entry $w
	}

	rename $w _$w
	interp alias {} $w {} tentry_widgetproc $w
	eval [linsert $args 0 tentry_widgetproc $w configure]
	return $w
}
proc tentry_widgetproc {w cmd args} {
	global use_ttk
	switch -- $cmd {
		state {
			if {$use_ttk} {
				return [uplevel 1 [list _$w $cmd] $args]
			} else {
				if {[lsearch -exact $args pressed] != -1} {
					_$w configure -background lightpink
				} else {
					_$w configure -background lightgreen
				}
			}
		}
		configure {
			if {$use_ttk} {
				if {[set n [lsearch -exact $args -background]] != -1} {
					set args [lreplace $args $n [incr n]]
					if {[llength $args] == 0} {return}
				}
			}
			return [uplevel 1 [list _$w $cmd] $args]
		}
		default { return [uplevel 1 [list _$w $cmd] $args] }
	}
}

# Tk 8.6 provides a standard font selection dialog. This uses the native
# dialogs on Windows and MacOSX or a standard Tk dialog on X11.
proc tchoosefont {w title familyvar sizevar} {
	if {[package vsatisfies [package provide Tk] 8.6]} {
		upvar #0 $familyvar family
		upvar #0 $sizevar size
		tk fontchooser configure -parent $w -title $title \
			-font [list $family $size] \
			-command [list on_choosefont $familyvar $sizevar]
		tk fontchooser show
	} else {
		choose_font::pick $w $title $familyvar $sizevar
	}
}

# Called when the Tk 8.6 fontchooser selects a font.
proc on_choosefont {familyvar sizevar font} {
	upvar #0 $familyvar family
	upvar #0 $sizevar size
	set font [font actual $font]
	set family [dict get $font -family]
	set size [dict get $font -size]
}

# Local variables:
# mode: tcl
# indent-tabs-mode: t
# tab-width: 4
# End:
)tcl";

// Define a style used for the surround of text widgets.
static void InitEntryFrame()
{
	R"tcl(
	ttk::style theme settings default {
		ttk::style layout EntryFrame {
			EntryFrame.field -sticky nswe -border 0 -children {
				EntryFrame.fill -sticky nswe -children {
					EntryFrame.padding -sticky nswe
				}
			}
		}
		ttk::style configure EntryFrame -padding 1 -relief sunken
		ttk::style map EntryFrame -background {}
	}
	ttk::style theme settings classic {
		ttk::style configure EntryFrame -padding 2 -relief sunken
		ttk::style map EntryFrame -background {}
	}
	ttk::style theme settings alt {
		ttk::style configure EntryFrame -padding 2
		ttk::style map EntryFrame -background {}
	}
	ttk::style theme settings clam {
		ttk::style configure EntryFrame -padding 2
		ttk::style map EntryFrame -background {}
	}

	# Ignore errors for missing native themes
	catch {
		ttk::style theme settings winnative {
			ttk::style configure EntryFrame -padding 2
		}
		ttk::style theme settings xpnative {
			ttk::style configure EntryFrame -padding 1
			ttk::style element create EntryFrame.field vsapi \
				EDIT 1 {disabled 4 focus 3 active 2 {} 1} -padding 1
		}
		ttk::style theme settings vista {
			ttk::style configure EntryFrame -padding 2
			ttk::style element create EntryFrame.field vsapi \
				EDIT 6 {disabled 4 focus 3 active 2 {} 1} -padding 2
		}
	}

	bind EntryFrame <Enter> {%W instate !disabled {%W state active}}
	bind EntryFrame <Leave> {%W state !active}
	bind EntryFrame <<ThemeChanged>> {
		set pad [ttk::style lookup EntryFrame -padding]
		%W configure -padding [expr {$pad eq {} ? 1 : $pad}]
	}
	)tcl"_tcl;
}

decltype(label({})) tlabel(const std::string& w)
{
	if (useTtk()) {
		return label(w) -style("Color.TLabel"s);
	} else {
		return label(w);
	}
}

// Create a text widget with any theme specific properties.
decltype(text({})) ttext(std::string w)
{
	w = std::string(textw(w));
	if (useTtk()) {
		auto theme = "ttk_get_current_theme"_tcls;
		if (theme == "vista" || theme == "xpnative")
			w << configure() -highlightthickness(0) -borderwidth(0);
		if (std::string(winfo(wndclass, winfo(parent, w))) == "EntryFrame") {
			bind(w, "<FocusIn>"s,
				[](const std::string& W) { winfo(parent, W) << "state focus"_tcl; }, event_W);
			bind(w, "<FocusOut>"s,
				[](const std::string& W) { winfo(parent, W) << "state !focus"_tcl; }, event_W);
		}
	}
	return w << configure();
}

// themed frame suitable for surrounding a text field.
decltype(frame({})) textframe(const std::string& w)
{
	if (useTtk()) {
		if ("catch {ttk::style layout EntryFrame}"_tcli) {
			InitEntryFrame();
		}
		return frame(w) -wndclass("EntryFrame"s) -style("EntryFrame"s);
	} else {
		return frame(w);
	}
}
