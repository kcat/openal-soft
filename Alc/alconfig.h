#ifndef ALCONFIG_H
#define ALCONFIG_H

#include "aloptional.h"

void ReadALConfig();

int ConfigValueExists(const char *devName, const char *blockName, const char *keyName);
const char *GetConfigValue(const char *devName, const char *blockName, const char *keyName, const char *def);
int GetConfigValueBool(const char *devName, const char *blockName, const char *keyName, int def);

int ConfigValueStr(const char *devName, const char *blockName, const char *keyName, const char **ret);
al::optional<int> ConfigValueInt(const char *devName, const char *blockName, const char *keyName);
al::optional<unsigned int> ConfigValueUInt(const char *devName, const char *blockName, const char *keyName);
int ConfigValueFloat(const char *devName, const char *blockName, const char *keyName, float *ret);
int ConfigValueBool(const char *devName, const char *blockName, const char *keyName, int *ret);

#endif /* ALCONFIG_H */
