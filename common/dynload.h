#ifndef AL_DYNLOAD_H
#define AL_DYNLOAD_H

#include "config.h"

#if defined(_WIN32) || defined(HAVE_DLFCN_H)

#include <string>

#include "expected.hpp"
#include "gsl/gsl"

#define HAVE_DYNLOAD 1

#include "dlopennote.h"

[[nodiscard]]
auto LoadLib(gsl::czstring name) -> al::expected<void*, std::string>;
void CloseLib(void *handle);
[[nodiscard]]
auto GetSymbol(void *handle, gsl::czstring name) -> al::expected<void*, std::string>;

#else

#define HAVE_DYNLOAD 0

#endif

#endif /* AL_DYNLOAD_H */
