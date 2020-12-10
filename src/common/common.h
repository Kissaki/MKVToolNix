/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   definitions used in all programs, helper functions

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#undef min
#undef max

#include <type_traits>

#include <algorithm>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <cassert>
#include <cstring>

#if defined(HAVE_SYS_TYPES_H)
# include <sys/types.h>
#endif // HAVE_SYS_TYPES_H
#if defined(HAVE_STDINT_H)
# include <stdint.h>
#endif // HAVE_STDINT_H
#if defined(HAVE_INTTYPES_H)
# include <inttypes.h>
#endif // HAVE_INTTYPES_H

// Don't support user-defined literals in fmt as they aren't used by
// MKVToolNix and produce a compiler warning in -Wpedantic
#define FMT_USE_USER_DEFINED_LITERALS 0

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/rational.hpp>
#include <boost/system/error_code.hpp>

namespace balg = boost::algorithm;
namespace bfs  = boost::filesystem;

using namespace std::string_literals;

#include "common/os.h"

#include <ebml/c/libebml_t.h>
#undef min
#undef max

#include <ebml/EbmlElement.h>
#include <ebml/EbmlMaster.h>

/* i18n stuff */
#if defined(HAVE_LIBINTL_H)
# include <libintl.h>
// libintl defines 'snprintf' to 'libintl_snprintf' on certain
// platforms such as mingw or macOS.  'std::snprintf' becomes
// 'std::libintl_snprintf' which doesn't exist.
# undef fprintf
# undef snprintf
# undef sprintf
#else
# define gettext(s)                            (s)
# define ngettext(s_singular, s_plural, count) ((count) != 1 ? (s_plural) : (s_singular))
#endif

#undef Y
#undef NY
#define Y(s)                            gettext(u8##s)
#define NY(s_singular, s_plural, count) ngettext(u8##s_singular, u8##s_plural, count)

#define BUGMSG Y("This should not have happened. Please contact the author " \
                 "Moritz Bunkus <moritz@bunkus.org> with this error/warning " \
                 "message, a description of what you were trying to do, " \
                 "the command line used and which operating system you are " \
                 "using. Thank you.")


#define MXMSG_ERROR    5
#define MXMSG_WARNING 10
#define MXMSG_INFO    15

#if !defined(FOURCC)
#define FOURCC(a, b, c, d) (uint32_t)((((unsigned char)a) << 24) + \
                                      (((unsigned char)b) << 16) + \
                                      (((unsigned char)c) <<  8) + \
                                       ((unsigned char)d))
#endif
#define isblanktab(c) (((c) == ' ')  || ((c) == '\t'))
#define iscr(c)       (((c) == '\n') || ((c) == '\r'))

#define TIMESTAMP_SCALE 1000000

void mxrun_before_exit(std::function<void()> function);
[[noreturn]]
void mxexit(int code = -1);
void set_process_priority(int priority);

extern unsigned int verbose;

void mtx_common_init(std::string const &program_name, char const *argv0);
std::string const &get_program_name();

#define MTX_DECLARE_PRIVATE(PrivateClass) \
  inline PrivateClass* p_func() { return reinterpret_cast<PrivateClass *>(&(*p_ptr)); } \
  inline const PrivateClass* p_func() const { return reinterpret_cast<const PrivateClass *>(&(*p_ptr)); } \
  friend class PrivateClass;

#define MTX_DECLARE_PUBLIC(Class)                                    \
  inline Class* q_func() { return static_cast<Class *>(q_ptr); } \
  inline const Class* q_func() const { return static_cast<const Class *>(q_ptr); } \
  friend class Class;

#include "common/debugging.h"
#include "common/error.h"
#include "common/memory.h"
#include "common/mm_io.h"
#include "common/output.h"
