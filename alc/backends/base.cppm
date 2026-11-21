module;

#include "alc/backends/base.hpp"

export module backends.base;

export {
    using ::ClockLatency;
    using ::BackendBase;
    using ::BackendPtr;

    using ::BackendType;
    using ::BackendFactory;

    using ::NullBackendFactory;
    using ::LoopbackBackendFactory;
#if HAVE_PIPEWIRE
    using ::PipeWireBackendFactory;
#endif
#if HAVE_JACK
    using ::JackBackendFactory;
#endif
#if HAVE_PULSEAUDIO
    using ::PulseBackendFactory;
#endif
#if HAVE_ALSA
    using ::AlsaBackendFactory;
#endif
#if HAVE_WASAPI
    using ::WasapiBackendFactory;
#endif
#if HAVE_COREAUDIO
    using ::CoreAudioBackendFactory;
#endif
#if HAVE_OPENSL
    using ::OSLBackendFactory;
#endif
#if HAVE_OBOE
    using ::OboeBackendFactory;
#endif
#if HAVE_SOLARIS
    using ::SolarisBackendFactory;
#endif
#if HAVE_SNDIO
    using ::SndIOBackendFactory;
#endif
#if HAVE_OSS
    using ::OSSBackendFactory;
#endif
#if HAVE_DSOUND
    using ::DSoundBackendFactory;
#endif
#if HAVE_WINMM
    using ::WinMMBackendFactory;
#endif
#if HAVE_PORTAUDIO
    using ::PortBackendFactory;
#endif
#if HAVE_SDL3
    using ::SDL3BackendFactory;
#endif
#if HAVE_SDL2
    using ::SDL2BackendFactory;
#endif
#if HAVE_WAVE
    using ::WaveBackendFactory;
#endif
}
