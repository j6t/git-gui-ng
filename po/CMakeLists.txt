include(FindGettext)

if(NOT GETTEXT_FOUND)
	message(FATAL_ERROR "gettext is required")
endif(NOT GETTEXT_FOUND)

gettext_create_translations(git-gui.pot
	de.po
	el.po
	fr.po
	hu.po
	it.po
	ja.po
	nb.po
	pt_br.po
	pt_pt.po
	ru.po
	sv.po
	vi.po
	zh_cn.po
)
