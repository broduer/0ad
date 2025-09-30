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
 * archive backend for Zip files.
 */

#include "precompiled.h"

#include "archive_zip.h"

#include "lib/debug.h"
#include "lib/file/archive/archive.h"
#include "lib/os_path.h"
#include "lib/status.h"
#include "lib/types.h"

#include <cinttypes>
#include <zip.h>

class ArchiveFile_Zip : public IArchiveFile
{
public:
	ArchiveFile_Zip(std::shared_ptr<zip_t> zip, zip_uint64_t index, OsPath path)
		: m_Zip(std::move(zip)), m_Index(index), m_Path(path)
	{
	}

	size_t Precedence() const override
	{
		return 2u;
	}

	wchar_t LocationCode() const override
	{
		return 'A';
	}

	const OsPath& Path() const override
	{
		return m_Path;
	}

	Status Load(const OsPath& /*name*/, const std::shared_ptr<u8>& buf, size_t size) const override
	{
		zip_file_t* zipFile = zip_fopen_index(m_Zip.get(), m_Index, 0);
		if (zipFile == nullptr)
		{
        	debug_printf("Failed to open file at zip index '%" PRIu64 "'\n", m_Index);
			return ERR::FAIL;
		}

		if (zip_fread(zipFile, buf.get(), size) < 0)
		{
        	debug_printf("Failed to read file at zip index '%" PRIu64 "'\n", m_Index);
			zip_fclose(zipFile);
			return ERR::FAIL;
		}

		if (zip_fclose(zipFile) != 0)
		{
        	debug_printf("Failed to close file at zip index '%" PRIu64 "'\n", m_Index);
			return ERR::FAIL;
		}

		return INFO::OK;
	}

private:
	std::shared_ptr<zip_t> m_Zip;
	zip_uint64_t m_Index;
	OsPath m_Path;
};


struct ZipArchiveDeleter
{
    void operator()(zip_t* zip) const noexcept
    {
		if (zip_close(zip) < 0)
		{
        	debug_printf("archive-deleter: cannot close archive : %s\n", zip_strerror(zip));
        	zip_discard(zip);
		}
    }
};


class ArchiveReader_Zip : public IArchiveReader
{
public:
	ArchiveReader_Zip(const OsPath& archivePath)
	{
		int err;
		zip_t* zip = zip_open(archivePath.string8().c_str(), 0, &err);
		if (zip == nullptr)
		{
			zip_error_t error;
			zip_error_init_with_code(&error, err);
			std::runtime_error exception{"archive-reader: cannot open input archive " + archivePath.string8() + ": " + zip_error_strerror(&error)};
			zip_error_fini(&error);
			throw exception;
		}
		m_Zip = std::shared_ptr<zip_t>(zip, ZipArchiveDeleter());
	}

	Status ReadEntries(ArchiveEntryCallback cb, uintptr_t cbData) override
	{
		Status returnStatus = INFO::OK;

		const zip_int64_t numEntries{zip_get_num_entries(m_Zip.get(), 0)};
		if (numEntries < 0)
		{
        	debug_printf("Can't get entries count for null zip");
		}

		const zip_uint64_t indices{static_cast<std::make_unsigned_t<zip_int64_t>>(numEntries)};
		for (zip_uint64_t index = 0; index < indices; ++index)
		{
			zip_stat_t zipStat;
			if (zip_stat_index(m_Zip.get(), index, 0, &zipStat) < 0)
			{
        		debug_printf("Can't get zip stat for index '%" PRIu64 "'", index);
        		returnStatus = ERR::FAIL;
				continue;
			}
			const Path relativePathname(zipStat.name);
			if(!relativePathname.IsDirectory())
			{
				const OsPath name = relativePathname.Filename();
				CFileInfo fileInfo(name, zipStat.size, zipStat.mtime);
				std::shared_ptr<ArchiveFile_Zip> archiveFile = std::make_shared<ArchiveFile_Zip>(m_Zip, index, name);
				cb(relativePathname, fileInfo, archiveFile, cbData);
			}
		}

		return returnStatus;
	}

private:
	std::shared_ptr<zip_t> m_Zip;
};


class ArchiveWriter_Zip : public IArchiveWriter
{
public:
	ArchiveWriter_Zip(const OsPath& archivePath, bool noDeflate)
		: m_NoDeflate(noDeflate)
	{
		int err;
		zip_t* zip = zip_open(archivePath.string8().c_str(), ZIP_CREATE | ZIP_EXCL, &err);
		if (zip == nullptr)
		{
			zip_error_t error;
			zip_error_init_with_code(&error, err);
			std::runtime_error exception{"archive-writer: cannot open output archive " + archivePath.string8() + ": " + zip_error_strerror(&error)};
			zip_error_fini(&error);
			throw exception;
		}
		m_Zip = std::unique_ptr<zip_t, ZipArchiveDeleter>(zip);
	}

	Status AddFile(const OsPath& pathname, const OsPath& pathnameInArchive) override
	{
		zip_error_t error;
		zip_source_t* zipSource = zip_source_file_create(pathname.string8().c_str(), 0, ZIP_LENGTH_TO_END, &error);
		if (zipSource == nullptr)
		{
        	debug_printf("Can't open zip source file '%s' : %s\n", pathname.string8().c_str(), zip_error_strerror(&error));
        	zip_error_fini(&error);
        	return ERR::FAIL;
		}
		return AddZipSource(zipSource, pathnameInArchive);
	}

	Status AddMemory(const u8* data, size_t size, time_t /*mtime*/, const OsPath& pathnameInArchive) override
	{
		zip_error_t error;
		zip_source_t* zipSource = zip_source_buffer_create(data, size, 0, &error);
		if (zipSource == nullptr)
		{
        	debug_printf("Can't open zip source buffer : %s\n", zip_error_strerror(&error));
        	zip_error_fini(&error);
        	return ERR::FAIL;
		}
		return AddZipSource(zipSource, pathnameInArchive);
	}

private:
	std::unique_ptr<zip_t, ZipArchiveDeleter> m_Zip;
	bool m_NoDeflate;

	Status AddZipSource(zip_source_t* zipSource, const OsPath& pathnameInArchive)
	{
		const zip_int64_t index{zip_file_add(m_Zip.get(), pathnameInArchive.string8().c_str(), zipSource, ZIP_FL_ENC_UTF_8)};
		if (index < 0)
		{
        	debug_printf("Failed to add zip source as '%s'\n", pathnameInArchive.string8().c_str());
			zip_source_free(zipSource);
        	return ERR::FAIL;
		}
		const zip_int32_t comp{m_NoDeflate ? ZIP_CM_STORE : ZIP_CM_DEFLATE};
		if (zip_set_file_compression(m_Zip.get(), index, comp, 0) < 0)
		{
        	debug_printf("Failed to set compression for '%s'\n", pathnameInArchive.string8().c_str());
        	return ERR::FAIL;
		}
		return INFO::OK;
	}
};


PIArchiveReader CreateArchiveReader_Zip(const OsPath& archivePathname)
{
	try
	{
		return PIArchiveReader(new ArchiveReader_Zip(archivePathname));
	}
	catch(Status)
	{
		return PIArchiveReader();
	}
}

PIArchiveWriter CreateArchiveWriter_Zip(const OsPath& archivePathname, bool noDeflate)
{
	try
	{
		return PIArchiveWriter(new ArchiveWriter_Zip(archivePathname, noDeflate));
	}
	catch(Status)
	{
		return PIArchiveWriter();
	}
}
