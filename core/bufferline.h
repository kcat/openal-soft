#ifndef CORE_BUFFERLINE_H
#define CORE_BUFFERLINE_H

#include <array>

/* Size for temporary storage of buffer data, in floats. Larger values need
 * more memory and are harder on cache, while smaller values may need more
 * iterations for mixing.
 */
#define BUFFERSIZE 1024

using FloatBufferLine = std::array<float,BUFFERSIZE>;

#endif /* CORE_BUFFERLINE_H */
