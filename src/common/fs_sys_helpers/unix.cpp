/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   OS dependant file system & system helper functions

   Written by Moritz Bunkus <moritz@bunkus.org>
*/

#include "common/common_pch.h"

#include <stdlib.h>
#include <sys/time.h>

#if defined(SYS_APPLE)
# include <mach-o/dyld.h>
#endif

#include "common/error.h"
#include "common/fs_sys_helpers.h"
#include "common/mm_file_io.h"
#include "common/path.h"
#include "common/strings/editing.h"
#include "common/strings/parsing.h"
#include "common/strings/utf8.h"

namespace mtx::sys {

void
set_process_priority(int priority) {
  static const int s_nice_levels[5] = { 19, 2, 0, -2, -5 };

  // Avoid a compiler warning due to glibc having flagged 'nice' with
  // 'warn if return value is ignored'.
  if (!nice(s_nice_levels[priority + 2])) {
  }

#if defined(HAVE_SYSCALL) && defined(SYS_ioprio_set)
  if (-2 == priority)
    syscall(SYS_ioprio_set,
            1,        // IOPRIO_WHO_PROCESS
            0,        // current process/thread
            3 << 13); // I/O class 'idle'
#endif
}

int64_t
get_current_time_millis() {
  struct timeval tv;
  if (0 != gettimeofday(&tv, nullptr))
    return -1;

  return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

std::filesystem::path
get_application_data_folder() {
  auto home = getenv("HOME");
  if (!home)
    return {};

  // If $HOME/.mkvtoolnix exists already then keep using it to avoid
  // losing existing user configuration.
  auto old_default_folder = mtx::fs::to_path(home) / ".mkvtoolnix";
  if (std::filesystem::exists(old_default_folder))
    return old_default_folder;

  // If XDG_CONFIG_HOME is set then use that folder.
  auto xdg_config_home = getenv("XDG_CONFIG_HOME");
  if (xdg_config_home)
    return mtx::fs::to_path(xdg_config_home) / "mkvtoolnix";

  // If all fails then use the XDG fallback folder for config files.
  return mtx::fs::to_path(home) / ".config" / "mkvtoolnix";
}

std::string
get_environment_variable(std::string const &key) {
  auto var = getenv(key.c_str());
  return var ? var : "";
}

void
unset_environment_variable(std::string const &key) {
  unsetenv(key.c_str());
}

int
system(std::string const &command) {
  return ::system(command.c_str());
}

std::filesystem::path
get_current_exe_path([[maybe_unused]] std::string const &argv0) {
#if defined(SYS_APPLE)
  std::string file_name;
  file_name.resize(4000);

  while (true) {
    memset(&file_name[0], 0, file_name.size());
    uint32_t size = file_name.size();
    auto result   = _NSGetExecutablePath(&file_name[0], &size);
    file_name.resize(size);

    if (0 == result)
      break;
  }

  return mtx::fs::absolute(mtx::fs::to_path(file_name)).parent_path();

#else // SYS_APPLE
  auto exe = mtx::fs::to_path("/proc/self/exe");
  if (std::filesystem::exists(exe)) {
    auto exe_path = std::filesystem::read_symlink(exe);

    return mtx::fs::absolute(exe_path).parent_path();
  }

  if (argv0.empty())
    return std::filesystem::current_path();

  exe = mtx::fs::absolute(argv0);
  if (std::filesystem::exists(exe))
    return exe.parent_path();

  return std::filesystem::current_path();
#endif
}

bool
is_installed() {
  return true;
}

uint64_t
get_memory_usage() {
  // This only works on Linux and other systems that implement a
  // Linux-compatible procfs on /proc. Not implemented for other
  // systems yet, as it's a debugging tool.

  try {
    auto content = mm_file_io_c::slurp("/proc/self/statm");
    if (!content) {
      return 0;
    }

    uint64_t value{};
    return mtx::string::parse_number(mtx::string::split(content->to_string(), " ", 2)[0], value) ? value * 4096 : 0;

  } catch (...) {
    return 0;
  }
}

}
