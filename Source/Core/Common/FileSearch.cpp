// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <functional>
#include <regex>

#include "Common/CommonPaths.h"
#include "Common/FileSearch.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

#ifdef HAS_STD_FILESYSTEM
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <cstring>
#include "Common/CommonFuncs.h"
#include "Common/FileUtil.h"
#endif

namespace Common
{
#ifndef HAS_STD_FILESYSTEM

static std::vector<std::string>
FileSearchWithTest(const std::vector<std::string>& directories, bool recursive,
                   std::function<bool(const File::FSTEntry&)> callback)
{
  std::vector<std::string> result;
  for (const std::string& directory : directories)
  {
    File::FSTEntry top = File::ScanDirectoryTree(directory, recursive);

    std::function<void(File::FSTEntry&)> DoEntry;
    DoEntry = [&](File::FSTEntry& entry) {
      if (callback(entry))
        result.push_back(entry.physicalName);
      for (auto& child : entry.children)
        DoEntry(child);
    };
    for (auto& child : top.children)
      DoEntry(child);
  }
  // remove duplicates
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::vector<std::string> DoFileSearch(const std::vector<std::string>& directories,
                                      const std::vector<std::string>& exts, bool recursive)
{
  const bool accept_all = exts.empty();
  return FileSearchWithTest(directories, recursive, [&](const File::FSTEntry& entry) {
    if (accept_all)
      return true;
    if (entry.isDirectory)
      return false;
    return std::any_of(exts.begin(), exts.end(), [&](const std::string& ext) {
      const std::string& name = entry.virtualName;
      return name.length() >= ext.length() &&
             strcasecmp(name.c_str() + name.length() - ext.length(), ext.c_str()) == 0;
    });
  });
}

#else

std::vector<std::string> DoFileSearch(const std::vector<std::string>& directories,
                                      const std::vector<std::string>& exts, bool recursive)
{
  // If no extensions are specified all files and directories are returned.
  const bool accept_all = exts.empty();

  fs::path::string_type ext_pattern_str;

  // Combine extensions into regex alternations matching end of string, and escape '.' chars.
  for (const auto& ext : exts)
    ext_pattern_str += StringToPath(ReplaceAll(ext, ".", "\\.") + "$|").native();

  // Remove final '|' character.
  if (!ext_pattern_str.empty())
    ext_pattern_str.pop_back();

  const std::basic_regex<fs::path::value_type> ext_pattern(
      ext_pattern_str, std::regex_constants::icase | std::regex_constants::optimize);

  auto ext_matches = [&ext_pattern](const fs::path& path) {
    return std::regex_search(path.filename().native(), ext_pattern);
  };

  std::vector<std::string> result;
  auto add_filtered = [&](const fs::directory_entry& entry) {
    auto& path = entry.path();
    std::error_code dir_error;
    if (accept_all || (ext_matches(path) && !fs::is_directory(path, dir_error)))
    {
      // Lexically normalize for std::unique usage below.
      result.emplace_back(PathToString(path.lexically_normal()));
    }
    else if (dir_error)
    {
      ERROR_LOG(COMMON, "fs::is_directory: %s", dir_error.message().c_str());
    }
  };

  for (const auto& directory : directories)
  {
    fs::path directory_path = StringToPath(directory);
    std::error_code iter_error;
    if (recursive)
    {
      for (auto& entry : fs::recursive_directory_iterator(
               std::move(directory_path), fs::directory_options::follow_directory_symlink,
               iter_error))
      {
        add_filtered(entry);
      }
    }
    else
    {
      for (auto& entry : fs::directory_iterator(std::move(directory_path), iter_error))
        add_filtered(entry);
    }

    if (iter_error)
      ERROR_LOG(COMMON, "fs::directory_iterator: %s", iter_error.message().c_str());
  }

  // Remove duplicates (occurring because caller gave e.g. duplicate or overlapping directories -
  // not because std::filesystem returns duplicates). Also note that this pathname-based uniqueness
  // isn't as thorough as std::filesystem::equivalent.
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());

  // Dolphin expects to be able to use "/" (DIR_SEP) everywhere.
  // std::filesystem uses the OS separator.
  constexpr fs::path::value_type os_separator = fs::path::preferred_separator;
  static_assert(os_separator == DIR_SEP_CHR || os_separator == '\\', "Unsupported path separator");
  if constexpr (os_separator != DIR_SEP_CHR)
  {
    for (auto& path : result)
      std::replace(path.begin(), path.end(), '\\', DIR_SEP_CHR);
  }

  return result;
}

#endif

}  // namespace Common
