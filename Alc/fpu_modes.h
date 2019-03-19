#ifndef FPU_MODES_H
#define FPU_MODES_H

class FPUCtl {
#if defined(HAVE_SSE_INTRINSICS) || (defined(__GNUC__) && defined(HAVE_SSE))
    unsigned int sse_state{};
#elif defined(HAVE___CONTROL87_2)
    unsigned int state{};
    unsigned int sse_state{};
#elif defined(HAVE__CONTROLFP)
    unsigned int state{};
#endif
    bool in_mode{};

public:
    FPUCtl() noexcept;
    ~FPUCtl() { leave(); }

    FPUCtl(const FPUCtl&) = delete;
    FPUCtl& operator=(const FPUCtl&) = delete;

    void leave() noexcept;
};

#endif /* FPU_MODES_H */
