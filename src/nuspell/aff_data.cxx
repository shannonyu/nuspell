/* Copyright 2016-2018 Dimitrij Mijoski
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

/**
 * @file aff_data.cxx
 * Affixing data structures.
 */

#include "aff_data.hxx"

#include "locale_utils.hxx"
#include "string_utils.hxx"

#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_map>

#include <fstream> // Only here for logging.
#include <iomanip> // Only here for logging.

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/locale.hpp>
#include <boost/range/adaptors.hpp>

/*
 * Aff_Data class and the method parse() should be structured in the following
 * way. The data members of the class should be data structures that are
 * useful for reading and searching operations, but are not necessarely
 * good for dynamic updates. They should be designed toward their later use
 * while spellchecking.
 *
 * On the other hand, the method parse() should fill the data in intermediate
 * data structures that are good for dynamic updates.
 *
 * E.g. In the class: it can store the affixes in a sorted vector by the key
 * on which will be searched, the adding affix. (aka flat_map)
 *
 * In method parse(): it will fill the affixes in simple unsorted vector.
 *
 * For simple stiff like simple int, flag, or for data structures that are
 * equally good for updating and reading, just parse directly into the class
 * data members.
 *
 * class Aff_data {
 *	Affix_Table affix_table;     // wraps a vector and keeps invariant
 *	                                that the vector is always sorted
 *	Substring_Replace_Table rep_table;
 * };
 *
 * parse(istream in) {
 *	vector<aff_entry> affix_table_intermediate;
 *	vector<rep_entry> rep_table_intermediate;
 *	while (getline) {
 *	...
 *		affix_table_intermediate.push_back(entry)
 *	...
 *	}
 *	At the end
 *	affix_table = std::move(affix_table_intermediate);
 * }
 */

namespace nuspell {

using namespace std;

Encoding::Encoding(const std::string& e) : name(e)
{
	boost::algorithm::to_upper(name, locale::classic());
	if (name == "UTF8")
		name = "UTF-8";
}
auto Encoding::operator=(const std::string& e) -> Encoding&
{
	name = e;
	boost::algorithm::to_upper(name, locale::classic());
	if (name == "UTF8")
		name = "UTF-8";
	return *this;
}

void reset_failbit_istream(std::istream& in)
{
	in.clear(in.rdstate() & ~in.failbit);
}

bool read_to_slash_or_space(std::istream& in, std::string& out)
{
	in >> std::ws;
	int c;
	bool readSomething = false;
	while ((c = in.get()) != std::istream::traits_type::eof() &&
	       !isspace((char)c, in.getloc()) && c != '/') {
		out.push_back(c);
		readSomething = true;
	}
	bool slash = c == '/';
	if (readSomething || slash) {
		reset_failbit_istream(in);
	}
	return slash;
}

/**
 * Parses vector of class T from an input stream.
 *
 * @param in input stream to decode from.
 * @param line_num
 * @param command
 * @param[in,out] counts
 * @param[in,out] vec
 * @param parseLineFunc
 */
template <class T, class Func>
auto parse_vector_of_T(istream& in, size_t line_num, const string& command,
                       unordered_map<string, int>& counts, vector<T>& vec,
                       Func parseLineFunc) -> void
{
	auto dat = counts.find(command);
	if (dat == counts.end()) {
		// first line
		size_t a;
		in >> a;
		if (in.fail()) {
			a = 0; // err
			cerr << "Nuspell error: a vector command (series of "
			        "of similar commands) has no count. Ignoring "
			        "all of them."
			     << endl;
		}
		counts[command] = a;
	}
	else if (dat->second) {
		vec.emplace_back();
		parseLineFunc(in, vec.back());
		if (in.fail()) {
			vec.pop_back();
			cerr << "Nuspell error: single entry of a vector "
			        "command (series of "
			        "of similar commands) is invalid"
			     << endl;
		}
		dat->second--;
	}
	else {
		cerr << "Nuspell warning: extra entries of " << command << "\n";
		cerr << "Nuspell warning in line " << line_num << endl;
	}
}

/**
 * Decodes flags.
 *
 * Expects that there are flags in the stream.
 * If there are no flags in the stream (eg, stream is at eof)
 * or if the format of the flags is incorrect the stream failbit will be set.
 */
auto decode_flags(istream& in, size_t line_num, Flag_Type t,
                  const Encoding& enc) -> u16string
{
	string s;
	u16string ret;
	const auto err_message = "Nuspell warning: bytes above 127 in UTF-8 "
	                         "stream should not be treated alone as "
	                         "flags, please update dictionary to use "
	                         "FLAG UTF-8 and make the file valid UTF-8";
	switch (t) {
	case FLAG_SINGLE_CHAR:
		in >> s;
		if (in.fail()) {
			// err no flag at all
			cerr << "Nuspell error: missing single-character flag "
			        "in line "
			     << line_num << endl;
			exit(0);
			break;
		}
		if (enc.is_utf8() && !is_all_ascii(s)) {
			cerr << err_message << "\n";
			cerr << "Nuspell warning in line " << line_num << "\n"
			     << endl;
			// This error will be triggered in Hungarian.
			// Version 1 passed this, it just read a
			// single byte even if the stream utf-8.
			// Hungarian dictionary exploited this
			// bug/feature, resulting it's file to be
			// mixed utf-8 and latin2.
			// In v2 this will eventually work, with
			// a warning.
		}
		latin1_to_ucs2(s, ret);
		break;
	case FLAG_DOUBLE_CHAR: {
		in >> s;
		if (in.fail()) {
			// err no flag at all
			cerr << "Nuspell error: missing double-character flag "
			        "in line "
			     << line_num << endl;
			break;
		}
		if (enc.is_utf8() && !is_all_ascii(s)) {
			cerr << err_message << "\n";
			cerr << "Nuspell warning in line " << line_num << endl;
		}
		auto i = s.begin();
		auto e = s.end();
		if (s.size() & 1) {
			--e;
		}
		for (; i != e; i += 2) {
			char16_t c1 = static_cast<unsigned char>(*i);
			char16_t c2 = static_cast<unsigned char>(*(i + 1));
			ret.push_back((c1 << 8) | c2);
		}
		if (i != s.end()) {
			ret.push_back(static_cast<unsigned char>(*i));
		}
		break;
	}
	case FLAG_NUMBER:
		unsigned short flag;
		in >> flag;
		if (in.fail()) {
			// err no flag at all
			cerr << "Nuspell error: missing numerical flag in line "
			     << line_num << endl;
			break;
		}
		ret.push_back(flag);
		// peek can set failbit
		while (in.good() && in.peek() == ',') {
			in.get();
			if (in >> flag) {
				ret.push_back(flag);
			}
			else {
				// err, comma and no number after that
				cerr << "Nuspell error: long flag, no number "
				        "after comma"
				     << endl;
				break;
			}
		}
		break;
	case FLAG_UTF8: {
		in >> s;
		if (!enc.is_utf8()) {
			// err
			cerr << "Nuspell error: file encoding is not UTF-8, "
			        "yet flags are"
			     << endl;
		}
		if (in.fail()) {
			// err no flag at all
			cerr << "Nuspell error: missing UTF-8 flag in line "
			     << line_num << endl;
			break;
		}
		auto u32flags = boost::locale::conv::utf_to_utf<char32_t>(s);
		if (!is_all_bmp(u32flags)) {
			cerr << "Nuspell warning: flags must be in BMP, "
			        "skipping non-BMP\n"
			     << "Nuspell warning in line " << line_num << endl;
		}
		u32_to_ucs2_skip_non_bmp(u32flags, ret);
		break;
	}
	}
	return ret;
}

/**
 * Decodes a single flag from an input stream.
 *
 * @param in input stream to decode from.
 * @param line_num
 * @param t
 * @param enc encoding of the stream.
 * @return The value of the first decoded flag or 0 when no flag was decoded.
 */
auto decode_single_flag(istream& in, size_t line_num, Flag_Type t,
                        const Encoding& enc) -> char16_t
{
	auto flags = decode_flags(in, line_num, t, enc);
	if (flags.size()) {
		return flags.front();
	}
	return 0;
}

auto decode_flags_possible_alias(istream& in, size_t line_num, Flag_Type t,
                                 const Encoding& enc,
                                 const vector<Flag_Set>& flag_aliases)
    -> u16string
{
	if (flag_aliases.empty()) {
		return decode_flags(in, line_num, t, enc);
	}
	else {
		size_t i;
		if (in >> i && 0 < i && i <= flag_aliases.size())
			return flag_aliases[i - 1];
		else
			cerr << "Nuspell error: invalid flag alias index\n";
	}
	return {};
}

/**
 * Parses morhological fields.
 *
 * @param in input stream to parse from.
 * @param[in,out] vecOut
 */
auto parse_morhological_fields(istream& in, vector<string>& vecOut) -> void
{
	if (!in.good()) {
		return;
	}

	std::string morph;
	while (in >> morph) {
		vecOut.push_back(morph);
	}
	reset_failbit_istream(in);
}

/**
 * Parses an affix from an input stream.
 *
 * @param in input stream to parse from.
 * @param line_num
 * @param[in,out] command
 * @param t
 * @param enc
 * @param flag_aliases
 * @param[in,out] vec
 * @param[in,out] cmd_affix.
 */
auto parse_affix(istream& in, size_t line_num, string& command, Flag_Type t,
                 const Encoding& enc, const vector<Flag_Set>& flag_aliases,
                 vector<Affix>& vec,
                 unordered_map<string, pair<bool, int>>& cmd_affix) -> void
{
	char16_t f = decode_single_flag(in, line_num, t, enc);
	if (f == 0) {
		// err
		return;
	}
	char f1 = f & 0xff;
	char f2 = (f >> 8) & 0xff;
	command.push_back(f1);
	command.push_back(f2);
	auto dat = cmd_affix.find(command);
	// note: the current affix parser does not allow the same flag
	// to be used once with cross product and again witohut
	// one flag is tied to one cross product value
	if (dat == cmd_affix.end()) {
		char cross_char; // 'Y' or 'N'
		size_t cnt;
		in >> cross_char >> cnt;
		bool cross = cross_char == 'Y';
		if (in.fail()) {
			cnt = 0; // err
			cerr << "Nuspell error: a SFX/PFX header command is "
			        "invalid, missing count or cross product in "
			        "line "
			     << line_num << endl;
		}
		cmd_affix[command] = make_pair(cross, cnt);
	}
	else if (dat->second.second) {
		vec.emplace_back();
		auto& elem = vec.back();
		elem.flag = f;
		elem.cross_product = dat->second.first;
		in >> elem.stripping;
		if (elem.stripping == "0")
			elem.stripping = "";
		if (read_to_slash_or_space(in, elem.appending))
			elem.new_flags = decode_flags_possible_alias(
			    in, line_num, t, enc, flag_aliases);
		if (elem.appending == "0")
			elem.appending = "";
		if (in.fail()) {
			vec.pop_back();
			return;
		}
		in >> elem.condition;
		if (elem.condition.empty())
			elem.condition = '.';
		if (in.fail())
			reset_failbit_istream(in);
		else
			parse_morhological_fields(in,
			                          elem.morphological_fields);
		dat->second.second--;
	}
	else {
		cerr << "Nuspell warning: extra entries of "
		     << command.substr(0, 3) << "\n"
		     << "Nuspell warning in line " << line_num << endl;
	}
}

/**
 * Parses flag type.
 *
 * @param in input stream to parse from.
 * @param line_num
 * @param[out] flag_type
 */
auto parse_flag_type(istream& in, size_t line_num, Flag_Type& flag_type) -> void
{
	(void)line_num;
	string p;
	in >> p;
	boost::algorithm::to_upper(p, in.getloc());
	if (p == "LONG")
		flag_type = FLAG_DOUBLE_CHAR;
	else if (p == "NUM")
		flag_type = FLAG_NUMBER;
	else if (p == "UTF-8")
		flag_type = FLAG_UTF8;
	else
		cerr << "Nuspell error: unknown FLAG type" << endl;
}

auto parse_compound_rule(istream& in, size_t line_num, Flag_Type t,
                         const Encoding& enc, u16string& ret)
{
	switch (t) {
	case FLAG_SINGLE_CHAR:
	case FLAG_UTF8:
		ret = decode_flags(in, line_num, t, enc);
		break;
	case FLAG_DOUBLE_CHAR: {
		// Following could be replaced by non-library implementation,
		// reducing size of binary when no regex methods are called at
		// all.
		auto r = regex(R"(\((..)\)([?*]?))");
		auto str = string();
		in >> str;
		auto it = sregex_iterator(str.begin(), str.end(), r);
		auto last = sregex_iterator();
		for (; it != last; ++it) {
			auto& m = *it;
			auto i = m[1].first;
			char16_t c1 = static_cast<unsigned char>(*i);
			char16_t c2 = static_cast<unsigned char>(*(i + 1));
			ret.push_back((c1 << 8) | c2);

			if (m[2].length() != 0)
				ret.push_back(*m[2].first);
		}
		break;
	}
	case FLAG_NUMBER: {
		// Following could be replaced by non-library implementation,
		// reducing size of binary when no regex methods are called at
		// all.
		auto r = regex(R"(\(([0-9]+)\)([?*]?))");
		auto str = string();
		in >> str;
		auto it = sregex_iterator(str.begin(), str.end(), r);
		auto last = sregex_iterator();
		for (; it != last; ++it) {
			auto& m = *it;
			size_t number_pos = m.position(1);
			auto fl = strtoul(str.data() + number_pos, nullptr, 10);
			if (fl <= char16_t(-1))
				ret.push_back(fl);

			if (m[2].length() != 0)
				ret.push_back(*m[2].first);
		}
		break;
	}
	}
}

auto strip_bom(istream& in)
{
	string bom(3, '\0');
	in.read(&bom[0], 3);
	if (in.gcount() != 3 || bom != "\xEF\xBB\xBF") {
		for (int i = in.gcount() - 1; i >= 0; --i) {
			in.putback(bom[i]);
		}
		reset_failbit_istream(in);
	}
}

auto get_locale_name(string lang, string enc, const string& filename) -> string
{
	if (enc.empty())
		enc = "ISO8859-1";
	if (lang.empty()) {
		if (filename.empty()) {
			lang = "en_US";
		}
	}
	return lang + "." + enc;
}

auto Aff_Data::set_encoding_and_language(const string& enc, const string& lang)
    -> void
{
	boost::locale::generator locale_generator;
	locale_aff = locale_generator(get_locale_name(lang, enc));
	install_ctype_facets_inplace(locale_aff);
}

/**
 * Parses an input stream offering affix information.
 *
 * @param in input stream to parse from.
 * @return true on success.
 */
auto Aff_Data::parse_aff(istream& in) -> bool
{
	Encoding encoding;
	string language_code;
	string ignore_chars;
	vector<Affix> prefixes;
	vector<Affix> suffixes;
	vector<string> break_patterns;
	vector<pair<string, string>> input_conversion;
	vector<pair<string, string>> output_conversion;
	vector<vector<string>> morphological_aliases;
	bool break_exists = false;

	flag_type = FLAG_SINGLE_CHAR;

	unordered_map<string, string*> command_strings = {
	    {"LANG", &language_code},
	    {"IGNORE", &ignore_chars},

	    {"KEY", &keyboard_layout},
	    {"TRY", &try_chars},

	    {"WORDCHARS", &wordchars}};

	unordered_map<string, bool*> command_bools = {
	    {"COMPLEXPREFIXES", &complex_prefixes},

	    {"ONLYMAXDIFF", &only_max_diff},
	    {"NOSPLITSUGS", &no_split_suggestions},
	    {"SUGSWITHDOTS", &suggest_with_dots},
	    {"FORBIDWARN", &forbid_warn},

	    {"COMPOUNDMORESUFFIXES", &compound_more_suffixes},
	    {"CHECKCOMPOUNDDUP", &compound_check_up},
	    {"CHECKCOMPOUNDREP", &compound_check_rep},
	    {"CHECKCOMPOUNDCASE", &compound_check_case},
	    {"CHECKCOMPOUNDTRIPLE", &compound_check_triple},
	    {"SIMPLIFIEDTRIPLE", &compound_simplified_triple},

	    {"FULLSTRIP", &fullstrip},
	    {"CHECKSHARPS", &checksharps}};

	unordered_map<string, vector<string>*> command_vec_str = {
	    {"MAP", &map_related_chars}};

	unordered_map<string, short*> command_shorts = {
	    {"MAXCPDSUGS", &max_compound_suggestions},
	    {"MAXNGRAMSUGS", &max_ngram_suggestions},
	    {"MAXDIFF", &max_diff_factor},

	    {"COMPOUNDMIN", &compound_minimum},
	    {"COMPOUNDWORDMAX", &compound_word_max}};

	unordered_map<string, vector<pair<string, string>>*> command_vec_pair =
	    {{"REP", &replacements},
	     {"PHONE", &phonetic_replacements},
	     {"ICONV", &input_conversion},
	     {"OCONV", &output_conversion}};

	unordered_map<string, char16_t*> command_flag = {
	    {"NOSUGGEST", &nosuggest_flag},
	    {"WARN", &warn_flag},

	    {"COMPOUNDFLAG", &compound_flag},
	    {"COMPOUNDBEGIN", &compound_begin_flag},
	    {"COMPOUNDLAST", &compound_last_flag},
	    {"COMPOUNDMIDDLE", &compound_middle_flag},
	    {"ONLYINCOMPOUND", &compound_onlyin_flag},
	    {"COMPOUNDPERMITFLAG", &compound_permit_flag},
	    {"COMPOUNDFORBIDFLAG", &compound_forbid_flag},
	    {"COMPOUNDROOT", &compound_root_flag},
	    {"FORCEUCASE", &compound_force_uppercase},

	    {"CIRCUMFIX", &circumfix_flag},
	    {"FORBIDDENWORD", &forbiddenword_flag},
	    {"KEEPCASE", &keepcase_flag},
	    {"NEEDAFFIX", &need_affix_flag},
	    {"SUBSTANDARD", &substandard_flag}};

	// keeps count for each vector
	auto cmd_with_vec_cnt = unordered_map<string, int>();
	auto cmd_affix = unordered_map<string, pair<bool, int>>();
	auto line = string();
	auto command = string();
	size_t line_num = 0;
	auto ss = istringstream();
	auto loc = locale::classic();
	// while parsing, the streams must have plain ascii locale without
	// any special number separator otherwise istream >> int might fail
	// due to thousands separator.
	// "C" locale can be used assuming it is US-ASCII
	in.imbue(loc);
	ss.imbue(loc);
	strip_bom(in);
	while (getline(in, line)) {
		line_num++;

		if (encoding.is_utf8() && !validate_utf8(line)) {
			cerr << "Nuspell warning: invalid utf in aff file"
			     << endl;
			// Hungarian will triger this, contains mixed
			// utf-8 and latin2. See note in decode_flags().
		}

		ss.str(line);
		ss.clear();
		ss >> ws;
		if (ss.eof() || ss.peek() == '#') {
			continue; // skip comment or empty lines
		}
		ss >> command;
		boost::algorithm::to_upper(command, ss.getloc());
		ss >> ws;
		if (command == "PFX" || command == "SFX") {
			auto& vec = command[0] == 'P' ? prefixes : suffixes;
			parse_affix(ss, line_num, command, flag_type, encoding,
			            flag_aliases, vec, cmd_affix);
		}
		else if (command_strings.count(command)) {
			auto& str = *command_strings[command];
			if (str.empty())
				ss >> str;
			else
				cerr << "Nuspell warning: "
				        "setting "
				     << command << " more than once, ignoring\n"
				     << "Nuspell warning in line " << line_num
				     << endl;
		}
		else if (command_bools.count(command)) {
			*command_bools[command] = true;
		}
		else if (command_shorts.count(command)) {
			ss >> *command_shorts[command];
		}
		else if (command_flag.count(command)) {
			*command_flag[command] = decode_single_flag(
			    ss, line_num, flag_type, encoding);
		}
		else if (command_vec_str.count(command)) {
			auto& vec = *command_vec_str[command];
			auto func = [&](istream& in, string& p) { in >> p; };
			parse_vector_of_T(ss, line_num, command,
			                  cmd_with_vec_cnt, vec, func);
		}
		else if (command_vec_pair.count(command)) {
			auto& vec = *command_vec_pair[command];
			auto func = [&](istream& in, pair<string, string>& p) {
				in >> p.first >> p.second;
			};
			parse_vector_of_T(ss, line_num, command,
			                  cmd_with_vec_cnt, vec, func);
		}
		else if (command == "SET") {
			if (encoding.empty()) {
				auto str = string();
				ss >> str;
				encoding = str;
			}
			else {
				cerr << "Nuspell warning: "
				        "setting "
				     << command << " more than once, ignoring\n"
				     << "Nuspell warning in line " << line_num
				     << endl;
			}
		}
		else if (command == "FLAG") {
			parse_flag_type(ss, line_num, flag_type);
		}
		else if (command == "AF") {
			auto& vec = flag_aliases;
			auto func = [&](istream& inn, Flag_Set& p) {
				p = decode_flags(inn, line_num, flag_type,
				                 encoding);
			};
			parse_vector_of_T(ss, line_num, command,
			                  cmd_with_vec_cnt, vec, func);
		}
		else if (command == "AM") {
			auto& vec = morphological_aliases;
			parse_vector_of_T(ss, line_num, command,
			                  cmd_with_vec_cnt, vec,
			                  parse_morhological_fields);
		}
		else if (command == "BREAK") {
			auto& vec = break_patterns;
			auto func = [&](istream& in, string& p) { in >> p; };
			parse_vector_of_T(ss, line_num, command,
			                  cmd_with_vec_cnt, vec, func);
			break_exists = true;
		}
		else if (command == "CHECKCOMPOUNDPATTERN") {
			auto& vec = compound_check_patterns;
			auto func = [&](istream& in,
			                Compound_Check_Pattern& p) {
				if (read_to_slash_or_space(in,
				                           p.first_word_end)) {
					p.first_word_flag = decode_single_flag(
					    in, line_num, flag_type, encoding);
				}
				if (read_to_slash_or_space(
				        in, p.second_word_begin)) {
					p.second_word_flag = decode_single_flag(
					    in, line_num, flag_type, encoding);
				}
				if (in.fail()) {
					return;
				}
				in >> p.replacement;
				reset_failbit_istream(in);
			};
			parse_vector_of_T(ss, line_num, command,
			                  cmd_with_vec_cnt, vec, func);
		}
		else if (command == "COMPOUNDRULE") {
			auto func = [&](istream& in, u16string& rule) {
				parse_compound_rule(in, line_num, flag_type,
				                    encoding, rule);
			};
			parse_vector_of_T(ss, line_num, command,
			                  cmd_with_vec_cnt, compound_rules,
			                  func);
		}
		else if (command == "COMPOUNDSYLLABLE") {
			ss >> compound_syllable_max >> compound_syllable_vowels;
		}
		else if (command == "SYLLABLENUM") {
			compound_syllable_num =
			    decode_flags(ss, line_num, flag_type, encoding);
		}
		if (ss.fail()) {
			cerr
			    << "Nuspell error: could not parse affix file line "
			    << line_num << ": " << line << endl;
		}
	}
	// default BREAK definition
	if (!break_exists) {
		break_patterns.push_back("-");
		break_patterns.push_back("^-");
		break_patterns.push_back("-$");
	}

	// now fill data structures from temporary data
	set_encoding_and_language(encoding, language_code);
	if (encoding.is_utf8()) {
		using namespace boost::locale::conv;
		auto u_to_u_pair = [](auto& x) {
			return make_pair(utf_to_utf<wchar_t>(x.first),
			                 utf_to_utf<wchar_t>(x.second));
		};
		auto iconv =
		    boost::adaptors::transform(input_conversion, u_to_u_pair);
		wide_structures.input_substr_replacer = iconv;
		auto oconv =
		    boost::adaptors::transform(output_conversion, u_to_u_pair);
		wide_structures.output_substr_replacer = oconv;

		auto u_to_u = [](auto& x) { return utf_to_utf<wchar_t>(x); };
		auto break_pat =
		    boost::adaptors::transform(break_patterns, u_to_u);
		wide_structures.break_table = break_pat;
		wide_structures.ignored_chars = u_to_u(ignore_chars);

		for (auto& x : prefixes) {
			wide_structures.prefixes.emplace(
			    x.flag, x.cross_product, u_to_u(x.stripping),
			    u_to_u(x.appending), x.new_flags,
			    u_to_u(x.condition));
		}
		for (auto& x : suffixes) {
			wide_structures.suffixes.emplace(
			    x.flag, x.cross_product, u_to_u(x.stripping),
			    u_to_u(x.appending), x.new_flags,
			    u_to_u(x.condition));
		}
	}
	else {
		structures.input_substr_replacer = input_conversion;
		structures.output_substr_replacer = output_conversion;
		structures.break_table = break_patterns;
		structures.ignored_chars = ignore_chars;

		for (auto& x : prefixes) {
			structures.prefixes.emplace(x.flag, x.cross_product,
			                            x.stripping, x.appending,
			                            x.new_flags, x.condition);
		}
		for (auto& x : suffixes) {
			structures.suffixes.emplace(x.flag, x.cross_product,
			                            x.stripping, x.appending,
			                            x.new_flags, x.condition);
		}
	}

	cerr.flush();
	return in.eof(); // success when eof is reached
}

/**
 * @brief Scans @p line for morphological field [a-z][a-z]:
 * @param line
 *
 * @returns the end of the word before the morph field, or npos
 */
auto dic_find_end_of_word_heuristics(const string& line)
{
	if (line.size() < 4)
		return line.npos;
	size_t a = 0;
	for (;;) {
		a = line.find(' ', a);
		if (a == line.npos)
			break;
		auto b = line.find_first_not_of(' ', a);
		if (b == line.npos)
			break;
		if (b > line.size() - 3)
			break;
		if (line[b] >= 'a' && line[b] <= 'z' && line[b + 1] >= 'a' &&
		    line[b + 1] <= 'z' && line[b + 2] == ':')
			return a;
		a = b;
	}
	return line.npos;
}

/**
 * Parses an input stream offering dictionary information.
 *
 * @param in input stream to read from.
 * @param aff affix data to retrive locale and flag settings from.
 * @return true on success.
 */
auto Aff_Data::parse_dic(istream& in) -> bool
{
	size_t line_number = 1;
	size_t approximate_size;
	istringstream ss;
	string line;

	// locale must be without thousands separator.
	auto loc = locale::classic();
	in.imbue(loc);
	ss.imbue(loc);
	strip_bom(in);
	if (!getline(in, line)) {
		return false;
	}
	auto encoding =
	    Encoding(use_facet<boost::locale::info>(locale_aff).encoding());
	if (encoding.is_utf8() && !validate_utf8(line)) {
		cerr << "Invalid utf in dic file" << endl;
	}
	ss.str(line);
	if (ss >> approximate_size) {
		words.reserve(approximate_size);
	}
	else {
		return false;
	}

	string word;
	string morph;
	vector<string> morphs;
	u16string flags;

	while (getline(in, line)) {
		line_number++;
		ss.str(line);
		ss.clear();
		word.clear();
		morph.clear();
		flags.clear();
		morphs.clear();

		if (encoding.is_utf8() && !validate_utf8(line)) {
			cerr << "Invalid utf in dic file" << endl;
		}
		size_t slash_pos = 0;
		for (;;) {
			slash_pos = line.find('/', slash_pos);
			if (slash_pos == line.npos)
				break;
			if (slash_pos == 0)
				break;
			if (line[slash_pos - 1] != '\\')
				break;
			++slash_pos;
		}
		if (slash_pos != line.npos) {
			// slash found, word until slash
			word.assign(line, 0, slash_pos);
			ss.ignore(slash_pos + 1);
			flags = decode_flags_possible_alias(
			    ss, line_number, flag_type, encoding, flag_aliases);
			if (ss.fail())
				continue;
		}
		else if (line.find('\t') != line.npos) {
			// Tab found, word until tab. No flags.
			// After tab follow morphological fields
			getline(ss, word, '\t');
		}
		else {
			auto end = dic_find_end_of_word_heuristics(line);
			word.assign(line, 0, end);
			ss.ignore(end);
		}
		if (word.empty()) {
			continue;
		}
		// parse_morhological_fields(ss, morphs);

		auto casing = classify_casing(word, locale_aff);
		const char16_t HIDDEN_HOMONYM_FLAG = -1;
		switch (casing) {
		case Casing::ALL_CAPITAL: {
			// check for hidden homonym
			auto hom = words.equal_range(word);
			auto h = find_if(hom.first, hom.second, [&](auto& w) {
				return w.second.contains(HIDDEN_HOMONYM_FLAG);
			});

			if (h != hom.second) {
				// replace if found
				h->second = flags;
			}
			else {
				words.emplace(word, flags);
			}
		} break;
		case Casing::PASCAL:
		case Casing::CAMEL: {
			words.emplace(word, flags);

			// add the hidden homonym directly in uppercase
			auto up = boost::locale::to_upper(word, locale_aff);
			auto hom = words.equal_range(up);
			auto h = find_if(hom.first, hom.second, [&](auto& w) {
				return w.second.contains(HIDDEN_HOMONYM_FLAG);
			});
			if (h == hom.second) { // if not found
				flags += HIDDEN_HOMONYM_FLAG;
				words.emplace(up, flags);
			}

		} break;
		default:
			words.emplace(word, flags);
			break;
		}
	}
	return in.eof(); // success if we reached eof
}

auto Dic_Data::find(const wstring& word) -> iterator
{
	return find(boost::locale::conv::utf_to_utf<char>(word));
}

auto Dic_Data::find(const wstring& word) const -> const_iterator
{
	return find(boost::locale::conv::utf_to_utf<char>(word));
}
auto Dic_Data::equal_range(const std::wstring& word)
    -> std::pair<iterator, iterator>
{
	return equal_range(boost::locale::conv::utf_to_utf<char>(word));
}
auto Dic_Data::equal_range(const std::wstring& word) const
    -> std::pair<const_iterator, const_iterator>
{
	return equal_range(boost::locale::conv::utf_to_utf<char>(word));
}

void Aff_Data::log(const string& affpath)
{
	std::ofstream log_file;
	auto log_name = std::string(".am2.log"); // 1: Hunspell, 2: Nuspell
	log_name.insert(0, affpath);
	if (log_name.substr(0, 2) == "./")
		log_name.erase(0, 2);
	log_name.insert(0, "../nuspell/"); // prevent logging somewhere else
	log_file.open(log_name, std::ios_base::out);
	if (!log_file.is_open()) {
		return;
	}
	log_file << "affpath/affpath\t"
	         << log_name.erase(log_name.size() - 8, 8) << "\n";
	//	log_file << "key\tTODO" << "\n";
	log_file << "AFTER parse"
	         << "\n";

	log_file << "\n"
	         << "BASIC"
	         << "\n";
	// The contents of alldic and pHMgr are logged by a
	// seprate log
	// method in the hash manager.

	// log_file << "encoding/encoding.value\t\"" <<
	// encoding.value()
	//	 << "\"" << "\n";
	log_file << "pHMgr->flag_mode/flag_type\t";
	if (flag_type == FLAG_DOUBLE_CHAR)
		log_file << "double char";
	else if (flag_type == FLAG_SINGLE_CHAR)
		log_file << "single char";
	else if (flag_type == FLAG_NUMBER)
		log_file << "number";
	else if (flag_type == FLAG_UTF8)
		log_file << "utf8";
	log_file << "\n";
	log_file << "complexprefixes/complex_prefixes\t" << complex_prefixes
	         << "\n";
	// log_file << "lang/language_code\t\"" << language_code << "\""
	//         << "\n";
	// log_file << "ignorechars/ignore_chars\t\"" << ignore_chars << "\""
	//         << "\n";
	// TODO flag_aliases
	// TODO morphological_aliases

	// TODO locale_aff
#if 0
	for (std::vector<Affix>::const_iterator i = prefixes.begin();
	     i != prefixes.end(); ++i) {
		if (flag_type == FLAG_SINGLE_CHAR)
			log_file << "pfx/prefixes_" << std::setw(3)
			         << std::setfill('0')
			         << i - prefixes.begin() + 1 << ".aflag/flag\t"
			         << (char)i->flag << "\n";
		else
			log_file << "pfx/prefixes_" << std::setw(3)
			         << std::setfill('0')
			         << i - prefixes.begin() + 1 << ".aflag/flag\t"
			         << i->flag << "\n";
		log_file << "pfx/prefixes_" << std::setw(3) << std::setfill('0')
		         << i - prefixes.begin() + 1
		         << ".appnd@getKey/affix\t\"" << i->appending << "\""
		         << "\n";
		//		log_file << "pfx/prefixes_" <<
		// std::setw(3) <<
		// std::setfill('0')
		//		         << i - prefixes.begin() +
		// 1 <<
		//".contclasslen\t"
		//		         << i->affix <<
		// "\n";
	}
	for (std::vector<Affix>::const_iterator i = suffixes.begin();
	     i != suffixes.end(); ++i) {
		if (flag_type == FLAG_SINGLE_CHAR)
			log_file << "sfx/suffixes_" << std::setw(3)
			         << std::setfill('0')
			         << i - suffixes.begin() + 1 << ".aflag/flag\t"
			         << (char)i->flag << "\n";
		else
			log_file << "sfx/suffixes_" << std::setw(3)
			         << std::setfill('0')
			         << i - suffixes.begin() + 1 << ".aflag/flag\t"
			         << i->flag << "\n";
		log_file << "sfx/suffixes_" << std::setw(3) << std::setfill('0')
		         << i - suffixes.begin() + 1
		         << ".appnd@getKey/affix\t\"" << i->appending << "\""
		         << "\n";
		// later		log_file << "sfx/suffixes_" <<
		// std::setw(3)
		// <<
		// std::setfill('0')
		// later			 << i - suffixes.begin()
		// <<
		// ".cross()/cross_product\t"
		// later			 << i->cross_product <<
		// "\n";
		//		log_gile << "sfx/suffixes_" <<
		// std::setw(3) <<
		// std::setfill('0')
		//			 << i - suffixes.begin() + 1
		//<<
		//".contclasslen\t"
		//			 << i->affix <<
		// "\n";
		log_file << "sfx/suffixes_" << std::setw(3) << std::setfill('0')
		         << i - suffixes.begin() + 1 << ".rappnd@getKey/\t"
		         << "\n";
		log_file << "sfx/suffixes_" << std::setw(3) << std::setfill('0')
		         << i - suffixes.begin() + 1 << ".strip/stripping\t\""
		         << i->stripping << "\""
		         << "\n";
	}
#endif
	log_file << "\n"
	         << "SUGGESTION OPTIONS"
	         << "\n"
	            "\n";
	log_file << "keystring/keyboard_layout\t\"" << keyboard_layout << "\""
	         << "\n";
	log_file << "trystring/try_chars\t\"" << try_chars << "\""
	         << "\n";
	log_file << "nosuggest/nosuggest_flag\t" << nosuggest_flag << "\n";
	log_file << "maxcpdsugs/max_compound_suggestions\t"
	         << max_compound_suggestions << "\n";
	log_file << "maxngramsugs/max_ngram_suggestions\t"
	         << max_ngram_suggestions << "\n";
	log_file << "maxdiff/max_diff_factor\t" << max_diff_factor << "\n";
	log_file << "onlymaxdiff/only_max_diff;\t" << only_max_diff << "\n";
	log_file << "nosplitsugs/no_split_suggestions\t" << no_split_suggestions
	         << "\n";
	log_file << "sugswithdots/suggest_with_dots\t" << suggest_with_dots
	         << "\n";
	for (std::vector<pair<string, string>>::const_iterator i =
	         replacements.begin();
	     i != replacements.end(); ++i) {
		log_file << "reptable/replacements_" << std::setw(3)
		         << std::setfill('0') << i - replacements.begin() + 1
		         << "\t\"" << i->first << "\"\t\"" << i->second << "\""
		         << "\n";
	}
	for (std::vector<std::string>::const_iterator i =
	         map_related_chars.begin();
	     i != map_related_chars.end(); ++i) {
		log_file << "maptable_" << std::setw(3) << std::setfill('0')
		         << i - map_related_chars.begin() + 1 << "\t\"" << *i
		         << "\""
		         << "\n";
	}
	for (std::vector<pair<string, string>>::const_iterator i =
	         phonetic_replacements.begin();
	     i != phonetic_replacements.end(); ++i) {
		log_file << "phone.rules/phonetic_replacements_" << std::setw(3)
		         << std::setfill('0')
		         << i - phonetic_replacements.begin() << "\t\""
		         << i->first << "\"\t\"" << i->second << "\""
		         << "\n";
		// TODO phone->hash
		// TODO phone->utf8
	}
	log_file << "warn/warn_flag\t" << warn_flag << "\n";
	log_file << "forbidwarn/forbid_warn\t" << forbid_warn << "\n";

	log_file << "\n"
	         << "COMPOUNDING OPTIONS"
	         << "\n";
#if 0
	for (std::vector<std::string>::const_iterator i =
	         break_patterns.begin();
	     i != break_patterns.end(); ++i) {
		log_file << "breaktable/break_patterns_" << std::setw(3)
		         << std::setfill('0') << i - break_patterns.begin() + 1
		         << "\t\"" << *i << "\""
		         << "\n";
	}
#endif
	// TODO compound_rules
	log_file << "cpdmin/compound_minimum\t" << compound_minimum << "\n";
	log_file << "compoundflag/compound_flag\t" << compound_flag << "\n";
	log_file << "compoundbegin/compound_begin_flag\t" << compound_begin_flag
	         << "\n";
	log_file << "compoundend/compound_last_flag\t" << compound_last_flag
	         << "\n";
	log_file << "compoundmiddle/compound_middle_flag\t"
	         << compound_middle_flag << "\n";
	log_file << "onlyincompound/compound_onlyin_flag\t"
	         << compound_onlyin_flag << "\n";
	log_file << "compoundpermitflag/compound_permit_flag\t"
	         << compound_permit_flag << "\n";
	log_file << "compoundforbidflag/compound_forbid_flag\t"
	         << compound_forbid_flag << "\n";
	log_file << "compoundmoresuffixes/compound_more_suffixes\t"
	         << compound_more_suffixes << "\n";
	log_file << "compoundroot/compound_root_flag\t" << compound_root_flag
	         << "\n";
	log_file << "cpdwordmax/compound_word_max\t" << compound_word_max
	         << "\n";
	log_file << "checkcompounddup/compound_check_up\t" << compound_check_up
	         << "\n";
	log_file << "checkcompoundrep/compound_check_rep\t"
	         << compound_check_rep << "\n";
	log_file << "checkcompoundcase/compound_check_case\t"
	         << compound_check_case << "\n";
	log_file << "checkcompoundtriple/compound_check_triple\t"
	         << compound_check_triple << "\n";
	log_file << "simplifiedtriple/compound_simplified_triple\t"
	         << compound_simplified_triple << "\n";
	log_file << "forceucase/compound_force_uppercase\t"
	         << compound_force_uppercase << "\n";
	log_file << "cpdmaxsyllable/compound_syllable_max\t"
	         << compound_syllable_max << "\n";
	log_file << "cpdvowels/compound_syllable_vowels\t\""
	         << compound_syllable_vowels << "\""
	         << "\n";
	log_file << "cpdsyllablenum.length/"
	            "compound_syllable_num.size\t"
	         << compound_syllable_num.size() << "\n";

	log_file << "\n"
	         << "OTHERS"
	         << "\n";
	log_file << "circumfix/circumfix_flag\t" << circumfix_flag << "\n";
	log_file << "forbiddenword/forbiddenword_flag\t" << forbiddenword_flag
	         << "\n";
	log_file << "fullstrip/fullstrip\t" << fullstrip << "\n";
	log_file << "keepcase/keepcase_flag\t" << keepcase_flag << "\n";
#if 0
	for (std::vector<pair<string, string>>::const_iterator i =
	         input_conversion.begin();
	     i != input_conversion.end(); ++i) {
		log_file << "iconvtable/input_conversion_" << std::setw(3)
		         << std::setfill('0')
		         << i - input_conversion.begin() + 1 << "\t\""
		         << i->first << "\"\t\"" << i->second << "\""
		         << "\n";
	}
	for (std::vector<pair<string, string>>::const_iterator i =
	         output_conversion.begin();
	     i != output_conversion.end(); ++i) {
		log_file << "oconvtable/output_conversion_" << std::setw(3)
		         << std::setfill('0')
		         << i - output_conversion.begin() + 1 << "\t\""
		         << i->first << "\"\t\"" << i->second << "\""
		         << "\n";
	}
#endif
	log_file << "needaffix/need_affix_flag\t" << need_affix_flag << "\n";
	log_file << "substandard/substandard_flag\t" << substandard_flag
	         << "\n";
	// deprecated? log_file << "wordchars/wordchars\t\"" <<
	// wordchars <<
	// "\"" << "\n";
	log_file << "checksharps/checksharps\t" << checksharps << "\n";

	log_file << "END" << std::endl;
}
} // namespace nuspell
