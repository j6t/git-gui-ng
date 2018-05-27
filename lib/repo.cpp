// git-guing: the repository object
// Copyright (C) 2018 Johannes Sixt

#include "repo.h"
#include <cpptk.h>

using namespace std;
using namespace std::literals;
using namespace Tk;

Repo repo;

static void set_var(const char* name, string val)
{
	eval("set "s + name + " \"" + Tk::details::quote(val) + '"');
}

void Repo::set_gitdir(path dir)
{
	m_gitdir = move(dir);
	set_var("_gitdir", m_gitdir.string());
}

void Repo::set_prefix(path dir)
{
	m_prefix = move(dir);
	set_var("_prefix", m_prefix.string());
}

void Repo::set_worktree(path dir)
{
	m_worktree = move(dir);
	set_var("_gitworktree", m_worktree.string());
}

void Repo::init_name()
{
	m_name = m_gitdir.filename().string();
	if (m_name == ".git")
	{
		if (m_gitdir.has_parent_path())
			m_name = m_gitdir.parent_path().filename().string();
		else
			m_name = boost::filesystem::current_path().filename().string();
	}
	set_var("_reponame", m_name);
}
