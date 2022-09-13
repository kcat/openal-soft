#ifndef AL_FSTREAM_H
#define AL_FSTREAM_H

#ifdef _WIN32

#include <string>
#include <fstream>


namespace al {

// Inherit from std::ifstream to accept UTF-8 filenames
class ifstream final : public std::ifstream {
public:
    explicit ifstream(const char *filename, std::ios_base::openmode mode=std::ios_base::in);
    explicit ifstream(const std::string &filename, std::ios_base::openmode mode=std::ios_base::in)
        : ifstream{filename.c_str(), mode} { }

    explicit ifstream(const wchar_t *filename, std::ios_base::openmode mode=std::ios_base::in)
        : std::ifstream{filename, mode} { }
    explicit ifstream(const std::wstring &filename, std::ios_base::openmode mode=std::ios_base::in)
        : ifstream{filename.c_str(), mode} { }

    void open(const char *filename, std::ios_base::openmode mode=std::ios_base::in);
    void open(const std::string &filename, std::ios_base::openmode mode=std::ios_base::in)
    { open(filename.c_str(), mode); }

    ~ifstream() override;
};

} // namespace al

#else /* _WIN32 */

#include <fstream>

namespace al {

using ifstream = std::ifstream;

} // namespace al

#endif /* _WIN32 */

#endif /* AL_FSTREAM_H */
