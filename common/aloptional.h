#ifndef AL_OPTIONAL_H
#define AL_OPTIONAL_H

#include <optional>

namespace al {

constexpr auto nullopt = std::nullopt;

template<typename T>
using optional = std::optional<T>;

using std::make_optional;

} // namespace al

#endif /* AL_OPTIONAL_H */
