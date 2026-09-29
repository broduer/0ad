/* Copyright (C) 2026 Wildfire Games.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * higher-level interface on top of sysdep/filesystem.h
 */

#include "precompiled.h"

#include "file_system.h"

#include "lib/debug.h"
#include "lib/posix/posix_filesystem.h"

#include <chrono>
#include <filesystem>

bool DirectoryExists(const OsPath& path)
{
	try
	{
		return std::filesystem::is_directory(path.string());
	}
	catch (std::filesystem::filesystem_error& err)
	{
		debug_printf("DirectoryExists: failed to check if directory '%s' exists, reason: %s\n", path.string8().c_str(), err.what());
	}
	return false;
}


Status GetFileInfo(const OsPath& pathname, CFileInfo* pPtrInfo)
{
	try
	{
		const std::filesystem::path path{pathname.string()};
		*pPtrInfo = CFileInfo(path.filename().wstring(),
			static_cast<std::uint64_t>(std::filesystem::file_size(path)),
			static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::last_write_time(path).time_since_epoch()).count()));
	}
	catch (std::filesystem::filesystem_error& err)
	{
		debug_printf("GetFileInfo: failed to get file info for '%s', reason: %s\n", pathname.string8().c_str(), err.what());
		return ERR::EXCEPTION;
	}
	return INFO::OK;
}

Status GetDirectoryEntries(const OsPath& path, CFileInfos* files, DirectoryNames* subdirectoryNames)
{
	try
	{
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path.string()))
		{
			if (entry.is_directory() && entry.path().filename() != "." && entry.path().filename() != ".." && subdirectoryNames)
			{
				subdirectoryNames->emplace_back(entry.path().filename());
			}
			else if (entry.is_regular_file() && files)
			{
				files->emplace_back(entry.path().filename().wstring(),
					static_cast<std::uint64_t>(entry.file_size()),
					static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(entry.last_write_time().time_since_epoch()).count()));
			}
		}
	}
	catch (std::filesystem::filesystem_error& err)
	{
		debug_printf("GetDirectoryEntries: failed to get directory entries for'%s', reason: %s\n", path.string8().c_str(), err.what());
		return ERR::EXCEPTION;
	}
	return INFO::OK;
}

namespace
{

std::filesystem::perms ModeTToPerms(mode_t mode)
{
	using std::filesystem::perms;
	perms perm{perms::none};

	if (mode | S_IRUSR)
		perm |= perms::owner_read;
	if (mode | S_IWUSR)
		perm |= perms::owner_write;
	if (mode | S_IXUSR)
		perm |= perms::owner_exec;

	if (mode | S_IRGRP)
		perm |= perms::group_read;
	if (mode | S_IWGRP)
		perm |= perms::group_write;
	if (mode | S_IXGRP)
		perm |= perms::group_exec;

	if (mode | S_IROTH)
		perm |= perms::others_read;
	if (mode | S_IWOTH)
		perm |= perms::others_write;
	if (mode | S_IXOTH)
		perm |= perms::others_exec;

	return perm;
}

Status CreateDirectoriesImpl(const std::filesystem::path& path, const std::filesystem::perms& perms)
{
	if (std::filesystem::exists(path))
		return ERR::FAIL;

	if (!std::filesystem::is_directory(path.parent_path()))
	{
		const Status status = CreateDirectoriesImpl(path.parent_path(), perms);
		if (status != INFO::OK)
			return status;
	}

	std::filesystem::create_directory(path);
	std::filesystem::permissions(path, perms);
	return INFO::OK;
}

} // namespace

Status CreateDirectories(const OsPath& path, mode_t mode, bool breakpoint)
{
	try
	{
		return CreateDirectoriesImpl(std::filesystem::absolute(path.string()), ModeTToPerms(mode));
	}
	catch (std::filesystem::filesystem_error& err)
	{
		debug_printf("CreateDirectories: failed to create directories '%s', reason: %s\n", path.string8().c_str(), err.what());
		if (breakpoint)
            WARN_RETURN(ERR::EXCEPTION);
        else
			return ERR::EXCEPTION;
	}
}


Status DeleteDirectory(const OsPath& path)
{
	try
	{
		std::filesystem::remove_all(path.string());
	}
	catch (std::filesystem::filesystem_error& err)
	{
		debug_printf("DeleteDirectory: failed to delete directory '%s', reason: %s\n", path.string8().c_str(), err.what());
		return ERR::EXCEPTION;
	}
	return INFO::OK;
}

Status RenameFile(const OsPath& path, const OsPath& newPath)
{
	if (path.empty())
		return INFO::OK;

	try
	{
		std::filesystem::rename(std::filesystem::path(path.string()), std::filesystem::path(newPath.string()));
	}
	catch (std::filesystem::filesystem_error& err)
	{
		debug_printf("RenameFile: failed to rename %s to %s.\n%s\n", path.string8().c_str(), path.string8().c_str(), err.what());
		return ERR::EXCEPTION;
	}

	return INFO::OK;

}

Status CopyFile(const OsPath& path, const OsPath& newPath, bool override_if_exists/* = false*/)
{
	if(path.empty())
		return INFO::OK;

	try
	{
		if(override_if_exists)
			std::filesystem::copy_file(std::filesystem::path(path.string()), std::filesystem::path(newPath.string()), std::filesystem::copy_options::overwrite_existing);
		else
			std::filesystem::copy_file(std::filesystem::path(path.string()), std::filesystem::path(newPath.string()));
	}
	catch(std::filesystem::filesystem_error& err)
	{
		debug_printf("CopyFile: failed to copy %s to %s.\n%s\n", path.string8().c_str(), path.string8().c_str(), err.what());
		return ERR::EXCEPTION;
	}

	return INFO::OK;
}
