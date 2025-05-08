#ifndef CORE_BUFFERLINE_H
#define CORE_BUFFERLINE_H

#include <array>
#include <span>

/* Size for temporary storage of buffer data, in floats. Larger values need
 * more memory and are harder on cache, while smaller values may need more
 * iterations for mixing.
 */
inline constexpr auto BufferLineSize = size_t{1024};

using FloatBufferLine = std::array<float,BufferLineSize>;
using FloatBufferSpan = std::span<float,BufferLineSize>;
using FloatConstBufferSpan = std::span<const float,BufferLineSize>;

#endif /* CORE_BUFFERLINE_H */
