#ifndef AL_DYNLOAD_H
#define AL_DYNLOAD_H

#include "config.h"

#if HAVE_DYNLOAD

#if defined(_WIN32) || defined(HAVE_DLFCN_H)

#include <string>

#include "expected.hpp"
#include "gsl/gsl"

#include "dlopennote.h"

[[nodiscard]]
auto LoadLib(gsl::czstring name) -> al::expected<void*, std::string>;
void CloseLib(void *handle);
[[nodiscard]]
auto GetSymbol(void *handle, gsl::czstring name) -> al::expected<void*, std::string>;

#endif

#endif

#endif /* AL_DYNLOAD_H */
