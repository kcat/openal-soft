#ifndef AL_DIRECT_DEFS_H
#define AL_DIRECT_DEFS_H

#include "alc/context.h"
#include "gsl/gsl"


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
constexpr T DefaultVal() noexcept { return T{}; }

template<>
constexpr void DefaultVal() noexcept { }

} // namespace detail_

#define DECL_FUNC(R, Name)                                                    \
auto AL_APIENTRY Name() noexcept -> R                                         \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()));                           \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context) noexcept -> R  \
{                                                                             \
    return Name(al::verify_context(context));                                 \
}

#define DECL_FUNC1(R, Name, T1,n1)                                            \
auto AL_APIENTRY Name(T1 n1) noexcept -> R                                    \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1);                       \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1) noexcept\
    -> R                                                                      \
{                                                                             \
    return Name(al::verify_context(context), n1);                             \
}

#define DECL_FUNC2(R, Name, T1,n1, T2,n2)                                     \
auto AL_APIENTRY Name(T1 n1, T2 n2) noexcept -> R                             \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1, n2);                   \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1, T2 n2)  \
    noexcept -> R                                                             \
{                                                                             \
    return Name(al::verify_context(context), n1, n2);                         \
}

#define DECL_FUNC3(R, Name, T1,n1, T2,n2, T3,n3)                              \
auto AL_APIENTRY Name(T1 n1, T2 n2, T3 n3) noexcept -> R                      \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1, n2, n3);               \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1, T2 n2,  \
    T3 n3) noexcept -> R                                                      \
{                                                                             \
    return Name(al::verify_context(context), n1, n2, n3);                     \
}

#define DECL_FUNC4(R, Name, T1,n1, T2,n2, T3,n3, T4,n4)                       \
auto AL_APIENTRY Name(T1 n1, T2 n2, T3 n3, T4 n4) noexcept -> R               \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1, n2, n3, n4);           \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1, T2 n2,  \
    T3 n3, T4 n4) noexcept -> R                                               \
{                                                                             \
    return Name(al::verify_context(context), n1, n2, n3, n4);                 \
}

#define DECL_FUNC5(R, Name, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5)                \
auto AL_APIENTRY Name(T1 n1, T2 n2, T3 n3, T4 n4, T5 n5) noexcept -> R        \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name(gsl::make_not_null(context.get()), n1, n2, n3, n4, n5);       \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct(ALCcontext *context, T1 n1, T2 n2,  \
    T3 n3, T4 n4, T5 n5) noexcept -> R                                        \
{                                                                             \
    return Name(al::verify_context(context), n1, n2, n3, n4, n5);             \
}


#define DECL_FUNCEXT(R, Name,Ext)                                             \
auto AL_APIENTRY Name##Ext() noexcept -> R                                    \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()));                      \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context) noexcept  \
    -> R                                                                      \
{                                                                             \
    return Name##Ext(al::verify_context(context));                            \
}

#define DECL_FUNCEXT1(R, Name,Ext, T1,n1)                                     \
auto AL_APIENTRY Name##Ext(T1 n1) noexcept -> R                               \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1);                  \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1)    \
    noexcept -> R                                                             \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1);                        \
}

#define DECL_FUNCEXT2(R, Name,Ext, T1,n1, T2,n2)                              \
auto AL_APIENTRY Name##Ext(T1 n1, T2 n2) noexcept -> R                        \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2);              \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2) noexcept -> R                                                      \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2);                    \
}

#define DECL_FUNCEXT3(R, Name,Ext, T1,n1, T2,n2, T3,n3)                       \
auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3) noexcept -> R                 \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3);          \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3) noexcept -> R                                               \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3);                \
}

#define DECL_FUNCEXT4(R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4)                \
auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3, T4 n4) noexcept -> R          \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3, n4);      \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3, T4 n4) noexcept -> R                                        \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3, n4);            \
}

#define DECL_FUNCEXT5(R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5)         \
auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3, T4 n4, T5 n5) noexcept -> R   \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3, n4, n5);  \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3, T4 n4, T5 n5) noexcept -> R                                 \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3, n4, n5);        \
}

#define DECL_FUNCEXT6(R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5, T6,n6)  \
auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3, T4 n4, T5 n5, T6 n6) noexcept \
    -> R                                                                      \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3, n4, n5,   \
        n6);                                                                  \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3, T4 n4, T5 n5, T6 n6) noexcept -> R                          \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3, n4, n5, n6);    \
}

#define DECL_FUNCEXT8(R, Name,Ext, T1,n1, T2,n2, T3,n3, T4,n4, T5,n5, T6,n6,  \
    T7,n7, T8,n8)                                                             \
auto AL_APIENTRY Name##Ext(T1 n1, T2 n2, T3 n3, T4 n4, T5 n5, T6 n6, T7 n7,   \
    T8 n8) noexcept -> R                                                      \
{                                                                             \
    auto const context = GetContextRef();                                     \
    if(!context) [[unlikely]] return detail_::DefaultVal<R>();                \
    return Name##Ext(gsl::make_not_null(context.get()), n1, n2, n3, n4, n5,   \
        n6, n7, n8);                                                          \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, T1 n1,    \
    T2 n2, T3 n3, T4 n4, T5 n5, T6 n6, T7 n7, T8 n8) noexcept -> R            \
{                                                                             \
    return Name##Ext(al::verify_context(context), n1, n2, n3, n4, n5, n6, n7, \
        n8);                                                                  \
}

#endif /* AL_DIRECT_DEFS_H */
