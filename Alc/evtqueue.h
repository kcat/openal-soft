#ifndef AL_EVTQUEUE_H
#define AL_EVTQUEUE_H

#include "AL/al.h"

#include "alMain.h"

typedef struct MidiEvent {
    ALuint64 time;
    ALuint event;
    ALuint param[2];
} MidiEvent;

typedef struct EvtQueue {
    MidiEvent *events;
    ALsizei pos;
    ALsizei size;
    ALsizei maxsize;
} EvtQueue;

void InitEvtQueue(EvtQueue *queue);
void ResetEvtQueue(EvtQueue *queue);
ALenum InsertEvtQueue(EvtQueue *queue, const MidiEvent *evt);

#endif /* AL_EVTQUEUE_H */
