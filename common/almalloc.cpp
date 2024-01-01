
#include "config.h"

#include "almalloc.h"

#include <new>
#include <cstring>


gsl::owner<void*> al_calloc(size_t alignment, size_t size)
{
    gsl::owner<void*> ret{::operator new[](size, std::align_val_t{alignment}, std::nothrow)};
    if(ret) std::memset(ret, 0, size);
    return ret;
}
