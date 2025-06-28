
#include "config.h"

#include "router.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "AL/alc.h"
#include "AL/al.h"

#include "alstring.h"
#include "gsl/gsl"
#include "opthelpers.h"
#include "strutils.hpp"

#include "version.h"


eLogLevel LogLevel{eLogLevel::Error};
gsl::owner<std::FILE*> LogFile;

namespace {

/* C++23 has this... */
struct contains_fn_ {
    template<std::input_iterator I, std::sentinel_for<I> S, typename T,
        typename Proj=std::identity>
        requires std::indirect_binary_predicate<std::ranges::equal_to, std::projected<I, Proj>,
            const T*>
    constexpr auto operator()(I first, S last, const T& value, Proj proj={}) const -> bool
    {
        return std::ranges::find(std::move(first), last, value, proj) != last;
    }

    template<std::ranges::input_range R, typename T, typename Proj=std::identity>
        requires std::indirect_binary_predicate<std::ranges::equal_to,
            std::projected<std::ranges::iterator_t<R>, Proj>, const T*>
    constexpr auto operator()(R&& r, const T& value, Proj proj={}) const -> bool
    {
        const auto last = std::ranges::end(r);
        return std::ranges::find(std::ranges::begin(r), last, value, proj) != last;
    }
};
inline constexpr auto contains =  contains_fn_{};


auto gAcceptList = std::vector<std::wstring>{};
auto gRejectList = std::vector<std::wstring>{};


void AddModule(HMODULE module, const std::wstring_view name)
{
    if(contains(DriverList, module, &DriverIface::Module))
    {
        TRACE("Skipping already-loaded module {}", decltype(std::declval<void*>()){module});
        FreeLibrary(module);
        return;
    }
    if(contains(DriverList, name, &DriverIface::Name))
    {
        TRACE("Skipping similarly-named module {}", wstr_to_utf8(name));
        FreeLibrary(module);
        return;
    }

    if(!gAcceptList.empty())
    {
        if(std::ranges::none_of(gAcceptList, [name](const std::wstring_view accept)
            { return al::case_compare(name, accept) == 0; }))
        {
            TRACE("{} not found in ALROUTER_ACCEPT, skipping", wstr_to_utf8(name));
            FreeLibrary(module);
            return;
        }
    }
    if(std::ranges::any_of(gRejectList, [name](const std::wstring_view reject)
        { return al::case_compare(name, reject) == 0; }))
    {
        TRACE("{} found in ALROUTER_REJECT, skipping", wstr_to_utf8(name));
        FreeLibrary(module);
        return;
    }

    auto &newdrv = *DriverList.emplace_back(std::make_unique<DriverIface>(name, module));

    /* Load required functions. */
    auto loadok = true;
    auto do_load = [module,name](auto &func, const char *fname) -> bool
    {
        using func_t = std::remove_reference_t<decltype(func)>;
        auto ptr = GetProcAddress(module, fname);
        if(!ptr)
        {
            ERR("Failed to find entry point for {} in {}", fname, wstr_to_utf8(name));
            return false;
        }

        func = std::bit_cast<func_t>(ptr);
        return true;
    };
#define LOAD_PROC(x) loadok &= do_load(newdrv.x, #x)
    LOAD_PROC(alcCreateContext);
    LOAD_PROC(alcMakeContextCurrent);
    LOAD_PROC(alcProcessContext);
    LOAD_PROC(alcSuspendContext);
    LOAD_PROC(alcDestroyContext);
    LOAD_PROC(alcGetCurrentContext);
    LOAD_PROC(alcGetContextsDevice);
    LOAD_PROC(alcOpenDevice);
    LOAD_PROC(alcCloseDevice);
    LOAD_PROC(alcGetError);
    LOAD_PROC(alcIsExtensionPresent);
    LOAD_PROC(alcGetProcAddress);
    LOAD_PROC(alcGetEnumValue);
    LOAD_PROC(alcGetString);
    LOAD_PROC(alcGetIntegerv);
    LOAD_PROC(alcCaptureOpenDevice);
    LOAD_PROC(alcCaptureCloseDevice);
    LOAD_PROC(alcCaptureStart);
    LOAD_PROC(alcCaptureStop);
    LOAD_PROC(alcCaptureSamples);

    LOAD_PROC(alEnable);
    LOAD_PROC(alDisable);
    LOAD_PROC(alIsEnabled);
    LOAD_PROC(alGetString);
    LOAD_PROC(alGetBooleanv);
    LOAD_PROC(alGetIntegerv);
    LOAD_PROC(alGetFloatv);
    LOAD_PROC(alGetDoublev);
    LOAD_PROC(alGetBoolean);
    LOAD_PROC(alGetInteger);
    LOAD_PROC(alGetFloat);
    LOAD_PROC(alGetDouble);
    LOAD_PROC(alGetError);
    LOAD_PROC(alIsExtensionPresent);
    LOAD_PROC(alGetProcAddress);
    LOAD_PROC(alGetEnumValue);
    LOAD_PROC(alListenerf);
    LOAD_PROC(alListener3f);
    LOAD_PROC(alListenerfv);
    LOAD_PROC(alListeneri);
    LOAD_PROC(alListener3i);
    LOAD_PROC(alListeneriv);
    LOAD_PROC(alGetListenerf);
    LOAD_PROC(alGetListener3f);
    LOAD_PROC(alGetListenerfv);
    LOAD_PROC(alGetListeneri);
    LOAD_PROC(alGetListener3i);
    LOAD_PROC(alGetListeneriv);
    LOAD_PROC(alGenSources);
    LOAD_PROC(alDeleteSources);
    LOAD_PROC(alIsSource);
    LOAD_PROC(alSourcef);
    LOAD_PROC(alSource3f);
    LOAD_PROC(alSourcefv);
    LOAD_PROC(alSourcei);
    LOAD_PROC(alSource3i);
    LOAD_PROC(alSourceiv);
    LOAD_PROC(alGetSourcef);
    LOAD_PROC(alGetSource3f);
    LOAD_PROC(alGetSourcefv);
    LOAD_PROC(alGetSourcei);
    LOAD_PROC(alGetSource3i);
    LOAD_PROC(alGetSourceiv);
    LOAD_PROC(alSourcePlayv);
    LOAD_PROC(alSourceStopv);
    LOAD_PROC(alSourceRewindv);
    LOAD_PROC(alSourcePausev);
    LOAD_PROC(alSourcePlay);
    LOAD_PROC(alSourceStop);
    LOAD_PROC(alSourceRewind);
    LOAD_PROC(alSourcePause);
    LOAD_PROC(alSourceQueueBuffers);
    LOAD_PROC(alSourceUnqueueBuffers);
    LOAD_PROC(alGenBuffers);
    LOAD_PROC(alDeleteBuffers);
    LOAD_PROC(alIsBuffer);
    LOAD_PROC(alBufferData);
    LOAD_PROC(alDopplerFactor);
    LOAD_PROC(alDopplerVelocity);
    LOAD_PROC(alSpeedOfSound);
    LOAD_PROC(alDistanceModel);
#undef LOAD_PROC
    if(loadok)
    {
        auto alc_ver = std::array{0, 0};
        newdrv.alcGetIntegerv(nullptr, ALC_MAJOR_VERSION, 1, &alc_ver[0]);
        newdrv.alcGetIntegerv(nullptr, ALC_MINOR_VERSION, 1, &alc_ver[1]);
        if(newdrv.alcGetError(nullptr) == ALC_NO_ERROR)
            newdrv.ALCVer = MakeALCVer(alc_ver[0], alc_ver[1]);
        else
        {
            WARN("Failed to query ALC version for {}, assuming 1.0", wstr_to_utf8(name));
            newdrv.ALCVer = MakeALCVer(1, 0);
        }

        auto do_load2 = [module,name](auto &func, const char *fname) -> void
        {
            using func_t = std::remove_reference_t<decltype(func)>;
            auto ptr = GetProcAddress(module, fname);
            if(!ptr)
                WARN("Failed to find optional entry point for {} in {}", fname,
                    wstr_to_utf8(name));
            else
                func = std::bit_cast<func_t>(ptr);
        };
#define LOAD_PROC(x) do_load2(newdrv.x, #x)
        LOAD_PROC(alBufferf);
        LOAD_PROC(alBuffer3f);
        LOAD_PROC(alBufferfv);
        LOAD_PROC(alBufferi);
        LOAD_PROC(alBuffer3i);
        LOAD_PROC(alBufferiv);
        LOAD_PROC(alGetBufferf);
        LOAD_PROC(alGetBuffer3f);
        LOAD_PROC(alGetBufferfv);
        LOAD_PROC(alGetBufferi);
        LOAD_PROC(alGetBuffer3i);
        LOAD_PROC(alGetBufferiv);
#undef LOAD_PROC

        auto do_load3 = [name,&newdrv](auto &func, const char *fname) -> bool
        {
            using func_t = std::remove_reference_t<decltype(func)>;
            auto ptr = newdrv.alcGetProcAddress(nullptr, fname);
            if(!ptr)
            {
                ERR("Failed to find entry point for {} in {}", fname, wstr_to_utf8(name));
                return false;
            }

            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
            func = reinterpret_cast<func_t>(ptr);
            return true;
        };
#define LOAD_PROC(x) loadok &= do_load3(newdrv.x, #x)
        if(newdrv.alcIsExtensionPresent(nullptr, "ALC_EXT_thread_local_context"))
        {
            LOAD_PROC(alcSetThreadContext);
            LOAD_PROC(alcGetThreadContext);
        }
#undef LOAD_PROC
    }

    if(!loadok)
    {
        DriverList.pop_back();
        return;
    }
    TRACE("Loaded module {}, {}, ALC {}.{}", decltype(std::declval<void*>()){module},
        wstr_to_utf8(name), newdrv.ALCVer>>8, newdrv.ALCVer&255);
}

void SearchDrivers(const std::wstring_view path)
{
    TRACE("Searching for drivers in {}...", wstr_to_utf8(path));
    auto srchPath = std::wstring{path};
    srchPath += L"\\*oal.dll";

    auto fdata = WIN32_FIND_DATAW{};
    auto srchHdl = FindFirstFileW(srchPath.c_str(), &fdata);
    if(srchHdl == INVALID_HANDLE_VALUE) return;

    do {
        srchPath = path;
        srchPath += L"\\";
        srchPath += std::data(fdata.cFileName);
        TRACE("Found {}", wstr_to_utf8(srchPath));

        auto mod = LoadLibraryW(srchPath.c_str());
        if(!mod)
            WARN("Could not load {}", wstr_to_utf8(srchPath));
        else
            AddModule(mod, std::data(fdata.cFileName));
    } while(FindNextFileW(srchHdl, &fdata));
    FindClose(srchHdl);
}

auto GetLoadedModuleDirectory(const WCHAR *name, std::wstring *moddir) -> bool
{
    auto module = HMODULE{nullptr};

    if(name)
    {
        module = GetModuleHandleW(name);
        if(!module) return false;
    }

    moddir->assign(256, '\0');
    auto res = GetModuleFileNameW(module, moddir->data(), gsl::narrow_cast<DWORD>(moddir->size()));
    if(res >= moddir->size())
    {
        do {
            moddir->append(256, '\0');
            res = GetModuleFileNameW(module, moddir->data(),
                gsl::narrow_cast<DWORD>(moddir->size()));
        } while(res >= moddir->size());
    }
    moddir->resize(res);

    const auto sep0 = moddir->rfind('/');
    const auto sep1 = moddir->rfind('\\');
    if(sep0 < moddir->size() && sep1 < moddir->size())
        moddir->resize(std::max(sep0, sep1));
    else if(sep0 < moddir->size())
        moddir->resize(sep0);
    else if(sep1 < moddir->size())
        moddir->resize(sep1);
    else
        moddir->resize(0);

    return !moddir->empty();
}

} // namespace

void LoadDriverList()
{
    TRACE("Initializing router v0.1-{} {}", ALSOFT_GIT_COMMIT_HASH, ALSOFT_GIT_BRANCH);

    if(auto list = al::getenv(L"ALROUTER_ACCEPT"))
    {
        std::ranges::for_each(*list | std::views::split(','), [](auto&& subrange)
        {
            if(!subrange.empty())
                gAcceptList.emplace_back(std::wstring_view{subrange.begin(), subrange.end()});
        });
    }
    if(auto list = al::getenv(L"ALROUTER_REJECT"))
    {
        std::ranges::for_each(*list | std::views::split(','), [](auto&& subrange)
        {
            if(!subrange.empty())
                gRejectList.emplace_back(std::wstring_view{subrange.begin(), subrange.end()});
        });
    }

    auto dll_path = std::wstring{};
    if(GetLoadedModuleDirectory(L"OpenAL32.dll", &dll_path))
        TRACE("Got DLL path {}", wstr_to_utf8(dll_path));

    auto cwd_path = std::wstring{};
    if(auto curpath = std::filesystem::current_path(); !curpath.empty())
    {
        if constexpr(std::same_as<decltype(curpath)::string_type,std::wstring>)
            cwd_path = curpath.wstring();
        else
            cwd_path = utf8_to_wstr(al::u8_as_char(curpath.u8string()));
    }
    if(!cwd_path.empty() && (cwd_path.back() == '\\' || cwd_path.back() == '/'))
        cwd_path.pop_back();
    if(!cwd_path.empty())
        TRACE("Got current working directory {}", wstr_to_utf8(cwd_path));

    auto proc_path = std::wstring{};
    if(GetLoadedModuleDirectory(nullptr, &proc_path))
        TRACE("Got proc path {}", wstr_to_utf8(proc_path));

    auto sys_path = std::wstring{};
    if(auto pathlen = GetSystemDirectoryW(nullptr, 0))
    {
        do {
            sys_path.resize(pathlen);
            pathlen = GetSystemDirectoryW(sys_path.data(), pathlen);
        } while(pathlen >= sys_path.size());
        sys_path.resize(pathlen);
    }
    if(!sys_path.empty() && (sys_path.back() == '\\' || sys_path.back() == '/'))
        sys_path.pop_back();
    if(!sys_path.empty())
        TRACE("Got system path {}", wstr_to_utf8(sys_path));

    /* Don't search the DLL's path if it is the same as the current working
     * directory, app's path, or system path (don't want to do duplicate
     * searches, or increase the priority of the app or system path).
     */
    if(!dll_path.empty() && (cwd_path.empty() || dll_path != cwd_path)
        && (proc_path.empty() || dll_path != proc_path)
        && (sys_path.empty() || dll_path != sys_path))
        SearchDrivers(dll_path);
    if(!cwd_path.empty() && (proc_path.empty() || cwd_path != proc_path)
        && (sys_path.empty() || cwd_path != sys_path))
        SearchDrivers(cwd_path);
    if(!proc_path.empty() && (sys_path.empty() || proc_path != sys_path))
        SearchDrivers(proc_path);
    if(!sys_path.empty())
        SearchDrivers(sys_path);

    /* Sort drivers that can enumerate device names to the front. */
    std::ranges::stable_partition(DriverList, [](DriverIfacePtr &drv)
    {
        return drv->ALCVer >= MakeALCVer(1, 1)
            || drv->alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")
            || drv->alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT");
    });

    /* HACK: rapture3d_oal.dll isn't likely to work if it's one distributed for
     * specific games licensed to use it. It will enumerate a Rapture3D device
     * but fail to open. This isn't much of a problem, the device just won't
     * work for users not allowed to use it. But if it's the first in the list
     * where it gets used for the default device, the default device will fail
     * to open. Move it down so it's not used for the default device.
     */
    if(DriverList.size() > 1
        && al::case_compare(DriverList.front()->Name, L"rapture3d_oal.dll") == 0)
        std::swap(*DriverList.begin(), *(DriverList.begin()+1));
}

/* NOLINTNEXTLINE(misc-use-internal-linkage) Needs external linkage for Windows. */
auto APIENTRY DllMain(HINSTANCE, DWORD reason, void*) -> BOOL
{
    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        if(auto logfname = al::getenv(L"ALROUTER_LOGFILE"))
        {
            gsl::owner<std::FILE*> f{_wfopen(logfname->c_str(), L"w")};
            if(f == nullptr)
                ERR("Could not open log file: {}", wstr_to_utf8(*logfname));
            else
                LogFile = f;
        }
        if(auto loglev = al::getenv("ALROUTER_LOGLEVEL"))
        {
            char *end{};
            auto l = strtol(loglev->c_str(), &end, 0);
            if(!end || *end != '\0')
                ERR("Invalid log level value: {}", *loglev);
            else if(l < al::to_underlying(eLogLevel::None)
                || l > al::to_underlying(eLogLevel::Trace))
                ERR("Log level out of range: {}", *loglev);
            else
                LogLevel = static_cast<eLogLevel>(l);
        }
        break;

    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        DriverList.clear();

        if(LogFile)
            fclose(LogFile);
        LogFile = nullptr;

        break;
    }
    return TRUE;
}
