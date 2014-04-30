#ifndef ALSTRING_H
#define ALSTRING_H

#include <string.h>

#include "vector.h"


typedef char al_string_char_type;
DECL_VECTOR(al_string_char_type)

typedef vector_al_string_char_type al_string;
typedef const_vector_al_string_char_type const_al_string;

#define AL_STRING_INIT(_x)   VECTOR_INIT(_x)
#define AL_STRING_DEINIT(_x) VECTOR_DEINIT(_x)

inline ALsizei al_string_length(const_al_string str)
{ return VECTOR_SIZE(str); }

inline ALboolean al_string_empty(const_al_string str)
{ return al_string_length(str) == 0; }

inline const al_string_char_type *al_string_get_cstr(const_al_string str)
{ return str ? &VECTOR_FRONT(str) : ""; }

void al_string_clear(al_string *str);

int al_string_cmp(const_al_string str1, const_al_string str2);
int al_string_cmp_cstr(const_al_string str1, const al_string_char_type *str2);

void al_string_copy(al_string *str, const_al_string from);
void al_string_copy_cstr(al_string *str, const al_string_char_type *from);

void al_string_append_char(al_string *str, const al_string_char_type c);
void al_string_append_cstr(al_string *str, const al_string_char_type *from);
void al_string_append_range(al_string *str, const al_string_char_type *from, const al_string_char_type *to);

#ifdef _WIN32
#include <wchar.h>
/* Windows-only methods to deal with WideChar strings. */
void al_string_copy_wcstr(al_string *str, const wchar_t *from);
#endif


DECL_VECTOR(al_string)

#endif /* ALSTRING_H */
