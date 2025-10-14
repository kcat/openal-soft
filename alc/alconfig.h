#ifndef ALCONFIG_H
#define ALCONFIG_H

#include <optional>
#include <string>
#include <string_view>

#include "alnumeric.h"

void ReadALConfig();

auto GetConfigValueBool(std::string_view devName, std::string_view blockName,
    std::string_view keyName, bool def) -> bool;

auto ConfigValueStr(std::string_view devName, std::string_view blockName, std::string_view keyName)
    -> std::optional<std::string>;
auto ConfigValueI32(std::string_view devName, std::string_view blockName, std::string_view keyName)
    -> std::optional<i32>;
auto ConfigValueU32(std::string_view devName, std::string_view blockName, std::string_view keyName)
    -> std::optional<u32>;
auto ConfigValueF32(std::string_view devName, std::string_view blockName,
    std::string_view keyName) -> std::optional<f32>;
auto ConfigValueBool(std::string_view devName, std::string_view blockName,
    std::string_view keyName) -> std::optional<bool>;

#endif /* ALCONFIG_H */
