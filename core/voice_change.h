#ifndef VOICE_CHANGE_H
#define VOICE_CHANGE_H

#include <atomic>


struct Voice;


enum class VChangeState {
    Reset,
    Stop,
    Play,
    Pause,
    Restart
};
struct VoiceChange {
    Voice *mOldVoice{nullptr};
    Voice *mVoice{nullptr};
    unsigned mSourceID{0};
    VChangeState mState{};

    std::atomic<VoiceChange*> mNext{nullptr};
};
using LPVoiceChange = VoiceChange*;

#endif /* VOICE_CHANGE_H */
