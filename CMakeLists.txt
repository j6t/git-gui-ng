cmake_minimum_required(VERSION 3.2)
project(GitGUING)
set(GITGUING_VERSION 0.30.0)

include(CheckCXXCompilerFlag)
include(FindIntl)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(TCL REQUIRED)
include_directories(${TCL_INCLUDE_PATH} ${TK_INCLUDE_PATH})

find_package(Boost 1.60 REQUIRED COMPONENTS system filesystem)

include_directories(
	${Boost_INCLUDE_DIR}
	${Intl_INCLUDE_DIRS}
)
add_definitions(
	-DBOOST_SYSTEM_NO_DEPRECATED=1
	-DBOOST_FILESYSTEM_NO_DEPRECATED=1
)

configure_file(git-gui-config.h.in git-gui-config.h ESCAPE_QUOTES)

if(NOT CPPTK_SOURCE_DIR)
	find_path(CPPTK_SOURCE_DIR
		NAMES cpptk.h
		PATHS ${CMAKE_CURRENT_SOURCE_DIR}/C++Tk
		DOC "where the source code of C++Tk is located")
endif(NOT CPPTK_SOURCE_DIR)
if(CPPTK_SOURCE_DIR)
	include_directories(${CPPTK_SOURCE_DIR})
else(CPPTK_SOURCE_DIR)
	message(SEND_ERROR
		"C++Tk source directory not found.
Please specify -DCPPTK_SOURCE_DIR=/path/to/C++Tk with the cmake command \
or initialize the submodule in directory C++Tk:
   git submodule update --init")
endif(CPPTK_SOURCE_DIR)

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
	lib/i18n.cpp
	lib/index.cpp
	lib/line.cpp
	lib/logo.cpp
	lib/merge.cpp
	lib/mergetool.cpp
	lib/option.cpp
	lib/remote.cpp
	lib/remote_add.cpp
	lib/remote_branch_delete.cpp
	lib/repo.cpp
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
	${CPPTK_SOURCE_DIR}/base/cpptkbase.cc
	${CPPTK_SOURCE_DIR}/cpptk.cc
)

add_executable(git-guing ${git-guing_SRCS})

check_cxx_compiler_flag("-Wno-parentheses" CXX_WARN_PARENTHESES)
if(CXX_WARN_PARENTHESES)
	target_compile_options(git-guing PRIVATE "-Wno-parentheses")
endif(CXX_WARN_PARENTHESES)

target_include_directories(git-guing PUBLIC ${CMAKE_BINARY_DIR})
target_link_libraries(git-guing
	${TK_LIBRARY}
	${TCL_LIBRARY}
	${Boost_FILESYSTEM_LIBRARY}
	${Boost_SYSTEM_LIBRARY}
	${Intl_LIBRARIES}
)

add_subdirectory(po)

add_custom_target(git-gui.pot-update
	COMMAND xgettext
		-o po/git-gui.pot
		--default-domain=git-gui
		--add-comments=TRANSLATORS:
		--package-version=${GITGUING_VERSION}
		--package-name=${CMAKE_PROJECT_NAME}
		-kmc
		${git-guing_SRCS}
	DEPENDS ${git-guing_SRCS}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
