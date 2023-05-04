#ifndef ALCONFIG_H
#define ALCONFIG_H

#include <optional>
#include <string>


void ReadALConfig();

bool GetConfigValueBool(const char *devName, const char *blockName, const char *keyName, bool def);

std::optional<std::string> ConfigValueStr(const char *devName, const char *blockName, const char *keyName);
std::optional<int> ConfigValueInt(const char *devName, const char *blockName, const char *keyName);
std::optional<unsigned int> ConfigValueUInt(const char *devName, const char *blockName, const char *keyName);
std::optional<float> ConfigValueFloat(const char *devName, const char *blockName, const char *keyName);
std::optional<bool> ConfigValueBool(const char *devName, const char *blockName, const char *keyName);

#endif /* ALCONFIG_H */
