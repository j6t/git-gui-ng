// git-guing: the repository object
// Copyright (C) 2018 Johannes Sixt

#include "repo.h"
#include <cpptk.h>

using namespace std;
using namespace std::literals;
using namespace Tk;

Repo repo;

void Repo::set_gitdir(path dir)
{
	m_gitdir = move(dir);
	"_gitdir"_tclv = m_gitdir.string();
}

void Repo::set_prefix(path dir)
{
	m_prefix = move(dir);
	"_prefix"_tclv = m_prefix.string();
}

void Repo::set_worktree(path dir)
{
	m_worktree = move(dir);
	"_gitworktree"_tclv = m_worktree.string();
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
	"_reponame"_tclv = m_name;
}
