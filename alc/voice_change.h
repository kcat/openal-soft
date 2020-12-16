#ifndef VOICE_CHANGE_H
#define VOICE_CHANGE_H

#include <atomic>

#include "almalloc.h"

struct Voice;

using uint = unsigned int;


struct VoiceChange {
    Voice *mOldVoice{nullptr};
    Voice *mVoice{nullptr};
    uint mSourceID{0};
    int mState{0};

    std::atomic<VoiceChange*> mNext{nullptr};

    DEF_NEWDEL(VoiceChange)
};

#endif /* VOICE_CHANGE_H */
