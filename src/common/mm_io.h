/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   IO callback class definitions

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#ifndef __MTX_COMMON_MM_IO_H
#define __MTX_COMMON_MM_IO_H

#include "common/common.h"

#include <stack>
#if HAVE_POSIX_FADVISE
# include <unistd.h>
# include <sys/stat.h>
# include <sys/types.h>
#endif

#include <ebml/IOCallback.h>

#include "common/error.h"
#include "common/memory.h"
#include "common/smart_pointers.h"

using namespace libebml;

class MTX_DLL_API mm_io_error_c: public error_c {
public:
  mm_io_error_c(const std::string &_error): error_c(_error) { }
};

class MTX_DLL_API mm_io_eof_error_c: public mm_io_error_c {
public:
  mm_io_eof_error_c(): mm_io_error_c("end-of-file") { }
};

class MTX_DLL_API mm_io_seek_error_c: public mm_io_error_c {
public:
  mm_io_seek_error_c(): mm_io_error_c("seeking failed") { }
};

class MTX_DLL_API mm_io_open_error_c: public mm_io_error_c {
public:
  mm_io_open_error_c(): mm_io_error_c("opening the file failed") { }
};

class MTX_DLL_API mm_io_c: public IOCallback {
protected:
  bool dos_style_newlines;
  std::stack<int64_t> positions;
  int64_t m_current_position, cached_size;

public:
  mm_io_c()
    : dos_style_newlines(false)
    , m_current_position(0)
    , cached_size(-1)
  {
  }
  virtual ~mm_io_c() { }

  virtual uint64 getFilePointer() = 0;
  virtual void setFilePointer(int64 offset, seek_mode mode = seek_beginning) = 0;
  virtual bool setFilePointer2(int64 offset, seek_mode mode = seek_beginning);
  virtual uint32 read(void *buffer, size_t size) = 0;
  virtual uint32_t read(std::string &buffer, size_t size);
  virtual uint32_t read(memory_cptr &buffer, size_t size, int offset = 0);
  virtual unsigned char read_uint8();
  virtual uint16_t read_uint16_le();
  virtual uint32_t read_uint24_le();
  virtual uint32_t read_uint32_le();
  virtual uint64_t read_uint64_le();
  virtual uint16_t read_uint16_be();
  virtual uint32_t read_uint24_be();
  virtual uint32_t read_uint32_be();
  virtual uint64_t read_uint64_be();
  virtual int write_uint8(unsigned char value);
  virtual int write_uint16_le(uint16_t value);
  virtual int write_uint32_le(uint32_t value);
  virtual int write_uint64_le(uint64_t value);
  virtual int write_uint16_be(uint16_t value);
  virtual int write_uint32_be(uint32_t value);
  virtual int write_uint64_be(uint64_t value);
  virtual void skip(int64 numbytes);
  virtual size_t write(const void *buffer, size_t size) = 0;
  virtual uint32_t write(const memory_cptr &buffer, int size = -1, int offset = 0);
  virtual bool eof() = 0;
  virtual void flush() {
  }

  virtual std::string get_file_name() const = 0;

  virtual std::string getline();
  virtual bool getline2(std::string &s);
  virtual size_t puts(const std::string &s);
  inline size_t puts(const boost::format &format) {
    return puts(format.str());
  }
  virtual bool write_bom(const std::string &charset);
  virtual int getch();

  virtual void save_pos(int64_t new_pos = -1);
  virtual bool restore_pos();

  virtual int64_t get_size();

  virtual void close() = 0;

  virtual void use_dos_style_newlines(bool yes) {
    dos_style_newlines = yes;
  }
};

typedef counted_ptr<mm_io_c> mm_io_cptr;

#if HAVE_POSIX_FADVISE
struct file_id_t {
  bool m_initialized;
  dev_t m_dev;
  ino_t m_ino;

  file_id_t()
    : m_initialized(false)
    , m_dev(0)
    , m_ino(0)
  {
  }

  void initialize(const struct stat &st) {
    m_dev         = st.st_dev;
    m_ino         = st.st_ino;
    m_initialized = true;
  }
};
#endif

class MTX_DLL_API mm_file_io_c: public mm_io_c {
protected:
#if defined(SYS_WINDOWS)
  bool _eof;
#endif
#if HAVE_POSIX_FADVISE
  file_id_t m_file_id;
  unsigned long read_count, write_count;
  static bool use_posix_fadvise;
  bool use_posix_fadvise_here;
  std::string canonicalized_file_name;
#endif
  std::string file_name;
  void *file;

public:
  mm_file_io_c(const std::string &path, const open_mode mode = MODE_READ);
  virtual ~mm_file_io_c();

  static void prepare_path(const std::string &path);

  virtual uint64 getFilePointer();
#if defined(SYS_WINDOWS)
  virtual uint64 get_real_file_pointer();
#endif
  virtual void setFilePointer(int64 offset, seek_mode mode = seek_beginning);
  virtual uint32 read(void *buffer, size_t size);
  virtual size_t write(const void *buffer, size_t size);
  virtual void close();
  virtual bool eof();

  virtual std::string get_file_name() const {
    return file_name;
  }

  virtual int truncate(int64_t pos);

#if HAVE_POSIX_FADVISE
  void setup_fadvise(const std::string &local_path);
#endif

  static void setup();
  static void cleanup();
};

typedef counted_ptr<mm_file_io_c> mm_file_io_cptr;

class MTX_DLL_API mm_proxy_io_c: public mm_io_c {
protected:
  mm_io_c *proxy_io;
  bool proxy_delete_io;

public:
  mm_proxy_io_c(mm_io_c *_proxy_io, bool _proxy_delete_io = true):
    proxy_io(_proxy_io),
    proxy_delete_io(_proxy_delete_io) {
  }
  virtual ~mm_proxy_io_c() {
    close();
  }

  virtual void setFilePointer(int64 offset, seek_mode mode=seek_beginning) {
    return proxy_io->setFilePointer(offset, mode);
  }
  virtual uint64 getFilePointer() {
    return proxy_io->getFilePointer();
  }
  virtual uint32 read(void *buffer, size_t size) {
    return proxy_io->read(buffer, size);
  }
  virtual size_t write(const void *buffer, size_t size) {
    return proxy_io->write(buffer, size);
  }
  virtual bool eof() {
    return proxy_io->eof();
  }
  virtual void close();
  virtual std::string get_file_name() const {
    return proxy_io->get_file_name();
  }
};

typedef counted_ptr<mm_proxy_io_c> mm_proxy_io_cptr;

class MTX_DLL_API mm_null_io_c: public mm_io_c {
protected:
  int64_t pos;

public:
  mm_null_io_c();

  virtual uint64 getFilePointer();
  virtual void setFilePointer(int64 offset, seek_mode mode = seek_beginning);
  virtual uint32 read(void *buffer, size_t size);
  virtual size_t write(const void *buffer, size_t size);
  virtual void close();
};

typedef counted_ptr<mm_null_io_c> mm_null_io_cptr;

class MTX_DLL_API mm_mem_io_c: public mm_io_c {
protected:
  int64_t pos, mem_size, allocated, increase;
  unsigned char *mem;
  const unsigned char *ro_mem;
  bool free_mem, read_only;
  std::string file_name;

public:
  mm_mem_io_c(unsigned char *_mem, uint64_t _mem_size, int _increase);
  mm_mem_io_c(const unsigned char *_mem, uint64_t _mem_size);
  ~mm_mem_io_c();

  virtual uint64 getFilePointer();
  virtual void setFilePointer(int64 offset, seek_mode mode = seek_beginning);
  virtual uint32 read(void *buffer, size_t size);
  virtual uint32_t read(memory_cptr &buffer, size_t size, int offset = 0) {
    return mm_io_c::read(buffer, size, offset);
  }
  virtual size_t write(const void *buffer, size_t size);
  virtual uint32_t write(const memory_cptr &buffer, int size = -1, int offset = 0) {
    return mm_io_c::write(buffer, size, offset);
  }
  virtual void close();
  virtual bool eof();
  virtual std::string get_file_name() const {
    return file_name;
  }
  virtual void set_file_name(const std::string &_file_name) {
    file_name = _file_name;
  }

  virtual unsigned char *get_and_lock_buffer();
};

typedef counted_ptr<mm_mem_io_c> mm_mem_io_cptr;

enum byte_order_e {BO_UTF8, BO_UTF16_LE, BO_UTF16_BE, BO_UTF32_LE, BO_UTF32_BE, BO_NONE};

class MTX_DLL_API mm_text_io_c: public mm_proxy_io_c {
protected:
  byte_order_e byte_order;
  unsigned int bom_len;

public:
  mm_text_io_c(mm_io_c *_in, bool _delete_in = true);

  virtual void setFilePointer(int64 offset, seek_mode mode=seek_beginning);
  virtual std::string getline();
  virtual int read_next_char(char *buffer);
  virtual byte_order_e get_byte_order();
  virtual void set_byte_order(byte_order_e new_byte_order) {
    byte_order = new_byte_order;
  }

public:
  static bool has_byte_order_marker(const std::string &string);
  static bool detect_byte_order_marker(const unsigned char *buffer, unsigned int size, byte_order_e &byte_order, unsigned int &bom_length);
};

typedef counted_ptr<mm_text_io_c> mm_text_io_cptr;

class MTX_DLL_API mm_stdio_c: public mm_io_c {
public:
  mm_stdio_c();

  virtual uint64 getFilePointer();
  virtual void setFilePointer(int64 offset, seek_mode mode=seek_beginning);
  virtual uint32 read(void *buffer, size_t size);
  virtual size_t write(const void *buffer, size_t size);
  virtual void close();
  virtual bool eof() {
    return false;
  }
  virtual std::string get_file_name() const {
    return "";
  }
  virtual void flush();
};

typedef counted_ptr<mm_stdio_c> mm_stdio_cptr;

#endif // __MTX_COMMON_MM_IO_H
