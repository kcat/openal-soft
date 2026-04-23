module;

#include "context.hpp"

export module alc.context;

export {

using ::ContextFlags;
using ::ContextFlagBitset;
using ::DebugLogEntry;

namespace al {

using al::ContextDeleter;
using al::Context;
using al::verify_context;

}

using ::ContextRef;
using ::GetContextRef;
using ::UpdateContextProps;

using ::TrapALError;

}