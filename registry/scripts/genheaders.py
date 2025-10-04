#!/usr/bin/env python3
# -*- encoding utf-8 -*-
# Copyright (c) [2025] Dylan Perks (@Perksey) and Contributors
# SPDX-License-Identifier: Unlicense

import dataclasses
import os
import os.path
import datetime
import typing
import xml.etree.ElementTree
import reg


"""
A very primitive C header generator that makes use of the al.xml parser defined in reg.py.
This has been designed to be as close to the original headers (i.e. the headers before the generator was introduced)
as possible, which is why this is so verbose at times.

The headers that are generated are mostly defined by the CONFIGURATION variable defined below (after the preambles).
"""


@dataclasses.dataclass
class Header:
    """
    A configuration for the header generator.

    Attributes
    ----------
    header_file : str
        The path to the header file to generate, relative to this Python file (i.e. not necessarily the working
        directory)
    registry_file : str
        The path to the XML to parse, relative to this Python file (i.e. not necessarily the working directory)
    api : str or list of str
        The "api" attribute values to include in this header. Currently in OpenAL we have one header for "al", one
        header for "alc", and then one header for just the extensions (notwithstanding the EFX annex) which has both
        "al" and "alc" defined for this property.
    is_extension_header : bool
        True if this is an extension header, false otherwise. If include/exclude are not defined, this flag determines
        whether all <extension>s will be used for the default value of include or whether all <feature>s will be used.
    include : list of str, optional
        A list of <feature> or <extension> names to generate C code for. See the is_extension_header docs for the
        default behaviour if this is not provided.
    exclude : list of str, optional
        A list of <feature> or <extension> names to explicitly exclude from C code generation. This is empty by default.
    preamble : str, optional
        A snippet of C code to include at the start of the file.
        TODO: really we should be working to minimise this as much as possible (this is required for compatibility with
        the Khronos scripts for instance)
    """

    header_file: str
    registry_file: str
    api: str | typing.List[str]
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

EXT_PREAMBLE = """#include <stddef.h>
/* Define int64 and uint64 types */
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) ||             \\
    (defined(__cplusplus) && __cplusplus >= 201103L)
#include <stdint.h>
typedef int64_t _alsoft_int64_t;
typedef uint64_t _alsoft_uint64_t;
#elif defined(_WIN32)
typedef __int64 _alsoft_int64_t;
typedef unsigned __int64 _alsoft_uint64_t;
#else
/* Fallback if nothing above works */
#include <stdint.h>
typedef int64_t _alsoft_int64_t;
typedef uint64_t _alsoft_uint64_t;
#endif

#include "alc.h"
#include "al.h"

#ifdef __cplusplus
extern "C" {
#endif

"""


CONFIGURATION = [
    Header(
        "../../include/AL/alc.h",
        "../xml/al.xml",
        "alc",
        False,
        preamble=PLATFORM_PREAMBLE.format(prefix="ALC"),
    ),
    Header(
        "../../include/AL/al.h",
        "../xml/al.xml",
        "al",
        False,
        preamble=PLATFORM_PREAMBLE.format(prefix="AL"),
    ),
    Header(
        "../../include/AL/alext.h",
        "../xml/al.xml",
        ["al", "alc"],
        True,
        preamble=EXT_PREAMBLE,
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

ENUM_NAME_COLS = 49


def render_api(api: reg.Api, pfn: bool, registry: reg.Registry) -> str:
    """
    Renders the C declaration for the given API.

    Parameters
    ----------
    api : reg.Api
        The API to render.
    pfn : bool
        If api is a reg.Command, whether to render a function pointer (true) or a function declaration (false).
    registry : reg.Registry
        The registry to which api belongs. This is used to lookup value ranges.

    Returns
    -------
    str
        The C declaration.
    """
    if isinstance(api, reg.Command):
        if len(api.parameters) == 0:
            params = "(void)"
        else:
            params = f"({', '.join(x.repr.strip() for x in api.parameters)})"
        if pfn:
            return (
                f"typedef {api.return_type} ({api.namespace}_APIENTRY *"
                f"{api.pfn_name}){params} {api.namespace}_API_NOEXCEPT17;"
            )
        else:
            export = f"{api.namespace}_API " if api.export is not None else ""
            doc = reg.render_doc_comment(api.doc, registry)
            return (
                f"{doc}{export}{api.return_type} "
                f"{api.namespace}_APIENTRY {api.name}{params} {api.namespace}_API_NOEXCEPT;"
            )
    elif isinstance(api, reg.Enum):
        doc = reg.render_doc_comment(api.doc, registry, api.property)
        return f"{doc}{f'#define {api.name} ':<{ENUM_NAME_COLS}}{api.value}"
    elif isinstance(api, reg.Typedef) or isinstance(api, reg.Verbatim):
        return f"{reg.render_doc_comment(api.doc, registry)}{api.repr}"
    raise TypeError


def render_set(
    api_set: reg.ApiSet,
    registry: reg.Registry,
    current_header: Header,
    include_guard: bool = True,
) -> typing.Generator[str]:
    """
    Renders all APIs in the given API set (feature or extension). Note that this may implicitly generate a new header if
    the API set is annexed (e.g. EFX).

    Parameters
    ----------
    api_set : reg.ApiSet
        The API set to render.
    registry : reg.Registry
        The registry to which this API set belongs. Used to lookup value ranges.
    current_header : Header
        The header currently being generated. This is used to determine whether to include API-specific requirements,
        and also to determine whether an annexed API set header needs to be generated.
    include_guard : bool
        Whether to generate the #ifndef/#define include guards for this API set specifically. Always false for an annex.

    Returns
    -------
    iterable of str
        The lines of C code that make up this API set. Note that one element iterated by this generator may itself have
        multiple lines.
    """

    if include_guard:
        yield f"#ifndef {api_set.name}"
        yield f"#define {api_set.name} 1"
    if api_set.doc is not None:
        if len(api_set.doc) == 1:
            yield f"/* {api_set.doc[0].strip()} */"
        else:
            yield f"/* {api_set.doc[0].strip()}"
        for line in api_set.doc[1:]:
            yield f" * {line.strip()}"
        yield " */"
    annex_header = None
    if api_set.annex is not None:
        annex_header = f"{api_set.annex}.h"
    if (
        annex_header is not None
        and os.path.basename(current_header.header_file) != annex_header
    ):
        ext_header_file = os.path.join(
            os.path.dirname(__file__),
            os.path.dirname(current_header.header_file),
            annex_header,
        )
        with open(ext_header_file, "w") as f:
            external_headers = "".join(
                f"#include <{h.name}>\n"
                for h in registry.apis.values()
                if isinstance(h, reg.Include)
            )
            other_headers = "".join(
                f'#include "{os.path.basename(h.header_file)}"\n'
                for h in CONFIGURATION
                if h.header_file != current_header.header_file
            )
            preamble = f'{external_headers}\n{other_headers}\n#ifdef __cplusplus\nextern "C" {{\n#endif'.strip()
            f.write(
                STANDARD_TEMPLATE.format(
                    guard=f"AL_{os.path.basename(annex_header).upper().replace('.', '_')}",
                    preamble=preamble,
                    content="\n".join(
                        render_set(
                            api_set,
                            registry,
                            Header(
                                ext_header_file,
                                current_header.registry_file,
                                current_header.api,
                                current_header.include,
                                current_header.exclude,
                                preamble,
                            ),
                            False,
                        )
                    ),
                    date=datetime.datetime.now(datetime.timezone.utc),
                )
            )
        yield f'#include "{annex_header}"'
        if include_guard:
            yield "#endif"
        yield ""
        return
    passes = (
        ("non-command", "command-function", "command-pfn")
        if api_set.is_feature
        else ("non-command", "command-pfn", "command-function")
    )
    for pass_no, pass_name in enumerate(passes):
        written_preamble = False
        last_namespace = None
        for req_no, requirement in enumerate(api_set.require):
            if (
                requirement.api_specific is not None
                and requirement.api_specific not in current_header.api
            ):
                continue
            should_write_comment = (
                requirement.comment is not None and pass_name != "command-pfn"
            )
            written_comment = False
            written_any = False
            for api_name in requirement.apis:
                # We pop on the last pass to take account of promotions
                api = registry.apis.get(api_name)
                if api is None:
                    print(f"Skipping {api_name} for {api_set.name} as it doesn't exist")
                    continue
                if isinstance(api, reg.Include):
                    continue
                if pass_name.startswith("command-") != isinstance(api, reg.Command):
                    continue
                if (
                    last_namespace is not None
                    and pass_name == "command-function"
                    and last_namespace != api.namespace
                ):
                    yield "#endif"
                    written_preamble = False
                if not written_preamble:
                    if pass_name == "command-function":
                        if api_set.is_feature:
                            yield f"#ifndef {api.namespace}_NO_PROTOTYPES"
                            last_namespace = api.namespace
                        else:
                            yield "#ifdef AL_ALEXT_PROTOTYPES"
                    elif pass_name == "command-pfn" and api_set.is_feature:
                        yield "/* Pointer-to-function types, useful for storing dynamically loaded AL entry"
                        yield " * points."
                        yield " */"
                    written_preamble = True
                if not written_comment and should_write_comment:
                    yield f"/* {requirement.comment} */"
                    written_comment = True
                api = render_api(api, pass_name == "command-pfn", registry)
                api_lines = api.splitlines()
                for line in api_lines:
                    already_set = registry.written.get(api_name)
                    if already_set is not None:
                        if line.startswith("/*") or line.startswith(" *"):
                            continue
                        print(
                            f"{api_name} is used in {api_set.name} but was already written by {already_set}"
                        )
                        yield f"/*{line}*/"
                    else:
                        yield line
                if len(api_lines) > 1:
                    yield ""
                written_any = True
            # if requirement.comment is not None and written_comment:
            if (
                written_any
                and req_no != len(api_set.require) - 1
                and len(api_lines) <= 1
            ):
                yield ""
        if pass_name == "command-function" and written_preamble:
            if api_set.is_feature:
                yield f"#endif /* {last_namespace}_NO_PROTOTYPES */"
            else:
                yield "#endif"
            if pass_no != 2:
                yield ""
    if include_guard:
        yield "#endif"
    yield ""
    for requirement in api_set.require:
        for api in requirement.apis:
            registry.written[api] = api_set.name


def create_header(registry: reg.Registry, header: Header):
    """
    Creates a header file using the given configuration and XML registry.

    Parameters
    ----------
    registry : reg.Registry
        The registry to get API declarations from.
    header : Header
        The header generation configuration.
    """
    output_file = os.path.join(os.path.dirname(__file__), header.header_file)
    header = STANDARD_TEMPLATE.format(
        guard=f"AL_{os.path.basename(output_file).upper().replace('.', '_')}",
        preamble=header.preamble,
        content="\n".join(
            x
            for y in (
                render_set(registry.sets[s], registry, header) for s in header.include
            )
            for x in y
        ),
        date=datetime.datetime.now(datetime.timezone.utc),
    )
    with open(output_file, "w") as f:
        f.write(header)


def main():
    """
    The main entry-point for the header generation script.
    """

    print("Reading registries...")
    registries: typing.Dict[str, reg.Registry] = {}
    for registry in set(x.registry_file for x in CONFIGURATION):
        # Read XML relative to this file
        registry_xml = xml.etree.ElementTree.parse(
            os.path.join(os.path.dirname(__file__), registry)
        )

        # Parse
        registries[registry] = reg.Registry(registry_xml)

    for header in CONFIGURATION:
        if isinstance(header.api, str):
            header.api = [header.api]
        if header.include is None:
            header.include = [
                k
                for k, v in registries[header.registry_file].sets.items()
                if not v.is_feature == header.is_extension_header
                and (header.exclude is None or k not in header.exclude)
                and any(a in v.api for a in header.api)
            ]
        print(f"Generating {header.header_file}...")
        create_header(registries[header.registry_file], header)


if __name__ == "__main__":
    main()
