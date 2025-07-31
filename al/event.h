#ifndef AL_EVENT_H
#define AL_EVENT_H

namespace al {
struct Context;
} // namespace al

void StartEventThrd(al::Context *ctx);
void StopEventThrd(al::Context *ctx);

#endif
