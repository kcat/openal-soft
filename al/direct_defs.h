#ifndef AL_DIRECT_DEFS_H
#define AL_DIRECT_DEFS_H

#define DECL_FUNC(R, Name)                                                    \
R AL_API Name(void) START_API_FUNC                                            \
{                                                                             \
    return Name##Direct(GetContextRef().get());                               \
} END_API_FUNC

#define DECL_FUNC1(R, Name, T1)                                               \
R AL_API Name(T1 a) START_API_FUNC                                            \
{                                                                             \
    return Name##Direct(GetContextRef().get(), a);                            \
} END_API_FUNC

#define DECL_FUNC2(R, Name, T1, T2)                                           \
R AL_API Name(T1 a, T2 b) START_API_FUNC                                      \
{                                                                             \
    return Name##Direct(GetContextRef().get(), a, b);                         \
} END_API_FUNC


#define DECL_FUNCEXT(R, Name,Ext)                                             \
R AL_API Name##Ext(void) START_API_FUNC                                       \
{                                                                             \
    return Name##Direct##Ext(GetContextRef().get());                          \
} END_API_FUNC

#define DECL_FUNCEXT2(R, Name,Ext, T1, T2)                                    \
R AL_API Name##Ext(T1 a, T2 b) START_API_FUNC                                 \
{                                                                             \
    return Name##Direct##Ext(GetContextRef().get(), a, b);                    \
} END_API_FUNC

#endif /* AL_DIRECT_DEFS_H */
