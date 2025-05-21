
#include "config.h"
#include "config_simd.h"

#include "cpu_caps.h"

#if defined(_WIN32) && (defined(_M_ARM) || defined(_M_ARM64))
#include <windows.h>
#ifndef PF_ARM_NEON_INSTRUCTIONS_AVAILABLE
#define PF_ARM_NEON_INSTRUCTIONS_AVAILABLE 19
#endif
#endif

#if defined(HAVE_CPUID_H)
#include <cpuid.h>
#elif defined(HAVE_INTRIN_H)
#include <intrin.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <ranges>
#include <string>


namespace {

#if defined(HAVE_GCC_GET_CPUID) \
    && (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
using reg_type = unsigned int;
inline auto get_cpuid(unsigned int f) -> std::array<reg_type,4>
{
    auto ret = std::array<reg_type,4>{};
    __get_cpuid(f, ret.data(), &ret[1], &ret[2], &ret[3]);
    return ret;
}
#define CAN_GET_CPUID

#elif defined(HAVE_CPUID_INTRINSIC) \
    && (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))

using reg_type = int;
inline auto get_cpuid(unsigned int f) -> std::array<reg_type,4>
{
    auto ret = std::array<reg_type,4>{};
    (__cpuid)(ret.data(), f);
    return ret;
}
#define CAN_GET_CPUID
#endif

} // namespace

auto GetCPUInfo() -> std::optional<CPUInfo>
{
    auto ret = CPUInfo{};

#ifdef CAN_GET_CPUID
    auto cpuregs = get_cpuid(0);
    if(cpuregs[0] == 0)
        return std::nullopt;

    const auto maxfunc = cpuregs[0];

    cpuregs = get_cpuid(0x80000000);
    const auto maxextfunc = cpuregs[0];

    const auto as_chars4 = std::array{
        std::bit_cast<std::array<char,4>>(cpuregs[1]),
        std::bit_cast<std::array<char,4>>(cpuregs[3]),
        std::bit_cast<std::array<char,4>>(cpuregs[2])};
    auto all_chars12 = as_chars4 | std::views::join;
    ret.mVendor.append(all_chars12.begin(), all_chars12.end());

    /* Remove null chars and duplicate/unnecessary spaces. */
    std::erase(ret.mVendor, '\0');
    auto iter_end = std::ranges::unique(ret.mVendor, [](const char c0, const char c1)
        { return std::isspace(c0) && std::isspace(c1); });
    ret.mVendor.erase(iter_end.begin(), iter_end.end());
    if(!ret.mVendor.empty() && std::isspace(ret.mVendor.back()))
        ret.mVendor.pop_back();
    if(!ret.mVendor.empty() && std::isspace(ret.mVendor.front()))
        ret.mVendor.erase(ret.mVendor.begin());

    if(maxextfunc >= 0x80000004)
    {
        const auto as_chars16 = std::array{
            std::bit_cast<std::array<char,16>>(get_cpuid(0x80000002)),
            std::bit_cast<std::array<char,16>>(get_cpuid(0x80000003)),
            std::bit_cast<std::array<char,16>>(get_cpuid(0x80000004))};
        const auto all_chars48 = as_chars16 | std::views::join;
        ret.mName.append(all_chars48.begin(), all_chars48.end());

        std::erase(ret.mName, '\0');
        iter_end = std::ranges::unique(ret.mName, [](const char c0, const char c1)
            { return std::isspace(c0) && std::isspace(c1); });
        ret.mName.erase(iter_end.begin(), iter_end.end());
        if(!ret.mName.empty() && std::isspace(ret.mName.back()))
            ret.mName.pop_back();
        if(!ret.mName.empty() && std::isspace(ret.mName.front()))
            ret.mName.erase(ret.mName.begin());
    }

    if(maxfunc >= 1)
    {
        cpuregs = get_cpuid(1);
        if((cpuregs[3]&(1<<25)))
            ret.mCaps |= CPU_CAP_SSE;
        if((ret.mCaps&CPU_CAP_SSE) && (cpuregs[3]&(1<<26)))
            ret.mCaps |= CPU_CAP_SSE2;
        if((ret.mCaps&CPU_CAP_SSE2) && (cpuregs[2]&(1<<0)))
            ret.mCaps |= CPU_CAP_SSE3;
        if((ret.mCaps&CPU_CAP_SSE3) && (cpuregs[2]&(1<<19)))
            ret.mCaps |= CPU_CAP_SSE4_1;
    }

#else

    /* Assume support for whatever's supported if we can't check for it */
#if HAVE_SSE4_1
#warning "Assuming SSE 4.1 run-time support!"
    ret.mCaps |= CPU_CAP_SSE | CPU_CAP_SSE2 | CPU_CAP_SSE3 | CPU_CAP_SSE4_1;
#elif HAVE_SSE3
#warning "Assuming SSE 3 run-time support!"
    ret.mCaps |= CPU_CAP_SSE | CPU_CAP_SSE2 | CPU_CAP_SSE3;
#elif HAVE_SSE2
#warning "Assuming SSE 2 run-time support!"
    ret.mCaps |= CPU_CAP_SSE | CPU_CAP_SSE2;
#elif HAVE_SSE
#warning "Assuming SSE run-time support!"
    ret.mCaps |= CPU_CAP_SSE;
#endif
#endif /* CAN_GET_CPUID */

#if HAVE_NEON
#ifdef __ARM_NEON
    ret.mCaps |= CPU_CAP_NEON;
#elif defined(_WIN32) && (defined(_M_ARM) || defined(_M_ARM64))
    if(IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE))
        ret.mCaps |= CPU_CAP_NEON;
#else
#warning "Assuming NEON run-time support!"
    ret.mCaps |= CPU_CAP_NEON;
#endif
#endif

    return ret;
}
