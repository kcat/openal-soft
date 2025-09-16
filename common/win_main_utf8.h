#ifndef WIN_MAIN_UTF8_H
#define WIN_MAIN_UTF8_H

/* For Windows systems this provides a way to get UTF-8 encoded argv strings,
 * and also overrides fopen to accept UTF-8 filenames. Working with wmain
 * directly complicates cross-platform compatibility, while normal main() in
 * Windows uses the current codepage (which may have limited availability of
 * characters).
 *
 * For MinGW, you must link with -municode
 */
#ifdef _WIN32
#include "config.h"

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#ifndef __cplusplus
#ifdef __GNUC__
__attribute__((__unused__))
#endif
static FILE *my_fopen(const char *fname, const wchar_t *wmode)
{
    wchar_t *wname;
    int namelen;
    errno_t err;
    FILE *file;

    namelen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fname, -1, NULL, 0);
    if(namelen <= 0)
    {
        fprintf(stderr, "Failed to convert UTF-8 fname \"%s\"\n", fname);
        return NULL;
    }

    wname = (wchar_t*)calloc((size_t)namelen, sizeof(wchar_t));
    if(!wname)
    {
        fprintf(stderr, "Failed to allocate %zu bytes for fname conversion\n",
            sizeof(wchar_t)*(size_t)namelen);
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fname, -1, wname, namelen);

    err = _wfopen_s(&file, wname, wmode);
    if(err)
    {
        errno = err;
        file = NULL;
    }

    free(wname);
    return file;
}
#define fopen(f, m) my_fopen((f), (L##m))
#endif


/* SDL overrides main and provides UTF-8 args for us. */
#if !defined(SDL_MAIN_NEEDED) && !defined(SDL_MAIN_AVAILABLE)
int my_main(int, char**);
#define main my_main

#ifdef __cplusplus
#include <iostream>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>

#include "alnumeric.h"
#include "fmt/base.h"
#include "fmt/ostream.h"

#if HAVE_CXXMODULES
import alsoft.gsl;
#else
#include "gsl/gsl"
#endif

extern "C"
auto wmain(int argc, wchar_t **wargv) -> int
{
    static constexpr auto flags = DWORD{WC_ERR_INVALID_CHARS};
    const auto wargs = std::span{wargv, gsl::narrow<size_t>(argc)};
    auto argstr = std::string{};
    try {
        for(std::wstring_view arg : wargs)
        {
            if(arg.empty())
            {
                argstr.push_back('\0');
                continue;
            }

            const auto u16len = gsl::narrow<int>(arg.size());
            const auto u8len = WideCharToMultiByte(CP_UTF8, flags, arg.data(), u16len, nullptr, 0,
                nullptr, nullptr);
            if(u8len < 1)
                throw std::runtime_error{"WideCharToMultiByte failed"};

            const auto curlen = argstr.empty() ? argstr.size() : (argstr.size()+1);
            const auto newlen = curlen + gsl::narrow<size_t>(u8len);
            argstr.resize(newlen, '\0');
            if(WideCharToMultiByte(CP_UTF8, flags, arg.data(), u16len, &argstr[curlen], u8len,
                nullptr, nullptr) < 1)
                throw std::runtime_error{"WideCharToMultiByte failed"};
        }
    }
    catch(std::exception& e) {
        fmt::println(std::cerr, "Failed to convert command line to UTF-8: {}", e.what());
        return -1;
    }

    auto argv = std::vector<char*>(wargs.size());
    auto stridx = 0_uz;
    std::ranges::generate(argv, [&argstr,&stridx]
    {
        auto *ret = &argstr[stridx];
        stridx = argstr.find('\0', stridx) + 1;
        return ret;
    });

    return main(argc, argv.data());
}

#else /* __cplusplus */

int wmain(int argc, wchar_t **wargv)
{
    char **argv;
    size_t total;
    int i;

    total = sizeof(*argv) * (size_t)argc;
    for(i = 0;i < argc;i++)
        total += (size_t)WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);

    argv = (char**)calloc(1, total);
    argv[0] = (char*)(argv + argc);
    for(i = 0;i < argc-1;i++)
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], 65535, NULL, NULL);
        argv[i+1] = argv[i] + len;
    }
    WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], 65535, NULL, NULL);

    i = main(argc, argv);

    free(argv);
    return i;
}
#endif /* __cplusplus */

#endif /* !defined(SDL_MAIN_NEEDED) && !defined(SDL_MAIN_AVAILABLE) */

#endif /* _WIN32 */

#endif /* WIN_MAIN_UTF8_H */
