cmake_minimum_required(VERSION 3.1)
project(GitGUI18)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(git-guing_SRCS
	git-gui.cpp
	lib/about.cpp
	lib/blame.cpp
	lib/branch.cpp
	lib/branch_checkout.cpp
	lib/branch_create.cpp
	lib/branch_delete.cpp
	lib/branch_rename.cpp
	lib/browser.cpp
	lib/checkout_op.cpp
	lib/choose_font.cpp
	lib/choose_repository.cpp
	lib/choose_rev.cpp
	lib/class.cpp
	lib/commit.cpp
	lib/console.cpp
	lib/database.cpp
	lib/date.cpp
	lib/diff.cpp
	lib/encoding.cpp
	lib/error.cpp
	lib/index.cpp
	lib/line.cpp
	lib/logo.cpp
	lib/merge.cpp
	lib/mergetool.cpp
	lib/option.cpp
	lib/remote.cpp
	lib/remote_add.cpp
	lib/remote_branch_delete.cpp
	lib/search.cpp
	lib/shortcut.cpp
	lib/spellcheck.cpp
	lib/sshkey.cpp
	lib/status_bar.cpp
	lib/themed.cpp
	lib/tools.cpp
	lib/tools_dlg.cpp
	lib/transport.cpp
	lib/win32.cpp
)

add_executable(git-guing ${git-guing_SRCS})
