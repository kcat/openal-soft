#ifndef CORE_FPU_CTRL_H
#define CORE_FPU_CTRL_H

#include "opthelpers.h"

class FPUCtl {
    unsigned int sse_state{};
    bool in_mode{};

    static unsigned int Set() noexcept NONBLOCKING;
    static void Reset(unsigned int state) noexcept NONBLOCKING;

public:
    FPUCtl() noexcept : sse_state{Set()}, in_mode{true} { }
    ~FPUCtl() { if(in_mode) Reset(sse_state); }

    FPUCtl(const FPUCtl&) = delete;
    FPUCtl& operator=(const FPUCtl&) = delete;

    void enter() noexcept
    {
        if(!in_mode)
            sse_state = Set();
        in_mode = true;
    }
    void leave() noexcept
    {
        if(in_mode)
            Reset(sse_state);
        in_mode = false;
    }
};

#endif /* CORE_FPU_CTRL_H */
