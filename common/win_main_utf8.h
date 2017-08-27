#ifndef WIN_MAIN_UTF8_H
#define WIN_MAIN_UTF8_H

/* For Windows systems this overrides main() so that the argv strings are UTF-8
 * encoded, and also overrides fopen to accept UTF-8 filenames. Working with
 * wmain directly complicates cross-platform compatibility, while normal main()
 * in Windows uses the current codepage (which has limited availability of
 * characters).
 *
 * For MinGW, you must link with -municode
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static char *ToUTF8(const wchar_t *from)
{
    char *out = NULL;
    int len;
    if((len=WideCharToMultiByte(CP_UTF8, 0, from, -1, NULL, 0, NULL, NULL)) > 0)
    {
        out = calloc(sizeof(*out), len);
        WideCharToMultiByte(CP_UTF8, 0, from, -1, out, len, NULL, NULL);
        out[len-1] = 0;
    }
    return out;
}

static WCHAR *FromUTF8(const char *str)
{
    WCHAR *out = NULL;
    int len;

    if((len=MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0)) > 0)
    {
        out = calloc(sizeof(WCHAR), len);
        MultiByteToWideChar(CP_UTF8, 0, str, -1, out, len);
        out[len-1] = 0;
    }
    return out;
}


static FILE *my_fopen(const char *fname, const char *mode)
{
    WCHAR *wname=NULL, *wmode=NULL;
    FILE *file = NULL;

    wname = FromUTF8(fname);
    wmode = FromUTF8(mode);
    if(!wname)
        fprintf(stderr, "Failed to convert UTF-8 filename: \"%s\"\n", fname);
    else if(!wmode)
        fprintf(stderr, "Failed to convert UTF-8 mode: \"%s\"\n", mode);
    else
        file = _wfopen(wname, wmode);

    free(wname);
    free(wmode);

    return file;
}
#define fopen my_fopen


#define main my_main
int main(int argc, char *argv[]);

static char **arglist;
static void cleanup_arglist(void)
{
    int i;
    for(i = 0;arglist[i];i++)
        free(arglist[i]);
    free(arglist);
}

int wmain(int argc, const wchar_t *wargv[])
{
    int i;

    atexit(cleanup_arglist);
    arglist = calloc(sizeof(*arglist), argc+1);
    for(i = 0;i < argc;i++)
        arglist[i] = ToUTF8(wargv[i]);

    return main(argc, arglist);
}

#endif

#endif /* WIN_MAIN_UTF8_H */
