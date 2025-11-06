// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/DirectIOFile.h"

#include <utility>

#if defined(_WIN32)
#include <windows.h>

#include <cstring>

#include "Common/Buffer.h"
#include "Common/CommonFuncs.h"
#include "Common/StringUtil.h"
#else
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Common/FileUtil.h"
#endif

#include "Common/Assert.h"

namespace File
{
DirectIOFile::DirectIOFile() = default;

DirectIOFile::~DirectIOFile()
{
  Close();
}

DirectIOFile::DirectIOFile(const DirectIOFile& other)
{
  *this = other.Duplicate();
}

DirectIOFile& DirectIOFile::operator=(const DirectIOFile& other)
{
  return *this = other.Duplicate();
}

DirectIOFile::DirectIOFile(DirectIOFile&& other)
{
  Swap(other);
}

DirectIOFile& DirectIOFile::operator=(DirectIOFile&& other)
{
  Close();
  Swap(other);
  return *this;
}

DirectIOFile::DirectIOFile(const std::string& path, AccessMode access_mode, OpenMode create_mode)
{
  Open(path, access_mode, create_mode);
}

bool DirectIOFile::Open(const std::string& path, AccessMode access_mode, OpenMode open_mode)
{
  ASSERT(!IsOpen());

  if (open_mode == OpenMode::Default)
    open_mode = (access_mode == AccessMode::Write) ? OpenMode::Truncate : OpenMode::MustExist;

#if defined(_WIN32)
  DWORD desired_access = GENERIC_READ | GENERIC_WRITE;
  if (access_mode == AccessMode::Read)
    desired_access = GENERIC_READ;
  else if (access_mode == AccessMode::Write)
    desired_access = GENERIC_WRITE;

  // Allow deleting through our handle.
  desired_access |= DELETE;

  // All sharing is allowed to more closely match default behavior on other OSes.
  constexpr DWORD share_mode = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;

  DWORD creation_disposition = OPEN_EXISTING;
  if (open_mode == CreateMode::CreateNew)
    creation_disposition = CREATE_NEW;
  else if (open_mode == CreateMode::Default && access_mode == AccessMode::Write)
    creation_disposition = OPEN_ALWAYS;

  m_handle = CreateFile(UTF8ToTStr(path).c_str(), desired_access, share_mode, nullptr,
                        creation_disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (!IsOpen())
    WARN_LOG_FMT(COMMON, "CreateFile: {}", Common::GetLastErrorString());
#elif defined(ANDROID)
  // Leveraging IOFile to avoid reimplementing things.
  const char* open_mode_str = "r+";
  if (access_mode == AccessMode::Read)
    open_mode_str = "r";
  else if (access_mode == AccessMode::Write)
    open_mode_str = "w";

  // TODO: open modes!!

  if (IOFile file(path, open_mode_str); file.IsOpen())
    m_fd = dup(fileno(file.GetHandle()));
#else
  int flags = O_RDWR;
  if (access_mode == AccessMode::Read)
    flags = O_RDONLY;
  else if (access_mode == AccessMode::Write)
    flags = O_WRONLY;

  if (open_mode == OpenMode::Truncate)
    flags |= O_CREAT | O_TRUNC;
  else if (open_mode == OpenMode::MustCreate)
    flags |= O_CREAT | O_EXCL;
  else if (open_mode != OpenMode::MustExist)
    flags |= O_CREAT;

  m_fd = open(path.c_str(), flags, 0666);
#endif

  return IsOpen();
}

bool DirectIOFile::Close()
{
  if (!IsOpen())
    return false;

  m_current_offset = 0;

#if defined(_WIN32)
  return CloseHandle(std::exchange(m_handle, INVALID_HANDLE_VALUE)) != 0;
#else
  return close(std::exchange(m_fd, -1)) == 0;
#endif
}

bool DirectIOFile::IsOpen() const
{
#if defined(_WIN32)
  return m_handle != INVALID_HANDLE_VALUE;
#else
  return m_fd != -1;
#endif
}

#if defined(_WIN32)
template <auto* TransferFunc>
static bool OverlappedTransfer(HANDLE handle, u64 offset, auto* data_ptr, u64 size)
{
  // ReadFile/WriteFile take a 32bit size so we must loop to handle our 64bit size.
  while (true)
  {
    OVERLAPPED overlapped{};
    overlapped.Offset = DWORD(offset);
    overlapped.OffsetHigh = DWORD(offset >> 32);

    DWORD bytes_transferred{};
    if (TransferFunc(handle, data_ptr, DWORD(size), &bytes_transferred, &overlapped) == 0)
    {
      ERROR_LOG_FMT(COMMON, "OverlappedTransfer: {}", Common::GetLastErrorString());
      return false;
    }

    size -= bytes_transferred;

    if (size == 0)
      return true;

    offset += bytes_transferred;
    data_ptr += bytes_transferred;
  }
}
#endif

bool DirectIOFile::OffsetRead(u64 offset, u8* out_ptr, u64 size)
{
#if defined(_WIN32)
  return OverlappedTransfer<ReadFile>(m_handle, offset, out_ptr, size);
#else
  return pread(m_fd, out_ptr, size, off_t(offset)) == ssize_t(size);
#endif
}

bool DirectIOFile::OffsetWrite(u64 offset, const u8* in_ptr, u64 size)
{
#if defined(_WIN32)
  return OverlappedTransfer<WriteFile>(m_handle, offset, in_ptr, size);
#else
  return pwrite(m_fd, in_ptr, size, off_t(offset)) == ssize_t(size);
#endif
}

u64 DirectIOFile::GetSize() const
{
#if defined(_WIN32)
  LARGE_INTEGER result{};
  if (GetFileSizeEx(m_handle, &result) != 0)
    return result.QuadPart;
#else
  struct stat st{};
  if (fstat(m_fd, &st) == 0)
    return st.st_size;
#endif

  return 0;
}

bool DirectIOFile::Seek(s64 offset, SeekOrigin origin)
{
  if (!IsOpen())
    return false;

  u64 reference_pos = 0;
  switch (origin)
  {
  case SeekOrigin::Current:
    reference_pos = m_current_offset;
    break;
  case SeekOrigin::End:
    reference_pos = GetSize();
    break;
  default:
    break;
  }

  // Don't let our current offset underflow.
  if (offset < 0 && u64(-offset) > reference_pos)
    return false;

  m_current_offset = reference_pos + offset;
  return true;
}

void DirectIOFile::Swap(DirectIOFile& other)
{
#if defined(_WIN32)
  std::swap(m_handle, other.m_handle);
#else
  std::swap(m_fd, other.m_fd);
#endif
  std::swap(m_current_offset, other.m_current_offset);
}

DirectIOFile DirectIOFile::Duplicate() const
{
  DirectIOFile result;

  if (!IsOpen())
    return result;

#if defined(_WIN32)
  const auto current_process = GetCurrentProcess();
  if (DuplicateHandle(current_process, m_handle, current_process, &result.m_handle, 0, FALSE,
                      DUPLICATE_SAME_ACCESS) == 0)
  {
    ERROR_LOG_FMT(COMMON, "DuplicateHandle: {}", Common::GetLastErrorString());
  }
#else
  result.m_fd = dup(m_fd);
#endif

  ASSERT(result.IsOpen());

  result.m_current_offset = m_current_offset;

  return result;
}

bool Resize(DirectIOFile& file, u64 size)
{
#if defined(_WIN32)
  // This operation is not "atomic", but it's the only thing we're using the file pointer for.
  // Concurrent `Resize` would need some external synchronization to prevent race regardless.
  const LARGE_INTEGER distance{.QuadPart = LONGLONG(size)};
  return (SetFilePointerEx(file.GetHandle(), distance, nullptr, FILE_BEGIN) != 0) &&
         (SetEndOfFile(file.GetHandle()) != 0);
#else
  return ftruncate(file.GetHandle(), off_t(size)) == 0;
#endif
}

bool Rename(DirectIOFile& file [[maybe_unused]], const std::string& source_path [[maybe_unused]],
            const std::string& destination_path)
{
#if defined(_WIN32)
  const auto dest_name = UTF8ToWString(destination_path);
  const auto dest_name_byte_size = DWORD(dest_name.size() * sizeof(WCHAR));
  FILE_RENAME_INFO info{
      .ReplaceIfExists = TRUE,
      .FileNameLength = dest_name_byte_size,  // The size in bytes, not including null termination.
  };
  constexpr auto filename_struct_offset = offsetof(FILE_RENAME_INFO, FileName);
  Common::UniqueBuffer<u8> buffer(filename_struct_offset + dest_name_byte_size + sizeof(WCHAR));
  std::memcpy(buffer.data(), &info, filename_struct_offset);
  std::memcpy(buffer.data() + filename_struct_offset, dest_name.c_str(),
              dest_name_byte_size + sizeof(WCHAR));
  return SetFileInformationByHandle(file.GetHandle(), FileRenameInfo, buffer.data(),
                                    DWORD(buffer.size())) != 0;
#else
  return file.IsOpen() && Rename(source_path, destination_path);
#endif
}

bool Delete(DirectIOFile& file [[maybe_unused]], const std::string& filename)
{
#if defined(_WIN32)
  FILE_DISPOSITION_INFO info{.DeleteFile = TRUE};
  return SetFileInformationByHandle(file.GetHandle(), FileDispositionInfo, &info, sizeof(info)) !=
         0;
#else
  return file.IsOpen() && Delete(filename, IfAbsentBehavior::NoConsoleWarning);
#endif
}

}  // namespace File
