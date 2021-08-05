/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   BCP 47 language tags

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

namespace mtx::bcp47 {

class language_c {
protected:
  struct prefix_restrictions_t {
    bool language{}, extended_language_subtags{}, script{}, region{}, variants{};
  };

  std::string m_language;                               // shortest ISO 639 code or reserved or registered language subtag
  std::vector<std::string> m_extended_language_subtags; // selected ISO 639 codes
  std::string m_script;                                 // ISO 15924 code
  std::string m_region;                                 // either ISO 3166-1 code or UN M.49 code
  std::vector<std::string> m_variants;                  // registered variants
  std::vector<std::string> m_extensions;
  std::vector<std::string> m_private_use;

  bool m_valid{false};
  std::string m_parser_error;

  mutable std::string m_formatted;
  mutable bool m_formatted_up_to_date{};

  static bool ms_disabled;

public:
  void clear() noexcept;

  bool has_valid_iso639_code() const noexcept;
  bool has_valid_iso639_2_code() const noexcept;
  std::string get_iso639_alpha_3_code() const noexcept;
  std::string get_iso639_2_alpha_3_code_or(std::string const &value_if_invalid) const noexcept;

  std::string dump() const noexcept;
  std::string format(bool force = false) const noexcept;
  std::string format_long(bool force = false) const noexcept;
  bool is_valid() const noexcept;
  std::string const &get_error() const noexcept;

  bool operator ==(language_c const &other) const noexcept;
  bool operator !=(language_c const &other) const noexcept;

  language_c &set_valid(bool valid);
  language_c &set_language(std::string const &language);
  language_c &set_extended_language_subtags(std::vector<std::string> const &extended_language_subtags);
  language_c &set_script(std::string const &script);
  language_c &set_region(std::string const &region);
  language_c &set_variants(std::vector<std::string> const &variants);
  language_c &set_extensions(std::vector<std::string> const &extensions);
  language_c &set_private_use(std::vector<std::string> const &private_use);

  std::string const &get_language() const noexcept;
  std::vector<std::string> const &get_extended_language_subtags() const noexcept;
  std::string const &get_script() const noexcept;
  std::string const &get_region() const noexcept;
  std::vector<std::string> const &get_variants() const noexcept;
  std::vector<std::string> const &get_extensions() const noexcept;
  std::vector<std::string> const &get_private_use() const noexcept;

protected:
  std::string format_internal(bool force) const noexcept;

  bool parse_language(std::string const &code);
  bool parse_script(std::string const &code);
  bool parse_region(std::string const &code);
  bool parse_extlangs_or_variants(std::string const &str, bool is_extlangs);

  bool validate_extlangs_or_variants(bool is_extlangs);
  bool validate_one_extlang_or_variant(std::size_t extlang_or_variant_index, bool is_extlang);
  bool matches_prefix(language_c const &prefix, std::size_t extlang_or_variant_index, bool is_extlang, prefix_restrictions_t const &restrictions) const noexcept;

public:
  static language_c parse(std::string const &language);

  static void disable();
  static bool is_disabled();
};

void init_re();

inline std::ostream &
operator <<(std::ostream &out,
            language_c const &language) {
  out << language.format();
  return out;
}

inline bool
operator<(language_c const &a,
          language_c const &b) {
  return a.format() < b.format();
}

} // namespace mtx::bcp47

namespace std {

template<>
struct hash<mtx::bcp47::language_c> {
  std::size_t operator()(mtx::bcp47::language_c const &key) const {
    return std::hash<std::string>()(key.format());
  }
};

} // namespace mtx::bcp47
