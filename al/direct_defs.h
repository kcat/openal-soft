#ifndef AL_DIRECT_DEFS_H
#define AL_DIRECT_DEFS_H

namespace detail_ {

template<typename T>
constexpr T DefaultVal() noexcept { return T{}; }

template<>
constexpr void DefaultVal() noexcept { }

} // namespace detail_

#define DECL_FUNC(R, Name)                                                    \
R AL_APIENTRY Name(void) START_API_FUNC                                       \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct(context.get());                                       \
} END_API_FUNC

#define DECL_FUNC1(R, Name, T1)                                               \
R AL_APIENTRY Name(T1 a) START_API_FUNC                                       \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct(context.get(), a);                                    \
} END_API_FUNC

#define DECL_FUNC2(R, Name, T1, T2)                                           \
R AL_APIENTRY Name(T1 a, T2 b) START_API_FUNC                                 \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct(context.get(), a, b);                                 \
} END_API_FUNC

#define DECL_FUNC3(R, Name, T1, T2, T3)                                       \
R AL_APIENTRY Name(T1 a, T2 b, T3 c) START_API_FUNC                           \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct(context.get(), a, b, c);                              \
} END_API_FUNC

#define DECL_FUNC4(R, Name, T1, T2, T3, T4)                                   \
R AL_APIENTRY Name(T1 a, T2 b, T3 c, T4 d) START_API_FUNC                     \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct(context.get(), a, b, c, d);                           \
} END_API_FUNC

#define DECL_FUNC5(R, Name, T1, T2, T3, T4, T5)                               \
R AL_APIENTRY Name(T1 a, T2 b, T3 c, T4 d, T5 e) START_API_FUNC               \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct(context.get(), a, b, c, d, e);                        \
} END_API_FUNC


#define DECL_FUNCEXT(R, Name,Ext)                                             \
R AL_APIENTRY Name##Ext(void) START_API_FUNC                                  \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct##Ext(context.get());                                  \
} END_API_FUNC

#define DECL_FUNCEXT1(R, Name,Ext, T1)                                        \
R AL_APIENTRY Name##Ext(T1 a) START_API_FUNC                                  \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct##Ext(context.get(), a);                               \
} END_API_FUNC

#define DECL_FUNCEXT2(R, Name,Ext, T1, T2)                                    \
R AL_APIENTRY Name##Ext(T1 a, T2 b) START_API_FUNC                            \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct##Ext(context.get(), a, b);                            \
} END_API_FUNC

#define DECL_FUNCEXT3(R, Name,Ext, T1, T2, T3)                                \
R AL_APIENTRY Name##Ext(T1 a, T2 b, T3 c) START_API_FUNC                      \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct##Ext(context.get(), a, b, c);                         \
} END_API_FUNC

#define DECL_FUNCEXT4(R, Name,Ext, T1, T2, T3, T4)                            \
R AL_APIENTRY Name##Ext(T1 a, T2 b, T3 c, T4 d) START_API_FUNC                \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct##Ext(context.get(), a, b, c, d);                      \
} END_API_FUNC

#define DECL_FUNCEXT5(R, Name,Ext, T1, T2, T3, T4, T5)                        \
R AL_APIENTRY Name##Ext(T1 a, T2 b, T3 c, T4 d, T5 e) START_API_FUNC          \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct##Ext(context.get(), a, b, c, d, e);                   \
} END_API_FUNC

#define DECL_FUNCEXT6(R, Name,Ext, T1, T2, T3, T4, T5, T6)                    \
R AL_APIENTRY Name##Ext(T1 a, T2 b, T3 c, T4 d, T5 e, T6 f) START_API_FUNC    \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return detail_::DefaultVal<R>();                    \
    return Name##Direct##Ext(context.get(), a, b, c, d, e, f);                \
} END_API_FUNC

#endif /* AL_DIRECT_DEFS_H */
