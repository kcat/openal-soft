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

#ifdef _WIN32
#ifdef __MINGW32__
#define _WIN32_IE 0x501
#else
#define _WIN32_IE 0x400
#endif
#endif

#include "config.h"

#include <cstdlib>
#include <cctype>
#include <cstring>
#ifdef _WIN32_IE
#include <windows.h>
#include <shlobj.h>
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <vector>
#include <string>
#include <algorithm>

#include "alMain.h"
#include "alconfig.h"
#include "logging.h"
#include "compat.h"


namespace {

struct ConfigEntry {
    std::string key;
    std::string value;
};
al::vector<ConfigEntry> ConfOpts;


std::string &lstrip(std::string &line)
{
    size_t pos{0};
    while(pos < line.length() && std::isspace(line[pos]))
        ++pos;
    line.erase(0, pos);
    return line;
}

bool readline(std::istream &f, std::string &output)
{
    while(f.good() && f.peek() == '\n')
        f.ignore();

    return std::getline(f, output) && !output.empty();
}

std:: string expdup(const char *str)
{
    std::string output;

    while(*str != '\0')
    {
        const char *addstr;
        size_t addstrlen;

        if(str[0] != '$')
        {
            const char *next = std::strchr(str, '$');
            addstr = str;
            addstrlen = next ? static_cast<size_t>(next-str) : std::strlen(str);

            str += addstrlen;
        }
        else
        {
            str++;
            if(*str == '$')
            {
                const char *next = std::strchr(str+1, '$');
                addstr = str;
                addstrlen = next ? static_cast<size_t>(next-str) : std::strlen(str);

                str += addstrlen;
            }
            else
            {
                bool hasbraces{(*str == '{')};
                if(hasbraces) str++;

                std::string envname;
                while((std::isalnum(*str) || *str == '_'))
                    envname += *(str++);

                if(hasbraces && *str != '}')
                    continue;

                if(hasbraces) str++;
                if((addstr=std::getenv(envname.c_str())) == nullptr)
                    continue;
                addstrlen = std::strlen(addstr);
            }
        }
        if(addstrlen == 0)
            continue;

        output.append(addstr, addstrlen);
    }

    return output;
}

void LoadConfigFromFile(std::istream &f)
{
    std::string curSection;
    std::string buffer;

    while(readline(f, buffer))
    {
        while(!buffer.empty() && std::isspace(buffer.back()))
            buffer.pop_back();
        if(lstrip(buffer).empty())
            continue;

        buffer.push_back(0);
        char *line{&buffer[0]};

        if(line[0] == '[')
        {
            char *section = line+1;
            char *endsection;

            endsection = std::strchr(section, ']');
            if(!endsection || section == endsection)
            {
                ERR("config parse error: bad line \"%s\"\n", line);
                continue;
            }
            if(endsection[1] != 0)
            {
                char *end = endsection+1;
                while(std::isspace(*end))
                    ++end;
                if(*end != 0 && *end != '#')
                {
                    ERR("config parse error: bad line \"%s\"\n", line);
                    continue;
                }
            }
            *endsection = 0;

            curSection.clear();
            if(strcasecmp(section, "general") != 0)
            {
                do {
                    char *nextp = std::strchr(section, '%');
                    if(!nextp)
                    {
                        curSection += section;
                        break;
                    }

                    curSection.append(section, nextp);
                    section = nextp;

                    if(((section[1] >= '0' && section[1] <= '9') ||
                        (section[1] >= 'a' && section[1] <= 'f') ||
                        (section[1] >= 'A' && section[1] <= 'F')) &&
                       ((section[2] >= '0' && section[2] <= '9') ||
                        (section[2] >= 'a' && section[2] <= 'f') ||
                        (section[2] >= 'A' && section[2] <= 'F')))
                    {
                        unsigned char b = 0;
                        if(section[1] >= '0' && section[1] <= '9')
                            b = (section[1]-'0') << 4;
                        else if(section[1] >= 'a' && section[1] <= 'f')
                            b = (section[1]-'a'+0xa) << 4;
                        else if(section[1] >= 'A' && section[1] <= 'F')
                            b = (section[1]-'A'+0x0a) << 4;
                        if(section[2] >= '0' && section[2] <= '9')
                            b |= (section[2]-'0');
                        else if(section[2] >= 'a' && section[2] <= 'f')
                            b |= (section[2]-'a'+0xa);
                        else if(section[2] >= 'A' && section[2] <= 'F')
                            b |= (section[2]-'A'+0x0a);
                        curSection += static_cast<char>(b);
                        section += 3;
                    }
                    else if(section[1] == '%')
                    {
                        curSection += '%';
                        section += 2;
                    }
                    else
                    {
                        curSection += '%';
                        section += 1;
                    }
                } while(*section != 0);
            }

            continue;
        }

        char *comment{std::strchr(line, '#')};
        if(comment) *(comment++) = 0;
        if(!line[0]) continue;

        char key[256]{};
        char value[256]{};
        if(std::sscanf(line, "%255[^=] = \"%255[^\"]\"", key, value) == 2 ||
           std::sscanf(line, "%255[^=] = '%255[^\']'", key, value) == 2 ||
           std::sscanf(line, "%255[^=] = %255[^\n]", key, value) == 2)
        {
            /* sscanf doesn't handle '' or "" as empty values, so clip it
             * manually. */
            if(std::strcmp(value, "\"\"") == 0 || std::strcmp(value, "''") == 0)
                value[0] = 0;
        }
        else if(sscanf(line, "%255[^=] %255[=]", key, value) == 2)
        {
            /* Special case for 'key =' */
            value[0] = 0;
        }
        else
        {
            ERR("config parse error: malformed option line: \"%s\"\n\n", line);
            continue;
        }

        std::string fullKey;
        if(!curSection.empty())
        {
            fullKey += curSection;
            fullKey += '/';
        }
        fullKey += key;
        while(!fullKey.empty() && std::isspace(fullKey.back()))
            fullKey.pop_back();

        /* Check if we already have this option set */
        auto ent = std::find_if(ConfOpts.begin(), ConfOpts.end(),
            [&fullKey](const ConfigEntry &entry) -> bool
            { return entry.key == fullKey; }
        );
        if(ent != ConfOpts.end())
            ent->value = expdup(value);
        else
        {
            ConfOpts.emplace_back(ConfigEntry{std::move(fullKey), expdup(value)});
            ent = ConfOpts.end()-1;
        }

        TRACE("found '%s' = '%s'\n", ent->key.c_str(), ent->value.c_str());
    }
    ConfOpts.shrink_to_fit();
}

} // namespace


#ifdef _WIN32
void ReadALConfig()
{
    WCHAR buffer[MAX_PATH];
    if(SHGetSpecialFolderPathW(nullptr, buffer, CSIDL_APPDATA, FALSE) != FALSE)
    {
        std::string filepath{wstr_to_utf8(buffer)};
        filepath += "\\alsoft.ini";

        TRACE("Loading config %s...\n", filepath.c_str());
        al::ifstream f{filepath};
        if(f.is_open())
            LoadConfigFromFile(f);
    }

    std::string ppath{GetProcBinary().path};
    if(!ppath.empty())
    {
        ppath += "\\alsoft.ini";
        TRACE("Loading config %s...\n", ppath.c_str());
        al::ifstream f{ppath};
        if(f.is_open())
            LoadConfigFromFile(f);
    }

    const WCHAR *str{_wgetenv(L"ALSOFT_CONF")};
    if(str != nullptr && *str)
    {
        std::string filepath{wstr_to_utf8(str)};

        TRACE("Loading config %s...\n", filepath.c_str());
        al::ifstream f{filepath};
        if(f.is_open())
            LoadConfigFromFile(f);
    }
}
#else
void ReadALConfig()
{
    const char *str{"/etc/openal/alsoft.conf"};

    TRACE("Loading config %s...\n", str);
    al::ifstream f{str};
    if(f.is_open())
        LoadConfigFromFile(f);
    f.close();

    if(!(str=getenv("XDG_CONFIG_DIRS")) || str[0] == 0)
        str = "/etc/xdg";
    std::string confpaths = str;
    /* Go through the list in reverse, since "the order of base directories
     * denotes their importance; the first directory listed is the most
     * important". Ergo, we need to load the settings from the later dirs
     * first so that the settings in the earlier dirs override them.
     */
    std::string fname;
    while(!confpaths.empty())
    {
        auto next = confpaths.find_last_of(':');
        if(next < confpaths.length())
        {
            fname = confpaths.substr(next+1);
            confpaths.erase(next);
        }
        else
        {
            fname = confpaths;
            confpaths.clear();
        }

        if(fname.empty() || fname.front() != '/')
            WARN("Ignoring XDG config dir: %s\n", fname.c_str());
        else
        {
            if(fname.back() != '/') fname += "/alsoft.conf";
            else fname += "alsoft.conf";

            TRACE("Loading config %s...\n", fname.c_str());
            al::ifstream f{fname};
            if(f.is_open())
                LoadConfigFromFile(f);
        }
        fname.clear();
    }

#ifdef __APPLE__
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    if(mainBundle)
    {
        unsigned char fileName[PATH_MAX];
        CFURLRef configURL;

        if((configURL=CFBundleCopyResourceURL(mainBundle, CFSTR(".alsoftrc"), CFSTR(""), nullptr)) &&
           CFURLGetFileSystemRepresentation(configURL, true, fileName, sizeof(fileName)))
        {
            al::ifstream f{reinterpret_cast<char*>(fileName)};
            if(f.is_open())
                LoadConfigFromFile(f);
        }
    }
#endif

    if((str=getenv("HOME")) != nullptr && *str)
    {
        fname = str;
        if(fname.back() != '/') fname += "/.alsoftrc";
        else fname += ".alsoftrc";

        TRACE("Loading config %s...\n", fname.c_str());
        al::ifstream f{fname};
        if(f.is_open())
            LoadConfigFromFile(f);
    }

    if((str=getenv("XDG_CONFIG_HOME")) != nullptr && str[0] != 0)
    {
        fname = str;
        if(fname.back() != '/') fname += "/alsoft.conf";
        else fname += "alsoft.conf";
    }
    else
    {
        fname.clear();
        if((str=getenv("HOME")) != nullptr && str[0] != 0)
        {
            fname = str;
            if(fname.back() != '/') fname += "/.config/alsoft.conf";
            else fname += ".config/alsoft.conf";
        }
    }
    if(!fname.empty())
    {
        TRACE("Loading config %s...\n", fname.c_str());
        al::ifstream f{fname};
        if(f.is_open())
            LoadConfigFromFile(f);
    }

    std::string ppath{GetProcBinary().path};
    if(!ppath.empty())
    {
        if(ppath.back() != '/') ppath += "/alsoft.conf";
        else ppath += "alsoft.conf";

        TRACE("Loading config %s...\n", ppath.c_str());
        al::ifstream f{ppath};
        if(f.is_open())
            LoadConfigFromFile(f);
    }

    if((str=getenv("ALSOFT_CONF")) != nullptr && *str)
    {
        TRACE("Loading config %s...\n", str);
        al::ifstream f{str};
        if(f.is_open())
            LoadConfigFromFile(f);
    }
}
#endif

const char *GetConfigValue(const char *devName, const char *blockName, const char *keyName, const char *def)
{
    if(!keyName)
        return def;

    std::string key;
    if(blockName && strcasecmp(blockName, "general") != 0)
    {
        key = blockName;
        if(devName)
        {
            key += '/';
            key += devName;
        }
        key += '/';
        key += keyName;
    }
    else
    {
        if(devName)
        {
            key = devName;
            key += '/';
        }
        key += keyName;
    }

    auto iter = std::find_if(ConfOpts.cbegin(), ConfOpts.cend(),
        [&key](const ConfigEntry &entry) -> bool
        { return entry.key == key; }
    );
    if(iter != ConfOpts.cend())
    {
        TRACE("Found %s = \"%s\"\n", key.c_str(), iter->value.c_str());
        if(!iter->value.empty())
            return iter->value.c_str();
        return def;
    }

    if(!devName)
    {
        TRACE("Key %s not found\n", key.c_str());
        return def;
    }
    return GetConfigValue(nullptr, blockName, keyName, def);
}

int ConfigValueExists(const char *devName, const char *blockName, const char *keyName)
{
    const char *val = GetConfigValue(devName, blockName, keyName, "");
    return val[0] != 0;
}

al::optional<std::string> ConfigValueStr(const char *devName, const char *blockName, const char *keyName)
{
    const char *val = GetConfigValue(devName, blockName, keyName, "");
    if(!val[0]) return al::nullopt;

    return al::make_optional<std::string>(val);
}

al::optional<int> ConfigValueInt(const char *devName, const char *blockName, const char *keyName)
{
    const char *val = GetConfigValue(devName, blockName, keyName, "");
    if(!val[0]) return al::nullopt;

    return al::make_optional(static_cast<int>(std::strtol(val, nullptr, 0)));
}

al::optional<unsigned int> ConfigValueUInt(const char *devName, const char *blockName, const char *keyName)
{
    const char *val = GetConfigValue(devName, blockName, keyName, "");
    if(!val[0]) return al::nullopt;

    return al::make_optional(static_cast<unsigned int>(std::strtoul(val, nullptr, 0)));
}

al::optional<float> ConfigValueFloat(const char *devName, const char *blockName, const char *keyName)
{
    const char *val = GetConfigValue(devName, blockName, keyName, "");
    if(!val[0]) return al::nullopt;

    return al::make_optional(std::strtof(val, nullptr));
}

al::optional<bool> ConfigValueBool(const char *devName, const char *blockName, const char *keyName)
{
    const char *val = GetConfigValue(devName, blockName, keyName, "");
    if(!val[0]) return al::nullopt;

    return al::make_optional(
        strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0 ||
        strcasecmp(val, "on") == 0 || atoi(val) != 0);
}

int GetConfigValueBool(const char *devName, const char *blockName, const char *keyName, int def)
{
    const char *val = GetConfigValue(devName, blockName, keyName, "");

    if(!val[0]) return def != 0;
    return (strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0 ||
            strcasecmp(val, "on") == 0 || atoi(val) != 0);
}
