// Echelon @feature Codex 16/10/2026 core StringUtils
/*
** Implementation of the core string helpers.
*/

#include "Echelon/Core/StringUtils.h"

namespace echelon::core
{
namespace
{

bool IsAsciiAlnum(unsigned char value)
{
	return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') ||
		(value >= 'A' && value <= 'Z');
}

char AsciiLower(char character)
{
	const unsigned char value = static_cast<unsigned char>(character);
	if (value >= 'A' && value <= 'Z') {
		return static_cast<char>(value + ('a' - 'A'));
	}
	return character;
}

} // namespace

void Trim(std::string &value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		value.clear();
		return;
	}
	const size_t last = value.find_last_not_of(" \t\r\n");
	value.erase(last + 1);
	value.erase(0, first);
}

std::string TrimCopy(std::string_view value)
{
	std::string copy(value);
	Trim(copy);
	return copy;
}

void ToLower(std::string &value)
{
	for (char &character : value) {
		character = AsciiLower(character);
	}
}

std::string ToLowerCopy(std::string_view value)
{
	std::string copy(value);
	ToLower(copy);
	return copy;
}

bool EqualsNoCase(std::string_view left, std::string_view right)
{
	if (left.size() != right.size()) {
		return false;
	}
	for (size_t index = 0; index < left.size(); ++index) {
		if (AsciiLower(left[index]) != AsciiLower(right[index])) {
			return false;
		}
	}
	return true;
}

bool StartsWith(std::string_view value, std::string_view prefix)
{
	return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view value, std::string_view suffix)
{
	return value.size() >= suffix.size() &&
		value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> Split(std::string_view value, char delimiter)
{
	std::vector<std::string> fields;
	size_t start = 0;
	while (true) {
		const size_t position = value.find(delimiter, start);
		if (position == std::string_view::npos) {
			fields.emplace_back(value.substr(start));
			break;
		}
		fields.emplace_back(value.substr(start, position - start));
		start = position + 1;
	}
	return fields;
}

std::string Join(const std::vector<std::string> &fields, std::string_view delimiter)
{
	std::string result;
	for (size_t index = 0; index < fields.size(); ++index) {
		if (index != 0) {
			result.append(delimiter);
		}
		result += fields[index];
	}
	return result;
}

std::string Slugify(std::string_view value)
{
	std::string result;
	bool separatorPending = false;
	size_t index = 0;
	while (index < value.size()) {
		const unsigned char character = static_cast<unsigned char>(value[index]);
		if (character < 0x80) {
			if (IsAsciiAlnum(character)) {
				if (separatorPending && !result.empty()) {
					result.push_back('-');
				}
				separatorPending = false;
				result.push_back(AsciiLower(static_cast<char>(character)));
			} else if (!result.empty()) {
				separatorPending = true;
			}
			++index;
			continue;
		}
		// Non-ASCII input: keep the bytes as letters so UTF-8 names survive.
		// 2-byte UTF-8 Cyrillic block U+0400..U+04FF (lead 0xD0/0xD1,
		// continuation 0x80..0xBF) additionally gets case-folded.
		if ((character == 0xD0 || character == 0xD1) && index + 1 < value.size()) {
			const unsigned char next = static_cast<unsigned char>(value[index + 1]);
			if (next >= 0x80 && next <= 0xBF) {
				if (separatorPending && !result.empty()) {
					result.push_back('-');
				}
				separatorPending = false;
				if (character == 0xD0 && next >= 0x80 && next <= 0x9F) {
					if (next == 0x81) {
						// U+0401 CYRILLIC CAPITAL LETTER IO -> U+0451 small.
						result.push_back(static_cast<char>(0xD1));
						result.push_back(static_cast<char>(0x91));
					} else {
						// U+0400..U+041F -> U+0400..U+043F (second byte += 0x20).
						result.push_back(static_cast<char>(character));
						result.push_back(static_cast<char>(next + 0x20));
					}
				} else if (character == 0xD0 && next >= 0xA0 && next <= 0xAF) {
					// U+0420..U+042F -> U+0440..U+044F (lead 0xD0 -> 0xD1).
					result.push_back(static_cast<char>(0xD1));
					result.push_back(static_cast<char>(next - 0x20));
				} else {
					result.push_back(static_cast<char>(character));
					result.push_back(static_cast<char>(next));
				}
				index += 2;
				continue;
			}
		}
		// Any other non-ASCII byte passes through verbatim as a letter.
		if (separatorPending && !result.empty()) {
			result.push_back('-');
		}
		separatorPending = false;
		result.push_back(static_cast<char>(character));
		++index;
	}
	return result.empty() ? "local-modification" : result;
}

} // namespace echelon::core
