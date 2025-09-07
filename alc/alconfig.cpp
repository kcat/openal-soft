/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "alconfig.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "almalloc.h"
#include "alnumeric.h"
#include "alstring.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "fmt/ranges.h"
#include "gsl/gsl"
#include "strutils.hpp"

#if ALSOFT_UWP
#include <winrt/Windows.Media.Core.h> // !!This is important!!
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
using namespace winrt;
#endif

namespace {

using namespace std::string_view_literals;

const auto EmptyString = std::string{};

struct ConfigEntry {
    std::string key;
    std::string value;

    ConfigEntry(auto&& key_, auto&& value_)
        : key{std::forward<decltype(key_)>(key_)}, value{std::forward<decltype(value_)>(value_)}
    { }
};
std::vector<ConfigEntry> ConfOpts;


/* True UTF-8 validation is way beyond me. However, this should weed out any
 * obviously non-UTF-8 text.
 *
 * The general form of the byte stream is relatively simple. The first byte of
 * a codepoint either has a 0 bit for the msb, indicating a single-byte ASCII-
 * compatible codepoint, or the number of bytes that make up the codepoint
 * (including itself) indicated by the number of successive 1 bits, and each
 * successive byte of the codepoint has '10' for the top bits. That is:
 *
 * 0xxxxxxx - single-byte ASCII-compatible codepoint
 * 110xxxxx 10xxxxxx - two-byte codepoint
 * 1110xxxx 10xxxxxx 10xxxxxx - three-byte codepoint
 * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx - four-byte codepoint
 * ... etc ...
 *
 * Where the 'x' bits are concatenated together to form a 32-bit Unicode
 * codepoint. This doesn't check whether the codepoints themselves are valid,
 * it just validates the correct number of bytes for multi-byte sequences.
 */
auto validate_utf8(const std::string_view str) -> bool
{
    auto const end = str.end();
    /* Look for the first multi-byte/non-ASCII codepoint. */
    auto current = std::ranges::find_if(str.begin(), end,
        [](const char ch) -> bool { return (ch&0x80) != 0; });
    while(const auto remaining = std::distance(current, end))
    {
        /* Get the number of bytes that make up this codepoint (must be at
         * least 2). This includes the current byte.
         */
        const auto tocheck = std::countl_one(as_unsigned(*current));
        if(tocheck < 2 || tocheck > remaining)
            return false;

        const auto next = std::next(current, tocheck);

        /* Check that the following bytes are a proper continuation. */
        const auto valid = std::ranges::all_of(std::next(current), next,
            [](const char ch) -> bool { return (ch&0xc0) == 0x80; });
        if(not valid)
            return false;

        /* Seems okay. Look for the next multi-byte/non-ASCII codepoint. */
        current = std::ranges::find_if(next, end, [](const char ch) -> bool { return ch&0x80; });
    }
    return true;
}

auto lstrip(std::string &line) -> std::string&
{
    auto iter = std::ranges::find_if_not(line, [](const char c) { return std::isspace(c); });
    line.erase(line.begin(), iter);
    return line;
}

auto expdup(std::string_view str) -> std::string
{
    auto output = std::string{};

    while(!str.empty())
    {
        if(auto nextpos = str.find('$'))
        {
            output += str.substr(0, nextpos);
            if(nextpos == std::string_view::npos)
                break;

            str.remove_prefix(nextpos);
        }

        str.remove_prefix(1);
        if(str.empty())
        {
            output += '$';
            break;
        }
        if(str.front() == '$')
        {
            output += '$';
            str.remove_prefix(1);
            continue;
        }

        const auto hasbraces = bool{str.front() == '{'};
        if(hasbraces) str.remove_prefix(1);

        const auto envenditer = std::ranges::find_if_not(str,
            [](const char c) { return c == '_' || std::isalnum(c); });

        if(hasbraces && (envenditer == str.end() || *envenditer != '}'))
            continue;

        const auto envend = gsl::narrow<size_t>(std::distance(str.begin(), envenditer));
        const auto envname = std::string{str.substr(0, envend)};
        str.remove_prefix(envend + hasbraces);

        if(const auto envval = al::getenv(envname.c_str()))
            output += *envval;
    }

    return output;
}

void LoadConfigFromFile(std::istream &f)
{
    constexpr auto whitespace_chars = " \t\n\f\r\v"sv;

    auto curSection = std::string{};
    auto buffer = std::string{};
    auto linenum = 0_uz;

    while(std::getline(f, buffer))
    {
        ++linenum;
        if(lstrip(buffer).empty())
            continue;

        auto cmtpos = std::min(buffer.find('#'), buffer.size());
        if(cmtpos != 0)
            cmtpos = buffer.find_last_not_of(whitespace_chars, cmtpos-1)+1;
        if(cmtpos == 0) continue;
        buffer.erase(cmtpos);

        if(not validate_utf8(buffer))
        {
            ERR(" config parse error: non-UTF-8 characters on line {}", linenum);
            ERR("{}", fmt::format("  {::#04x}",
                buffer|std::views::transform([](auto c) { return as_unsigned(c); })));
            continue;
        }

        if(buffer[0] == '[')
        {
            const auto endpos = buffer.find(']', 1);
            if(endpos == 1 || endpos == std::string::npos)
            {
                ERR(" config parse error on line {}: bad section \"{}\"", linenum, buffer);
                continue;
            }
            if(const auto last = buffer.find_first_not_of(whitespace_chars, endpos+1);
                last < buffer.size() && buffer[last] != '#')
            {
                ERR(" config parse error on line {}: extraneous characters after section \"{}\"",
                    linenum, buffer);
                continue;
            }

            auto section = std::string_view{buffer}.substr(1, endpos-1);

            curSection.clear();
            if(al::case_compare(section, "general"sv) == 0)
                continue;

            while(!section.empty())
            {
                const auto nextp = section.find('%');
                if(nextp == std::string_view::npos)
                {
                    curSection += section;
                    break;
                }

                curSection += section.substr(0, nextp);
                section.remove_prefix(nextp);

                if(section.size() > 2
                    && ((section[1] >= '0' && section[1] <= '9')
                        || (section[1] >= 'a' && section[1] <= 'f')
                        || (section[1] >= 'A' && section[1] <= 'F'))
                    && ((section[2] >= '0' && section[2] <= '9')
                        || (section[2] >= 'a' && section[2] <= 'f')
                        || (section[2] >= 'A' && section[2] <= 'F')))
                {
                    auto b = 0;
                    if(section[1] >= '0' && section[1] <= '9')
                        b = (section[1]-'0') << 4;
                    else if(section[1] >= 'a' && section[1] <= 'f')
                        b = (section[1]-'a'+0xa) << 4;
                    else if(section[1] >= 'A' && section[1] <= 'F')
                        b = (section[1]-'A'+0x0a) << 4;
                    if(section[2] >= '0' && section[2] <= '9')
                        b |= section[2]-'0';
                    else if(section[2] >= 'a' && section[2] <= 'f')
                        b |= section[2]-'a'+0xa;
                    else if(section[2] >= 'A' && section[2] <= 'F')
                        b |= section[2]-'A'+0x0a;
                    curSection += gsl::narrow_cast<char>(b);
                    section.remove_prefix(3);
                }
                else if(section.size() > 1 && section[1] == '%')
                {
                    curSection += '%';
                    section.remove_prefix(2);
                }
                else
                {
                    curSection += '%';
                    section.remove_prefix(1);
                }
            }

            continue;
        }

        auto sep = buffer.find('=');
        if(sep == std::string::npos)
        {
            ERR(" config parse error on line {}: malformed option \"{}\"", linenum, buffer);
            continue;
        }
        auto keypart = std::string_view{buffer}.substr(0, sep++);
        keypart.remove_suffix(keypart.size() - (keypart.find_last_not_of(whitespace_chars)+1));
        if(keypart.empty())
        {
            ERR(" config parse error on line {}: malformed option \"{}\"", linenum, buffer);
            continue;
        }
        auto valpart = std::string_view{buffer}.substr(sep);
        valpart.remove_prefix(std::min(valpart.find_first_not_of(whitespace_chars),
            valpart.size()));

        auto fullKey = curSection;
        if(!fullKey.empty())
            fullKey += '/';
        fullKey += keypart;

        if(valpart.size() > size_t{std::numeric_limits<int>::max()})
        {
            ERR(" config parse error on line {}: value too long \"{}\"", linenum, buffer);
            continue;
        }
        if(valpart.size() > 1)
        {
            if((valpart.front() == '"' && valpart.back() == '"')
                || (valpart.front() == '\'' && valpart.back() == '\''))
            {
                valpart.remove_prefix(1);
                valpart.remove_suffix(1);
            }
        }

        TRACE(" setting '{}' = '{}'", fullKey, valpart);

        /* Check if we already have this option set */
        const auto ent = std::ranges::find(ConfOpts, fullKey, &ConfigEntry::key);
        if(ent != ConfOpts.end())
        {
            if(!valpart.empty())
                ent->value = expdup(valpart);
            else
                ConfOpts.erase(ent);
        }
        else if(!valpart.empty())
            ConfOpts.emplace_back(std::move(fullKey), expdup(valpart));
    }
    ConfOpts.shrink_to_fit();
}

auto GetConfigValue(const std::string_view devName, const std::string_view blockName,
    const std::string_view keyName) -> const std::string&
{
    if(keyName.empty())
        return EmptyString;

    auto key = std::string{};
    if(!blockName.empty() && al::case_compare(blockName, "general"sv) != 0)
    {
        key = blockName;
        key += '/';
    }
    if(!devName.empty())
    {
        key += devName;
        key += '/';
    }
    key += keyName;

    const auto iter = std::ranges::find(ConfOpts, key, &ConfigEntry::key);
    if(iter != ConfOpts.cend())
    {
        TRACE("Found option {} = \"{}\"", key, iter->value);
        if(!iter->value.empty())
            return iter->value;
        return EmptyString;
    }

    if(devName.empty())
        return EmptyString;
    return GetConfigValue({}, blockName, keyName);
}

} // namespace


#ifdef _WIN32
void ReadALConfig()
{
    auto path = std::filesystem::path{};

#if !defined(_GAMING_XBOX)
    {
#if !ALSOFT_UWP
        auto bufstore = std::unique_ptr<WCHAR, decltype([](WCHAR *mem){ CoTaskMemFree(mem); })>{};
        const auto hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DONT_UNEXPAND,
            nullptr, al::out_ptr(bufstore));
        if(SUCCEEDED(hr))
        {
            const auto buffer = std::wstring_view{bufstore.get()};
#else
        auto localSettings = winrt::Windows::Storage::ApplicationDataContainer{
            winrt::Windows::Storage::ApplicationData::Current().LocalSettings()};
        auto bufstore = Windows::Storage::ApplicationData::Current().RoamingFolder().Path();
        auto buffer = std::wstring_view{bufstore};
        {
#endif
            path = std::filesystem::path{buffer};
            path /= L"alsoft.ini";

            TRACE("Loading config {}...", al::u8_as_char(path.u8string()));
            if(auto f = std::ifstream{path}; f.is_open())
                LoadConfigFromFile(f);
        }
    }
#endif

    path = std::filesystem::path(al::char_as_u8(GetProcBinary().path));
    if(!path.empty())
    {
        path /= L"alsoft.ini";
        TRACE("Loading config {}...", al::u8_as_char(path.u8string()));
        if(auto f = std::ifstream{path}; f.is_open())
            LoadConfigFromFile(f);
    }

    if(auto confpath = al::getenv(L"ALSOFT_CONF"))
    {
        path = *confpath;
        TRACE("Loading config {}...", al::u8_as_char(path.u8string()));
        if(auto f = std::ifstream{path}; f.is_open())
            LoadConfigFromFile(f);
    }
}

#else

void ReadALConfig()
{
    auto path = std::filesystem::path{"/etc/openal/alsoft.conf"};

    TRACE("Loading config {}...", al::u8_as_char(path.u8string()));
    if(auto f = std::ifstream{path}; f.is_open())
        LoadConfigFromFile(f);

    auto confpaths = al::getenv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    /* Go through the list in reverse, since "the order of base directories
     * denotes their importance; the first directory listed is the most
     * important". Ergo, we need to load the settings from the later dirs
     * first so that the settings in the earlier dirs override them.
     */
    while(!confpaths.empty())
    {
        auto next = confpaths.rfind(':');
        if(next < confpaths.length())
        {
            path = std::filesystem::path{std::string_view{confpaths}.substr(next+1)}
                .lexically_normal();
            confpaths.erase(next);
        }
        else
        {
            path = std::filesystem::path{confpaths}.lexically_normal();
            confpaths.clear();
        }

        if(!path.is_absolute())
            WARN("Ignoring XDG config dir: {}", al::u8_as_char(path.u8string()));
        else
        {
            path /= "alsoft.conf";

            TRACE("Loading config {}...", al::u8_as_char(path.u8string()));
            if(auto f = std::ifstream{path}; f.is_open())
                LoadConfigFromFile(f);
        }
    }

#ifdef __APPLE__
    auto mainBundle = CFBundleRef{CFBundleGetMainBundle()};
    if(mainBundle)
    {
        auto configURL = CFURLRef{CFBundleCopyResourceURL(mainBundle, CFSTR(".alsoftrc"),
            CFSTR(""), nullptr)};

        auto fileName = std::array<unsigned char,PATH_MAX>{};
        if(configURL && CFURLGetFileSystemRepresentation(configURL, true, fileName.data(), fileName.size()))
        {
            if(auto f = std::ifstream{reinterpret_cast<char*>(fileName.data())}; f.is_open())
                LoadConfigFromFile(f);
        }
    }
#endif

    if(auto homedir = al::getenv("HOME"))
    {
        path = *homedir;
        path /= ".alsoftrc";

        TRACE("Loading config {}...", al::u8_as_char(path.u8string()));
        if(auto f = std::ifstream{path}; f.is_open())
            LoadConfigFromFile(f);
    }

    if(auto configdir = al::getenv("XDG_CONFIG_HOME"))
    {
        path = *configdir;
        path /= "alsoft.conf";
    }
    else
    {
        path.clear();
        if(auto homedir = al::getenv("HOME"))
        {
            path = *homedir;
            path /= ".config/alsoft.conf";
        }
    }
    if(!path.empty())
    {
        TRACE("Loading config {}...", al::u8_as_char(path.u8string()));
        if(auto f = std::ifstream{path}; f.is_open())
            LoadConfigFromFile(f);
    }

    path = GetProcBinary().path;
    if(!path.empty())
    {
        path /= "alsoft.conf";

        TRACE("Loading config {}...", al::u8_as_char(path.u8string()));
        if(auto f = std::ifstream{path}; f.is_open())
            LoadConfigFromFile(f);
    }

    if(auto confname = al::getenv("ALSOFT_CONF"))
    {
        TRACE("Loading config {}...", *confname);
        if(auto f = std::ifstream{*confname}; f.is_open())
            LoadConfigFromFile(f);
    }
}
#endif

auto ConfigValueStr(const std::string_view devName, const std::string_view blockName,
    const std::string_view keyName) -> std::optional<std::string>
{
    if(auto&& val = GetConfigValue(devName, blockName, keyName); !val.empty())
        return val;
    return std::nullopt;
}

auto ConfigValueInt(const std::string_view devName, const std::string_view blockName,
    const std::string_view keyName) -> std::optional<int>
{
    if(auto&& val = GetConfigValue(devName, blockName, keyName); !val.empty()) try {
        return std::stoi(val, nullptr, 0);
    }
    catch(std::out_of_range&) {
        WARN("Option is out of range of int: {} = {}", keyName, val);
    }
    catch(std::exception&) {
        WARN("Option is not an int: {} = {}", keyName, val);
    }

    return std::nullopt;
}

auto ConfigValueUInt(const std::string_view devName, const std::string_view blockName,
    const std::string_view keyName) -> std::optional<unsigned int>
{
    if(auto&& val = GetConfigValue(devName, blockName, keyName); !val.empty()) try {
        return gsl::narrow<unsigned int>(std::stoul(val, nullptr, 0));
    }
    catch(std::out_of_range&) {
        WARN("Option is out of range of unsigned int: {} = {}", keyName, val);
    }
    catch(gsl::narrowing_error&) {
        WARN("Option is out of range of unsigned int: {} = {}", keyName, val);
    }
    catch(std::exception&) {
        WARN("Option is not an unsigned int: {} = {}", keyName, val);
    }
    return std::nullopt;
}

auto ConfigValueFloat(const std::string_view devName, const std::string_view blockName,
    const std::string_view keyName) -> std::optional<float>
{
    if(auto&& val = GetConfigValue(devName, blockName, keyName); !val.empty()) try {
        return std::stof(val);
    }
    catch(std::exception&) {
        WARN("Option is not a float: {} = {}", keyName, val);
    }
    return std::nullopt;
}

auto ConfigValueBool(const std::string_view devName, const std::string_view blockName,
    const std::string_view keyName) -> std::optional<bool>
{
    if(auto&& val = GetConfigValue(devName, blockName, keyName); !val.empty()) try {
        return al::case_compare(val, "on"sv) == 0 || al::case_compare(val, "yes"sv) == 0
            || al::case_compare(val, "true"sv) == 0 || std::stoll(val) != 0;
    }
    catch(std::out_of_range&) {
        /* If out of range, the value is some non-0 (true) value and it doesn't
         * matter that it's too big or small.
         */
        return true;
    }
    catch(std::exception&) {
        /* If stoll fails to convert for any other reason, it's some other word
         * that's treated as false.
         */
        return false;
    }
    return std::nullopt;
}

auto GetConfigValueBool(const std::string_view devName, const std::string_view blockName,
    const std::string_view keyName, bool def) -> bool
{
    if(auto&& val = GetConfigValue(devName, blockName, keyName); !val.empty()) try {
        return al::case_compare(val, "on"sv) == 0 || al::case_compare(val, "yes"sv) == 0
            || al::case_compare(val, "true"sv) == 0 || std::stoll(val) != 0;
    }
    catch(std::out_of_range&) {
        return true;
    }
    catch(std::exception&) {
        return false;
    }
    return def;
}
