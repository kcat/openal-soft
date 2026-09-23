//
// EAX API.
//
// Based on headers `eax[2-5].h` included in Doom 3 source code:
// https://github.com/id-Software/DOOM-3/tree/master/neo/openal/include
//

#include "config.h"

#include "api.h"

static_assert(sizeof(EAX30SOURCEPROPERTIES) == 72);
static_assert(sizeof(EAXSOURCESENDPROPERTIES) == 24);
static_assert(sizeof(EAXSOURCEOCCLUSIONSENDPROPERTIES) == 32);
static_assert(sizeof(EAXSOURCEEXCLUSIONSENDPROPERTIES) == 24);
static_assert(sizeof(EAXSOURCEALLSENDPROPERTIES) == 48);
