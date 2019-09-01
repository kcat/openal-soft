
#include "verstr.h"

#include "version.h"


const char *GetVersionString()
{
    return ALSOFT_VERSION "-" ALSOFT_GIT_COMMIT_HASH " (" ALSOFT_GIT_BRANCH " branch).";
}
