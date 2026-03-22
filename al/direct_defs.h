#ifndef AL_DIRECT_DEFS_H
#define AL_DIRECT_DEFS_H

#include "alc/context.h"
#include "gsl/gsl"
#include "opthelpers.h"


namespace al {

inline auto verify_context(ALCcontext *context) -> gsl::not_null<al::Context*>
{
    /* TODO: A debug/non-optimized build should essentially do
     * al::get_not_null(VerifyContext(context)) to ensure the ALCcontext handle
     * is valid, not just non-null.
     */
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast) */
    return gsl::make_not_null(static_cast<al::Context*>(context));
}

}

namespace detail_ {

template<typename T>
constexpr auto DefaultVal() noexcept -> T
{
    if constexpr(std::same_as<T, void>)
        return;
    else
        return T{};
}

} // namespace detail_

#if !defined(_WIN32) && !defined(AL_LIBTYPE_STATIC) && HAS_ATTRIBUTE(gnu::alias)
#define DefineFuncAlias(X, R, ...) extern "C" DECL_HIDDEN [[gnu::alias(#X)]]  \
    auto AL_APIENTRY X##_(__VA_ARGS__) noexcept -> R;

#else

#define DefineFuncAlias(...)
#endif

#define DECL_FUNC1(ATTR, R, Name, T1,n1)                                      \
ATTR auto AL_APIENTRY Name(T1 n1) noexcept -> R                               \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1);                       \
}                                                                             \
DefineFuncAlias(Name,R,T1)                                                    \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1) noexcept\
    -> R                                                                      \
{                                                                             \
    return Name(al::verify_context(context), n1);                             \
}                                                                             \
DefineFuncAlias(Name##Direct,R,ALCcontext*,T1)

#define DECL_FUNC2(ATTR, R, Name, T1,n1, T2,n2)                               \
ATTR auto AL_APIENTRY Name(T1 n1, T2 n2) noexcept -> R                        \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1, n2);                   \
}                                                                             \
DefineFuncAlias(Name,R,T1,T2)                                                 \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1, T2 n2)  \
    noexcept -> R                                                             \
{                                                                             \
    return Name(al::verify_context(context), n1, n2);                         \
}                                                                             \
DefineFuncAlias(Name##Direct,R,ALCcontext*,T1,T2)

#define DECL_FUNC3(ATTR, R, Name, T1,n1, T2,n2, T3,n3)                        \
ATTR auto AL_APIENTRY Name(T1 n1, T2 n2, T3 n3) noexcept -> R                 \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1, n2, n3);               \
}                                                                             \
DefineFuncAlias(Name,R,T1,T2,T3)                                              \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1, T2 n2,  \
    T3 n3) noexcept -> R                                                      \
{                                                                             \
    return Name(al::verify_context(context), n1, n2, n3);                     \
}                                                                             \
DefineFuncAlias(Name##Direct,R,ALCcontext*,T1,T2,T3)

#define DECL_FUNC4(ATTR, R, Name, T1,n1, T2,n2, T3,n3, T4,n4)                 \
ATTR auto AL_APIENTRY Name(T1 n1, T2 n2, T3 n3, T4 n4) noexcept -> R          \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1, n2, n3, n4);           \
}                                                                             \
DefineFuncAlias(Name,R,T1,T2,T3,T4)                                           \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1, T2 n2,  \
    T3 n3, T4 n4) noexcept -> R                                               \
{                                                                             \
    return Name(al::verify_context(context), n1, n2, n3, n4);                 \
}                                                                             \
DefineFuncAlias(Name##Direct,R,ALCcontext*,T1,T2,T3,T4)

#define DECL_FUNC5(ATTR, R, Name, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5)          \
ATTR auto AL_APIENTRY Name(T1 n1, T2 n2, T3 n3, T4 n4, T5 n5) noexcept -> R   \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1, n2, n3, n4, n5);       \
}                                                                             \
DefineFuncAlias(Name,R,T1,T2,T3,T4,T5)                                        \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1, T2 n2,  \
    T3 n3, T4 n4, T5 n5) noexcept -> R                                        \
{                                                                             \
    return Name(al::verify_context(context), n1, n2, n3, n4, n5);             \
}                                                                             \
DefineFuncAlias(Name##Direct,R,ALCcontext*,T1,T2,T3,T4,T5)

#define DECL_FUNC_SELECTOR(ATTR, R, Name, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5, NAME, ...) NAME
#define DECL_FUNC(...) DECL_FUNC_SELECTOR(__VA_ARGS__,                        \
    DECL_FUNC5, Misdefined, DECL_FUNC4, Misdefined, DECL_FUNC3, Misdefined,   \
    DECL_FUNC2, Misdefined, DECL_FUNC1, Misdefined, DECL_FUNC0)(__VA_ARGS__)

#define DECL_FUNCEXT0(ATTR, R, Name,Ext)                                      \
ATTR auto AL_APIENTRY Name##Ext() noexcept -> R                               \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()));                      \
}                                                                             \
DefineFuncAlias(Name##Ext,R)                                                  \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context) noexcept  \
    -> R                                                                      \
{                                                                             \
    return Name##Ext(al::verify_context(context));                            \
}                                                                             \
DefineFuncAlias(Name##Direct##Ext,R,ALCcontext*)

#define DECL_FUNCEXT1(ATTR, R, Name,Ext, T1,n1)                               \
ATTR auto AL_APIENTRY Name##Ext(T1 n1) noexcept -> R                          \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1);                  \
}                                                                             \
DefineFuncAlias(Name##Ext,R,T1)                                               \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1)    \
    noexcept -> R                                                             \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1);                        \
}                                                                             \
DefineFuncAlias(Name##Direct##Ext,R,ALCcontext*,T1)

#define DECL_FUNCEXT2(ATTR, R, Name,Ext, T1,n1, T2,n2)                        \
ATTR auto AL_APIENTRY Name##Ext(T1 n1, T2 n2) noexcept -> R                   \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2);              \
}                                                                             \
DefineFuncAlias(Name##Ext,R,T1,T2)                                            \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2) noexcept -> R                                                      \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2);                    \
}                                                                             \
DefineFuncAlias(Name##Direct##Ext,R,ALCcontext*,T1,T2)

#define DECL_FUNCEXT3(ATTR, R, Name,Ext, T1,n1, T2,n2, T3,n3)                 \
ATTR auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3) noexcept -> R            \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3);          \
}                                                                             \
DefineFuncAlias(Name##Ext,R,T1,T2,T3)                                         \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3) noexcept -> R                                               \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3);                \
}                                                                             \
DefineFuncAlias(Name##Direct##Ext,R,ALCcontext*,T1,T2,T3)

#define DECL_FUNCEXT4(ATTR, R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4)          \
ATTR auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3, T4 n4) noexcept -> R     \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3, n4);      \
}                                                                             \
DefineFuncAlias(Name##Ext,R,T1,T2,T3,T4)                                      \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3, T4 n4) noexcept -> R                                        \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3, n4);            \
}                                                                             \
DefineFuncAlias(Name##Direct##Ext,R,ALCcontext*,T1,T2,T3,T4)

#define DECL_FUNCEXT5(ATTR, R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5)   \
ATTR auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3, T4 n4, T5 n5) noexcept   \
    -> R                                                                      \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3, n4, n5);  \
}                                                                             \
DefineFuncAlias(Name##Ext,R,T1,T2,T3,T4,T5)                                   \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3, T4 n4, T5 n5) noexcept -> R                                 \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3, n4, n5);        \
}                                                                             \
DefineFuncAlias(Name##Direct##Ext,R,ALCcontext*,T1,T2,T3,T4,T5)

#define DECL_FUNCEXT6(ATTR, R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5,   \
    T6,n6) \
ATTR auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3, T4 n4, T5 n5, T6 n6)     \
    noexcept -> R                                                             \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3, n4, n5,   \
        n6);                                                                  \
}                                                                             \
DefineFuncAlias(Name##Ext,R,T1,T2,T3,T4,T5,T6)                                \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3, T4 n4, T5 n5, T6 n6) noexcept -> R                          \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3, n4, n5, n6);    \
}                                                                             \
DefineFuncAlias(Name##Direct##Ext,R,ALCcontext*,T1,T2,T3,T4,T5,T6)

#define DECL_FUNCEXT8(ATTR, R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5,   \
    T6,n6, T7,n7, T8,n8)                                                      \
ATTR auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3, T4 n4, T5 n5, T6 n6,     \
    T7 n7, T8 n8) noexcept -> R                                               \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3, n4, n5,   \
        n6, n7, n8);                                                          \
}                                                                             \
DefineFuncAlias(Name##Ext,R,T1,T2,T3,T4,T5,T6,T7,T8)                          \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3, T4 n4, T5 n5, T6 n6, T7 n7, T8 n8) noexcept -> R            \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3, n4, n5, n6, n7, \
        n8);                                                                  \
}                                                                             \
DefineFuncAlias(Name##Direct##Ext,R,ALCcontext*,T1,T2,T3,T4,T5,T6,T7,T8)

#define DECL_FUNCEXT_SELECTOR(ATTR, R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4,  \
    T5,n5, T6,n6, T7,n7, T8,n8, NAME, ...) NAME
#define DECL_FUNCEXT(...) DECL_FUNCEXT_SELECTOR(__VA_ARGS__,                  \
    DECL_FUNCEXT8, Misdefined, DECL_FUNCEXT7, Misdefined,                     \
    DECL_FUNCEXT6, Misdefined, DECL_FUNCEXT5, Misdefined,                     \
    DECL_FUNCEXT4, Misdefined, DECL_FUNCEXT3, Misdefined,                     \
    DECL_FUNCEXT2, Misdefined, DECL_FUNCEXT1, Misdefined,                     \
    DECL_FUNCEXT0)(__VA_ARGS__)

#endif /* AL_DIRECT_DEFS_H */
