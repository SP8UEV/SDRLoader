// i18n.hpp -- the program is English-only.
//
// User-facing strings are written as tr(L"English", L"..."): tr() always
// returns the first (English) argument. The two-argument shape is kept so the
// call sites don't all have to change, and so a language could be reintroduced
// later by making tr() consult language() again.
#pragma once

namespace sdi {

enum class Lang { En, Pl };  // kept for source compatibility; only En is used

inline Lang language() { return Lang::En; }
inline void setLanguage(Lang) {}
inline void detectLanguage() {}

inline const wchar_t* tr(const wchar_t* en, const wchar_t* /*other*/) { return en; }

}  // namespace sdi
