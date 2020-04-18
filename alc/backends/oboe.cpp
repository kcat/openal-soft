
#include "config.h"

#include "oboe.h"

#include "oboe/Oboe.h"


bool OboeBackendFactory::init() { return true; }

bool OboeBackendFactory::querySupport(BackendType /*type*/)
{ return false; }

std::string OboeBackendFactory::probe(BackendType /*type*/)
{
    return std::string{};
}

BackendPtr OboeBackendFactory::createBackend(ALCdevice* /*device*/, BackendType /*type*/)
{
    return nullptr;
}

BackendFactory &OboeBackendFactory::getFactory()
{
    static OboeBackendFactory factory{};
    return factory;
}
