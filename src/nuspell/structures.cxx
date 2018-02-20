/* Copyright 2018 Dimitrij Mijoski
 *
 * This file is part of Nuspell.
 *
 * Nuspell is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nuspell is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Nuspell.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "structures.hxx"

#include <algorithm>
#include <type_traits>

#include <boost/utility/string_view.hpp>

namespace hunspell {

using namespace std;
using boost::basic_string_view;

template <class Container>
void sort_uniq(Container& c)
{
	auto first = begin(c);
	auto last = end(c);
	sort(first, last);
	c.erase(unique(first, last), last);
}

void Flag_Set::sort_uniq() { hunspell::sort_uniq(flags); }

Flag_Set::Flag_Set(const std::u16string& s) : flags(s) { sort_uniq(); }

Flag_Set::Flag_Set(std::u16string&& s) : flags(move(s)) { sort_uniq(); }

auto Flag_Set::operator=(const std::u16string& s) -> Flag_Set&
{
	flags = s;
	sort_uniq();
	return *this;
}

auto Flag_Set::operator=(std::u16string&& s) -> Flag_Set&
{
	flags = move(s);
	sort_uniq();
	return *this;
}

auto Flag_Set::insert(const std::u16string& s) -> void
{
	flags += s;
	sort_uniq();
}

auto Flag_Set::erase(char16_t flag) -> bool
{
	auto i = flags.find(flag);
	if (i != flags.npos) {
		flags.erase(i, 1);
		return true;
	}
	return false;
}

static_assert(is_move_constructible<Flag_Set>::value,
              "Flag Set Not move constructable");
static_assert(is_move_assignable<Flag_Set>::value,
              "Flag Set Not move assingable");

template <class CharT>
void Substr_Replacer<CharT>::sort_uniq()
{
	auto first = table.begin();
	auto last = table.end();
	sort(first, last, [](auto& a, auto& b) { return a.first < b.first; });
	auto it = unique(first, last,
	                 [](auto& a, auto& b) { return a.first == b.first; });
	table.erase(it, last);

	// remove empty key ""
	if (!table.empty() && table.front().first.empty())
		table.erase(table.begin());
}

template <class CharT>
Substr_Replacer<CharT>::Substr_Replacer(const Table_Pairs& v) : table(v)
{
	sort_uniq();
}

template <class CharT>
Substr_Replacer<CharT>::Substr_Replacer(Table_Pairs&& v) : table(move(v))
{
	sort_uniq();
}
template <class CharT>
auto Substr_Replacer<CharT>::operator=(const Table_Pairs& v) -> Substr_Replacer&
{
	table = v;
	sort_uniq();
	return *this;
}
template <class CharT>
auto Substr_Replacer<CharT>::operator=(Table_Pairs&& v) -> Substr_Replacer&
{
	table = move(v);
	sort_uniq();
	return *this;
}

template <class CharT>
struct Comparer_Str_Rep {

	using StrT = basic_string<CharT>;
	using StrViewT = basic_string_view<CharT>;

	auto static cmp_prefix_of(const StrT& p, StrViewT of)
	{
		return p.compare(0, p.npos, of.data(), 0,
		                 min(p.size(), of.size()));
	}
	auto operator()(const pair<StrT, StrT>& a, StrViewT b)
	{
		return cmp_prefix_of(a.first, b) < 0;
	}
	auto operator()(StrViewT a, const pair<StrT, StrT>& b)
	{
		return cmp_prefix_of(b.first, a) > 0;
	}
	auto eq(const pair<StrT, StrT>& a, StrViewT b)
	{
		return cmp_prefix_of(a.first, b) == 0;
	}
};

template <class CharT>
auto find_match(const typename Substr_Replacer<CharT>::Table_Pairs& t,
                basic_string_view<CharT> s)
{
	Comparer_Str_Rep<CharT> csr;
	auto it = begin(t);
	auto last_match = end(t);
	for (;;) {
		auto it2 = upper_bound(it, end(t), s, csr);
		if (it2 == it) {
			// not found, s is smaller that the range
			break;
		}
		--it2;
		if (csr.eq(*it2, s)) {
			// Match found. Try another search maybe for
			// longer.
			last_match = it2;
			it = ++it2;
		}
		else {
			// not found, s is greater that the range
			break;
		}
	}
	return last_match;
}

template <class CharT>
auto Substr_Replacer<CharT>::replace(StrT& s) const -> StrT&
{

	if (table.empty())
		return s;
	for (size_t i = 0; i < s.size(); /*no increment here*/) {
		auto substr = basic_string_view<CharT>(&s[i], s.size() - i);
		auto it = find_match(table, substr);
		if (it != end(table)) {
			auto& match = *it;
			// match found. match.first is the found string,
			// match.second is the replacement.
			s.replace(i, match.first.size(), match.second);
			i += match.second.size();
			continue;
		}
		++i;
	}
	return s;
}

template <class CharT>
auto Substr_Replacer<CharT>::replace_copy(StrT s) const -> StrT
{
	replace(s);
	return s;
}

template class Substr_Replacer<char>;
template class Substr_Replacer<wchar_t>;

static_assert(is_move_constructible<Substr_Replacer<char>>::value,
              "Substring_Replacer Not move constructable");
static_assert(is_move_assignable<Substr_Replacer<char>>::value,
              "Substring_Replacer Not move assingable");

template <class CharT>
auto Break_Table<CharT>::order_entries() -> void
{
	using boost::make_iterator_range;
	auto& f = use_facet<ctype<CharT>>(locale::classic());
	auto dolar = f.widen('$');
	auto caret = f.widen('^');
	auto is_start_word_break = [=](auto& x) {
		return !x.empty() && x[0] == caret;
	};
	auto is_end_word_break = [=](auto& x) {
		return !x.empty() && x.back() == dolar;
	};
	start_word_breaks_last_it =
	    partition(begin(table), end(table), is_start_word_break);

	for (auto& e :
	     make_iterator_range(begin(table), start_word_breaks_last_it)) {
		e.erase(0, 1);
	}

	end_word_breaks_last_it =
	    partition(start_word_breaks_last_it, end(table), is_end_word_break);
	for (auto& e : make_iterator_range(start_word_breaks_last_it,
	                                   end_word_breaks_last_it)) {
		e.pop_back();
	}

	auto it = remove_if(end_word_breaks_last_it, end(table),
	                    [](auto& s) { return s.empty(); });
	table.erase(it, end(table));
}

template <class CharT>
Break_Table<CharT>::Break_Table(const Table_Str& v) : table(v)
{
	order_entries();
}

template <class CharT>
Break_Table<CharT>::Break_Table(Table_Str&& v) : table(std::move(v))
{
	order_entries();
}

template <class CharT>
auto Break_Table<CharT>::operator=(const Table_Str& v) -> Break_Table&
{
	table = v;
	order_entries();
	return *this;
}

template <class CharT>
auto Break_Table<CharT>::operator=(Table_Str&& v) -> Break_Table&
{
	table = move(v);
	order_entries();
	return *this;
}

template <class CharT>
template <class Func>
auto Break_Table<CharT>::break_and_spell(const StrT& s, Func spell_func) const
    -> bool
{
	for (auto& pat : start_word_breaks()) {
		if (s.compare(0, pat.size(), pat) == 0) {
			auto res = spell_func(s.substr(pat.size()));
			if (res)
				return true;
		}
	}
	for (auto& pat : end_word_breaks()) {
		if (pat.size() > s.size())
			continue;
		if (s.compare(s.size() - pat.size(), pat.size(), pat) == 0) {
			auto res = spell_func(s.substr(pat.size()));
			if (res)
				return true;
		}
	}

	for (auto& pat : middle_word_breaks()) {
		auto i = s.find(pat);
		if (i > 0 && i < s.size() - pat.size()) {
			auto part1 = s.substr(0, i);
			auto part2 = s.substr(i + pat.size());
			auto res1 = spell_func(part1);
			if (!res1)
				continue;
			auto res2 = spell_func(part2);
			if (res2)
				return true;
		}
	}
	return false;
}

template class Break_Table<char>;
template class Break_Table<wchar_t>;

/**
 * Constructs a prefix entry.
 *
 * @note Do not provide a string "0" for the parameter stripping.
 * In such case, only an emptry string "" should be used. This is handled by the
 * function <hunspell>::<parse_affix> and should not be double checked here for
 * reasons of optimization.
 *
 * @param flag TODO.
 * @param cross_product TODO.
 * @param strip TODO, see also note above.
 * @param append TODO.
 * @param condition TODO.
 */
Prefix_Entry::Prefix_Entry(char16_t flag, bool cross_product,
                           const std::string& strip, const std::string& append,
                           std::string condition)
    : Affix_Entry{flag, cross_product, strip, append,
                  regex(condition.insert(0, 1, '^'))}
{
}

/**
 * Converts a word into a root according to this prefix entry.
 *
 * The conversion of the word is done by removing at the beginning of the word
 * what * (could have been) appended and subsequently adding at the beginning
 * (what
 * could
 * have been) stripped. This method does the reverse of the derive method.
 *
 * @param word the word which is itself converted into a root.
 * @return The resulting root.
 */
auto Prefix_Entry::to_root(string& word) const -> string&
{
	return word.replace(0, appending.size(), stripping);
}

/**
 * Converts a copy of a word into a root according to this prefix entry.
 *
 * The conversion of the word is done by removing at the beginning of the word
 * what (could have been) appended and subsequently adding at the beginning
 * (what could have been) stripped. This method does the reverse of the derive
 * method.
 *
 * @param word the word of which a copy is used to get converted into a root.
 * @return The resulting root.
 */
auto Prefix_Entry::to_root_copy(string word) const -> string
{
	to_root(word);
	return word;
}

/**
 * Converts a root word into a derived word according to this prefix entry.
 *
 * The conversion of the word is done by replacing at the beginning of the word
 * what to strip with what to append.
 *
 * @param word the root word which is converted to a derived word.
 * @return The resulting derived word.
 */
auto Prefix_Entry::to_derived(string& word) const -> string&
{
	return word.replace(0, stripping.size(), appending);
}

/**
 * Converts a copy of a root word into a derived word according to this prefix
 * entry.
 *
 * The conversion of the word is done by replacing at the beginning of the word
 * what to strip with what to append.
 *
 * @param word the root word of which a copy is used to get converted to a
 * derived word.
 * @return The resulting derived word.
 */
auto Prefix_Entry::to_derived_copy(string word) const -> string
{
	to_derived(word);
	return word;
}

/**
 * Checks of the condition of this prefix entry matches the supplied word.
 *
 * The conversion of the word is done by replacing at the end of the word
 * what to strip with what to append.
 *
 * @note In regular expressions, dots in groups are not metacharacters.
 *
 * @param word to check against the condition.
 * @return The result of the check.
 */
auto Prefix_Entry::check_condition(const string& word) const -> bool
{
	auto m = smatch();
	return regex_search(word, m, condition);
}

/**
 * Constructs a suffix entry.
 *
 * @note Do not provide a string "0" for the parameter stripping.
 * In such case, only an emptry string "" should be used. This is handled by the
 * function <hunspell>::<parse_affix> and should not be double checked here for
 * reasons of optimization.
 *
 * @param flag TODO.
 * @param cross_product TODO.
 * @param strip TODO, see also note above.
 * @param append TODO.
 * @param condition TODO.
 */
Suffix_Entry::Suffix_Entry(char16_t flag, bool cross_product,
                           const std::string& strip, const std::string& append,
                           std::string condition)
    : Affix_Entry{flag, cross_product, strip, append, regex(condition += '$')}
{
}

/**
 * Converts a word into a root according to this suffix entry.
 *
 * The conversion of the word is done by removing at the end of the word what
 * (could have been) appended and subsequently adding at the end (what could
 * have been) stripped. This method does the reverse of the derive method.
 *
 * @param word the word which is itself converted into a root.
 * @return The resulting root.
 */
auto Suffix_Entry::to_root(string& word) const -> string&
{
	return word.replace(word.size() - appending.size(), appending.size(),
	                    stripping);
}

/**
 * Converts a copy of a word into a root according to this suffix entry.
 *
 * The conversion of the word is done by removing at the end of the word what
 * (could have been) appended and subsequently adding at the end (what could
 * have been) stripped. This method does the reverse of the derive method.
 *
 * @param word the word of which a copy is used to get converted into a root.
 * @return The resulting root.
 */
auto Suffix_Entry::to_root_copy(string word) const -> string
{
	return to_root(word);
}

/**
 * Converts a root word into a derived word according to this suffix entry.
 *
 * The conversion of the word is done by replacing at the end of the word
 * what to strip with what to append.
 *
 * @param word the root word which is converted to a derived word.
 * @return The resulting derived word.
 */
auto Suffix_Entry::to_derived(string& word) const -> string&
{
	return word.replace(word.size() - stripping.size(), stripping.size(),
	                    appending);
}

/**
 * Converts a copy of a root word into a derived word according to this suffix
 * entry.
 *
 * The conversion of the word is done by replacing at the end of the word
 * what to strip with what to append.
 *
 * @param word the root word of which a copy is used to get converted to a
 * derived word.
 * @return The resulting derived word.
 */
auto Suffix_Entry::to_derived_copy(string word) const -> string
{
	to_derived(word);
	return word;
}

/**
 * Checks of the condition of this suffix entry matches the supplied word.
 *
 * The conversion of the word is done by replacing at the end of the word
 * what to strip with what to append.
 *
 * @note In regular expressions, dots in groups are not metacharacters.
 *
 * @param word to check against the condition.
 * @return The resulting of the check.
 */
auto Suffix_Entry::check_condition(const string& word) const -> bool
{
	auto m = smatch();
	return regex_search(word, m, condition);
}
}
