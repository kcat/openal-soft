#!/usr/bin/env python3
# -*- encoding utf-8 -*-
# Copyright (c) [2025] Dylan Perks (@Perksey) and Contributors
# SPDX-License-Identifier: LGPL-2.1

import os
import os.path
import typing
import xml.etree.ElementTree
import dataclasses
import datetime

"""
A purpose-built Khronos-style XML parser for al.xml and C header generator for the OpenAL Soft headers. This has been
built to minimise changes to the shipping headers, and full compatibility, completeness, and correctness with the
official Khronos scripts is an explicit non-goal. It is also expected that this script will not be adaptable to
non-OpenAL uses. The code is also very crude. Have fun!
"""


@dataclasses.dataclass
class Header:
    header_file: str
    registry_file: str
    api: str
    is_extension_header: bool = False
    include: typing.Optional[typing.List[str]] = None
    exclude: typing.Optional[typing.List[str]] = None
    preamble: typing.Optional[str] = None


PLATFORM_PREAMBLE = """#ifdef __cplusplus
extern "C" {{

#ifdef _MSVC_LANG
#define {prefix}_CPLUSPLUS _MSVC_LANG
#else
#define {prefix}_CPLUSPLUS __cplusplus
#endif

#ifndef AL_DISABLE_NOEXCEPT
#if {prefix}_CPLUSPLUS >= 201103L
#define {prefix}_API_NOEXCEPT noexcept
#else
#define {prefix}_API_NOEXCEPT
#endif
#if {prefix}_CPLUSPLUS >= 201703L
#define {prefix}_API_NOEXCEPT17 noexcept
#else
#define {prefix}_API_NOEXCEPT17
#endif

#else /* AL_DISABLE_NOEXCEPT */

#define {prefix}_API_NOEXCEPT
#define {prefix}_API_NOEXCEPT17
#endif

#undef {prefix}_CPLUSPLUS

#else /* __cplusplus */

#define {prefix}_API_NOEXCEPT
#define {prefix}_API_NOEXCEPT17
#endif

#ifndef {prefix}_API
 #if defined(AL_LIBTYPE_STATIC)
  #define {prefix}_API
 #elif defined(_WIN32)
  #define {prefix}_API __declspec(dllimport)
 #else
  #define {prefix}_API extern
 #endif
#endif

#ifdef _WIN32
 #define {prefix}_APIENTRY __cdecl
#else
 #define {prefix}_APIENTRY
#endif

"""


CONFIGURATION = [
    Header(
        "../../include/AL/al.h",
        "../xml/al.xml",
        "al",
        False,
        preamble=PLATFORM_PREAMBLE.format(prefix="AL"),
    ),
    Header(
        "../../include/AL/alc.h",
        "../xml/al.xml",
        "alc",
        False,
        preamble=PLATFORM_PREAMBLE.format(prefix="ALC"),
    ),
]

STANDARD_TEMPLATE = """#ifndef {guard}
#define {guard}

/* This file is auto-generated! Please do not edit it manually.
 * Instead, modify the API in al.xml and regenerate using genheaders.py.
 *
 * Last regenerated: {date}
 */

/* NOLINTBEGIN */
{preamble}
{content}
#ifdef __cplusplus
}} /* extern "C" */
#endif
/* NOLINTEND */

#endif /* {guard} */
"""

# Max columns a doc comment can take up (excluding the " * ")
DOC_MAX_COLS = 80 - 3
ENUM_NAME_COLS = 49


@dataclasses.dataclass
class Requirement:
    apis: typing.List[str]
    comment: typing.Optional[str] = None


@dataclasses.dataclass
class ApiSet:
    is_feature: bool
    name: str

    # If extension, contains the value for "supported"
    # If feature, contains the value for "api"
    api: typing.List[str]
    require: typing.List[Requirement]

    def render(self, registry: "Registry") -> typing.Generator[str]:
        yield f"#ifndef {self.name}"
        yield f"#define {self.name} 1"
        for pass_name in ("non-command", "command-function", "command-pfn"):
            written_preamble = False
            for requirement in self.require:
                should_write_comment = (
                    requirement.comment is not None and pass_name != "command-pfn"
                )
                written_comment = False
                for api_name in requirement.apis:
                    # We pop on the last pass to take account of promotions
                    api = (
                        registry.apis.get(api_name)
                        if pass_name != "command-pfn"
                        else registry.apis.pop(api_name, None)
                    )
                    if api is None:
                        print(
                            f"Skipping {api_name} for {self.name} as it is unavailable (has it been promoted?)"
                        )
                        continue
                    if pass_name.startswith("command-") != isinstance(api, Command):
                        continue
                    if not written_preamble:
                        if pass_name == "command-function":
                            yield "#ifndef AL_NO_PROTOYPES"
                        elif pass_name == "command-pfn" and self.is_feature:
                            yield "/* Pointer-to-function types, useful for storing dynamically loaded AL entry"
                            yield " * points."
                            yield " */"
                        written_preamble = True
                    if not written_comment and should_write_comment:
                        yield f"/* {requirement.comment} */"
                        written_comment = True
                    if isinstance(api, Command):
                        yield getattr(api, pass_name[len("command-") :])
                    else:
                        yield api
                    if pass_name != "command-pfn" and requirement.comment is None:
                        yield ""
                if requirement.comment is not None and written_comment:
                    yield ""
            if pass_name == "command-function" and written_preamble:
                yield "#endif /* AL_NO_PROTOTYPES */"
                yield ""
        yield "#endif"


@dataclasses.dataclass
class Command:
    function: str
    pfn: str


class Registry:
    # A map of API names to rendered strings.
    # Commands, enums, etc are all rolled into here given that C guarantees that none of these identifiers will collide
    apis: typing.Dict[str, str | Command]

    # A map of feature or extension names to API sets
    sets: typing.Dict[str, ApiSet]

    groups: typing.Dict[str, typing.List[str]]

    def __init__(self, registry: xml.etree.ElementTree.ElementTree):
        self.groups = {}
        for enum in registry.findall(".//enums/enum"):
            for group in enum.attrib.get("group", "").split(","):
                group = group.strip()
                if group == "":
                    continue
                members = self.groups.get(group, [])
                if len(members) == 0:
                    self.groups[group] = members
                members.append(enum.attrib["name"])

        def innertext(
            tag: xml.etree.ElementTree.Element,
            recurse_fn: typing.Optional[
                typing.Callable[[xml.etree.ElementTree.Element], bool]
            ] = None,
        ) -> str:
            return (
                (tag.text or "")
                + "".join(
                    innertext(e) for e in tag if recurse_fn is None or recurse_fn(e)
                )
                + (tag.tail or "")
            )

        def return_type(proto: xml.etree.ElementTree.Element) -> str:
            return innertext(proto, lambda x: x.tag != "name").strip()

        def doc(tag: xml.etree.ElementTree.Element) -> str:
            def render_range(range_str: str, prefix_cols: int) -> typing.Generator[str]:
                if ".." in range_str:
                    bounds = range_str.split("..")
                    if sum(1 for x in bounds if x.strip() != "") > 1:
                        bounds[1] = (
                            bounds[1][1:] if "=" in bounds[1] else f"<{bounds[1][1:]}"
                        )
                    else:
                        bounds[1] = ""
                    yield f"{'Range: ':<{prefix_cols}}[{bounds[0]} - {bounds[1]}]"
                    return
                value_range = [
                    x
                    for y in (
                        self.groups[v] if v in self.groups else [v]
                        for v in range_str.split(",")
                    )
                    for x in y  # <-- flatten
                ]
                ret = f"{'Range: ':<{prefix_cols}}"
                if len(value_range) > 1:
                    ret += "["
                for i, value in enumerate(value_range):
                    if i == 0:
                        ret += value
                    elif len(ret) + 2 + len(value) + 1 > DOC_MAX_COLS:
                        yield f"{ret},"
                        ret = f"{' ' * (prefix_cols + 1)}{value}"
                    else:
                        ret = f"{ret}, {value}"
                if len(value_range) > 1:
                    ret += "]"
                yield ret

            doclines = []
            comment = tag.attrib.get("comment")
            if comment is not None:
                doclines.append(comment)
            property = tag.find("property")
            if property is not None:
                cols = 0
                prop_type = property.attrib.get("type")
                default = property.attrib.get("default")
                range_str = property.attrib.get("group") or property.attrib.get("range")
                class_str = property.attrib.get("class")
                if default is not None:
                    cols = len("Default: ")
                elif range_str is not None or class_str is not None:
                    cols = len("Range: ")
                else:
                    cols = len("Type: ")
                if prop_type is not None:
                    doclines.append(f"{'Type: ':<{cols}}{prop_type.replace(',', ', ')}")
                if range_str is not None:
                    gr = self.groups.get(range_str)
                    if gr is not None:
                        range_str = ",".join(gr)
                    doclines += [x for x in render_range(range_str, cols)]
                if class_str is not None:
                    doclines.append(
                        f"{'Range: ':<{cols}}any valid {class_str.title()} ID"
                    )
                if default is not None:
                    if "," in default:
                        default = "{" + default.replace(",", ", ") + "}"
                    doclines.append(f"{'Default: ':<{cols}}{default}")
            comment = tag.find("comment")
            if comment is not None:
                if len(doclines) > 0:
                    doclines.append("")
                for line in comment.text.strip().splitlines():
                    doclines.append(line.strip())
            if len(doclines) == 0:
                return ""
            if len(doclines) == 1 and comment is None:
                return f"/** {doclines[0]} */\n"
            return f"/**\n{''.join(f' * {line}'.rstrip(' ') + '\n' for line in doclines)} */\n"

        self.apis = {}
        for type in registry.findall(".//types/type"):
            # Find name
            name = type.find("name")
            if name is None:
                print(f"Skipping: {type}")
                continue
            # Types are verbatim
            self.apis[name.text.strip()] = f"{doc(type)}{innertext(type)}".strip()

        for commands in registry.findall(".//commands"):
            namespace = commands.attrib.get("namespace", "AL")
            pfn_return_cols = max(
                len(return_type(p).strip()) for p in commands.findall(".//proto")
            )
            for command in commands.iter("command"):
                proto = command.find("proto")
                name = proto.find("name")
                params = command.findall("param")
                if name is None:
                    print(f"Skipping: {command}")
                    continue
                noexcept = (
                    f" {namespace}_API_NOEXCEPT"
                    if command.attrib.get("except") == "no"
                    else ""
                )
                if len(params) == 0:
                    params = "(void)"
                else:
                    params = f"({', '.join(innertext(x).strip() for x in params)})"
                self.apis[name.text.strip()] = Command(
                    (
                        f"{doc(command)}{namespace}_API {return_type(proto)} "
                        f"{namespace}_APIENTRY {name.text.strip()}{params}{noexcept};"
                    ),
                    (
                        f"typedef {return_type(proto): <{pfn_return_cols}} ({namespace}_APIENTRY *LP{name.text.upper()})"
                        f"{params}{noexcept}17;"
                        if noexcept != ""
                        else f"{params};"
                    ),
                )

        for enum in registry.findall(".//enums/enum"):
            name = enum.attrib.get("name")
            value = enum.attrib.get("value")
            if name is None or value is None:
                continue
            value = value.strip()
            if "." in value:
                value = f"({value})"
            self.apis[name.strip()] = (
                f"{doc(enum)}{f'#define {name} ':<{ENUM_NAME_COLS}}{value}"
            )

        self.sets = {}
        for api_set in (
            x
            for y in (
                registry.findall(".//feature"),
                registry.findall(".//extensions/extension"),
            )
            for x in y  # <-- flatten
        ):
            # TODO if ever we use <remove>, add support here.
            self.sets[api_set.attrib["name"]] = ApiSet(
                api_set.tag == "feature",
                api_set.attrib["name"],
                (
                    [api_set.attrib["api"]]
                    if api_set.tag == "feature"
                    else api_set.attrib["supported"].split("|")
                ),
                require=[
                    Requirement(
                        [y.attrib["name"] for y in x if "name" in y.attrib],
                        x.attrib.get("comment"),
                    )
                    for x in api_set
                    if x.tag == "require"
                ],
            )

        #for k, v in self.apis.items():
        #    if isinstance(v, Command):
        #        print(f'"{k}" = "{v.function}" "{v.pfn}"')
        #        continue
        #    print(f'"{k}" = "{v}"')

    def create_header(
        self, sets: typing.Iterable[str], output_file: str, preamble: str = ""
    ):
        header = STANDARD_TEMPLATE.format(
            guard=f"AL_{os.path.basename(output_file).upper().replace('.', '_')}",
            preamble=preamble,
            content="\n".join(
                x for y in (self.sets[s].render(self) for s in sets) for x in y
            ),
            date=datetime.datetime.now(datetime.timezone.utc),
        )
        with open(output_file, "w") as f:
            f.write(header)


def main():
    print("Reading registries...")
    registries: typing.Dict[str, Registry] = {}
    for registry in set(x.registry_file for x in CONFIGURATION):
        # Read XML relative to this file
        registry_xml = xml.etree.ElementTree.parse(
            os.path.join(os.path.dirname(__file__), registry)
        )

        # Parse
        registries[registry] = Registry(registry_xml)

    for header in CONFIGURATION:
        if header.include is None:
            header.include = [
                k
                for k, v in registries[header.registry_file].sets.items()
                if not v.is_feature == header.is_extension_header
                and (header.exclude is None or k not in header.exclude)
                and header.api in v.api
            ]
        print(f"Generating {header.header_file}...")
        registries[header.registry_file].create_header(
            header.include,
            os.path.join(os.path.dirname(__file__), header.header_file),
            header.preamble,
        )


if __name__ == "__main__":
    main()
