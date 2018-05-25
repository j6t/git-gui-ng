// git-guing: the repository object
// Copyright (C) 2018 Johannes Sixt

#pragma once

#include <boost/filesystem.hpp>

class Repo
{
	using path = boost::filesystem::path;

public:
	void set_gitdir(path dir);
	const path& gitdir() const { return m_gitdir; }
	void set_prefix(path dir);
	const path& prefix() const { return m_prefix; }
	void set_worktree(path dir);
	const path& worktree() const { return m_worktree; }

private:
	path m_gitdir;
	path m_prefix;
	path m_worktree;
};
