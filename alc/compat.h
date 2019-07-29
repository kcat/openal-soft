#ifndef AL_COMPAT_H
#define AL_COMPAT_H

#ifdef __cplusplus

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <string>
#include <fstream>

inline std::string wstr_to_utf8(const WCHAR *wstr)
{
    std::string ret;

    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if(len > 0)
    {
        ret.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &ret[0], len, nullptr, nullptr);
        ret.pop_back();
    }

    return ret;
}

inline std::wstring utf8_to_wstr(const char *str)
{
    std::wstring ret;

    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if(len > 0)
    {
        ret.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, str, -1, &ret[0], len);
        ret.pop_back();
    }

    return ret;
}


namespace al {

// Windows' std::ifstream fails with non-ANSI paths since the standard only
// specifies names using const char* (or std::string). MSVC has a non-standard
// extension using const wchar_t* (or std::wstring?) to handle Unicode paths,
// but not all Windows compilers support it. So we have to make our own istream
// that accepts UTF-8 paths and forwards to Unicode-aware I/O functions.
class filebuf final : public std::streambuf {
    std::array<char_type,4096> mBuffer;
    HANDLE mFile{INVALID_HANDLE_VALUE};

    int_type underflow() override;
    pos_type seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode) override;
    pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override;

public:
    filebuf() = default;
    ~filebuf() override;

    bool open(const wchar_t *filename, std::ios_base::openmode mode);
    bool open(const char *filename, std::ios_base::openmode mode);

    bool is_open() const noexcept { return mFile != INVALID_HANDLE_VALUE; }
};

// Inherit from std::istream to use our custom streambuf
class ifstream final : public std::istream {
    filebuf mStreamBuf;

public:
    ifstream(const wchar_t *filename, std::ios_base::openmode mode = std::ios_base::in);
    ifstream(const std::wstring &filename, std::ios_base::openmode mode = std::ios_base::in)
      : ifstream(filename.c_str(), mode) { }
    ifstream(const char *filename, std::ios_base::openmode mode = std::ios_base::in);
    ifstream(const std::string &filename, std::ios_base::openmode mode = std::ios_base::in)
      : ifstream(filename.c_str(), mode) { }
    ~ifstream() override;

    bool is_open() const noexcept { return mStreamBuf.is_open(); }
};

} // namespace al

#define HAVE_DYNLOAD 1

#else /* _WIN32 */

#include <fstream>

namespace al {

using filebuf = std::filebuf;
using ifstream = std::ifstream;

} // namespace al

#if defined(HAVE_DLFCN_H)
#define HAVE_DYNLOAD 1
#endif

#endif /* _WIN32 */

#include <string>

struct PathNamePair { std::string path, fname; };
const PathNamePair &GetProcBinary(void);

#ifdef HAVE_DYNLOAD
void *LoadLib(const char *name);
void CloseLib(void *handle);
void *GetSymbol(void *handle, const char *name);
#endif

#endif /* __cplusplus */

#endif /* AL_COMPAT_H */
