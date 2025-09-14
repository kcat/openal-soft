#ifndef WIN_MAIN_UTF8_H
#define WIN_MAIN_UTF8_H

/* For Windows systems this provides a way to get UTF-8 encoded argv strings,
 * and also overrides fopen to accept UTF-8 filenames. Working with wmain
 * directly complicates cross-platform compatibility, while normal main() in
 * Windows uses the current codepage (which has limited availability of
 * characters).
 *
 * For MinGW, you must link with -municode
 */
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#ifdef __cplusplus
#include <memory>

#define STATIC_CAST(...) static_cast<__VA_ARGS__>
#define REINTERPRET_CAST(...) reinterpret_cast<__VA_ARGS__>

#else

#define STATIC_CAST(...) (__VA_ARGS__)
#define REINTERPRET_CAST(...) (__VA_ARGS__)

#ifdef __GNUC__
__attribute__((__unused__))
#endif
static FILE *my_fopen(const char *fname, const wchar_t *wmode)
{
    wchar_t *wname;
    int namelen;
    errno_t err;
    FILE *file;

    namelen = MultiByteToWideChar(CP_UTF8, 0, fname, -1, NULL, 0);
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
    MultiByteToWideChar(CP_UTF8, 0, fname, -1, wname, namelen);

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
extern "C"
#endif
int wmain(int argc, wchar_t **wargv)
{
    char **argv;
    size_t total;
    int i;

    total = sizeof(*argv) * STATIC_CAST(size_t)(argc);
    for(i = 0;i < argc;i++)
        total += STATIC_CAST(size_t)(WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL,
            NULL));

#ifdef __cplusplus
    auto argbuf = std::make_unique<char[]>(total);
    argv = reinterpret_cast<char**>(argbuf.get());
#else
    argv = (char**)calloc(1, total);
#endif
    argv[0] = REINTERPRET_CAST(char*)(argv + argc);
    for(i = 0;i < argc-1;i++)
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], 65535, NULL, NULL);
        argv[i+1] = argv[i] + len;
    }
    WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], 65535, NULL, NULL);

#ifdef __cplusplus
    return main(argc, argv);
#else
    i = main(argc, argv);

    free(argv);
    return i;
#endif
}
#endif /* !defined(SDL_MAIN_NEEDED) && !defined(SDL_MAIN_AVAILABLE) */

#endif /* _WIN32 */

#endif /* WIN_MAIN_UTF8_H */
