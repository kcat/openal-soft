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
A very primitive C header/module generator that makes use of the al.xml parser defined in reg.py.
This has been designed to be as close to the original headers/modules (i.e. the headers/modules before the generator was
introduced) as possible, which is why this is so verbose at times.

The files that are generated are mostly defined by the CONFIGURATION variable defined below (after the preambles).
"""


@dataclasses.dataclass
class FileToGenerate:
    """
    A configuration for the header/module generator.

    Attributes
    ----------
    output_file : str
        The path to the header/module file to generate, relative to this Python file (i.e. not necessarily the working
        directory)
    is_header : bool
        True if this file is a header, false if it is a module.
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

    output_file: str
    is_header: bool
    registry_file: str
    api: str | typing.List[str]
    is_extension_only: bool = False
    include: typing.Optional[typing.List[str]] = None
    exclude: typing.Optional[typing.List[str]] = None
    preamble: typing.Optional[str] = None


PLATFORM_HEADER_PREAMBLE = """#ifdef __cplusplus
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

EXT_HEADER_PREAMBLE = """#include <stddef.h>
/* Define int64 and uint64 types */
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) ||             \\
    (defined(__cplusplus) && __cplusplus >= 201103L)
#include <stdint.h>
typedef int64_t alsoft_impl_int64_t;
typedef uint64_t alsoft_impl_uint64_t;
#elif defined(_WIN32)
typedef __int64 alsoft_impl_int64_t;
typedef unsigned __int64 alsoft_impl_uint64_t;
#else
/* Fallback if nothing above works */
#include <stdint.h>
typedef int64_t alsoft_impl_int64_t;
typedef uint64_t alsoft_impl_uint64_t;
#endif

#include "alc.h"
#include "al.h"

#ifdef __cplusplus
extern "C" {
#endif

"""

MAIN_MODULE_COMMENT = """
/* The AL module provides core functionality of the AL API, without any non-
 * standard extensions.
 *
 * There are some limitations with the AL module. Stuff like AL_API and
 * AL_APIENTRY can't be used by code importing it since macros can't be
 * exported from modules, and there's no way to make aliases for these
 * properties that can be exported. Luckily AL_API isn't typically needed by
 * user code since it's used to indicate functions as being imported from the
 * library, which is only relevant to the declarations made in the module
 * itself.
 *
 * AL_APIENTRY is similarly typically only needed for specifying the calling
 * convention for functions and function pointers declared in the module.
 * However, some extensions use callbacks that need user code to define
 * functions with the same calling convention. Currently this is set to use the
 * platform's default calling convention (that is, it's defined to nothing),
 * except on Windows where it's defined to __cdecl. Interestingly, capture-less
 * lambdas seem to generate conversion operators that match function pointers
 * of any calling convention, but short of that, the user will be responsible
 * for ensuring callbacks use the cdecl calling convention on Windows and the
 * default for other OSs.
 *
 * Additionally, enums are declared as global inline constexpr ints. This
 * should generally be fine, as long as user code doesn't try to use them in
 * the preprocessor which will no longer recognize or expand them to integer
 * literals. Being global ints also defines them as actual objects stored in
 * memory, lvalues whose addresses can be taken, instead of as integer literals
 * or prvalues, which may have subtle implications. An unnamed enum would be
 * better here, since the enumerators associate a value with a name and don't
 * become referenceable objects in memory, except that gives the name a new
 * type (e.g. typeid(AL_NO_ERROR) != typeid(int)) which could create problems
 * for type deduction.
 *
 * Note that defining AL_LIBTYPE_STATIC, AL_DISABLE_NOEXCEPT, and/or
 * AL_NO_PROTOTYPES does still influence the function and function pointer type
 * declarations, but only when compiling the module. The user-defined macros
 * have no effect when importing the module.
 */
"""

ALC_MODULE_COMMENT = """
/* The ALC module provides core functionality of the device/system ALC API,
 * without any non-standard extensions.
 *
 * There are some limitations with the ALC module. Stuff like ALC_API and
 * ALC_APIENTRY can't be used by code importing it since macros can't be
 * exported from modules, and there's no way to make aliases for these
 * properties that can be exported. Luckily ALC_API isn't typically needed by
 * user code since it's used to indicate functions as being imported from the
 * library, which is only relevant to the declarations made in the module
 * itself.
 *
 * ALC_APIENTRY is similarly typically only needed for specifying the calling
 * convention for functions and function pointers declared in the module.
 * However, some extensions use callbacks that need user code to define
 * functions with the same calling convention. Currently this is set to use the
 * platform's default calling convention (that is, it's defined to nothing),
 * except on Windows where it's defined to __cdecl. Interestingly, capture-less
 * lambdas seem to generate conversion operators that match function pointers
 * of any calling convention, but short of that, the user will be responsible
 * for ensuring callbacks use the cdecl calling convention on Windows and the
 * default for other OSs.
 *
 * Additionally, enums are declared as global inline constexpr ints. This
 * should generally be fine as long as user code doesn't try to use them in
 * the preprocessor, which will no longer recognize or expand them to integer
 * literals. Being global ints also defines them as actual objects stored in
 * memory, lvalues whose addresses can be taken, instead of as integer literals
 * or prvalues, which may have subtle implications. An unnamed enum would be
 * better here, since the enumerators associate a value with a name and don't
 * become referenceable objects in memory, except that gives the name a new
 * type (e.g. typeid(ALC_NO_ERROR) != typeid(int)) which could create problems
 * for type deduction.
 *
 * Note that defining AL_LIBTYPE_STATIC, AL_DISABLE_NOEXCEPT, and/or
 * ALC_NO_PROTOTYPES does still influence the function and function pointer
 * type declarations, but only when compiling the module. The user-defined
 * macros have no effect when importing the module.
 */
"""

PLATFORM_MODULE_PREAMBLE = """
#ifndef {ns}_API
 #if defined(AL_LIBTYPE_STATIC)
  #define {ns}_API
 #elif defined(_WIN32)
  #define {ns}_API __declspec(dllimport)
 #else
  #define {ns}_API extern
 #endif
#endif

#ifdef _WIN32
 #define {ns}_APIENTRY __cdecl
#else
 #define {ns}_APIENTRY
#endif

#ifndef AL_DISABLE_NOEXCEPT
 #define {ns}_API_NOEXCEPT noexcept
#else
 #define {ns}_API_NOEXCEPT
#endif

#define ENUMDCL inline constexpr auto

export module openal.{mod};
"""

EXT_MODULE_PREAMBLE = """
#include <array>
#include <cstddef>
#include <cstdint>

#ifndef ALC_API
 #if defined(AL_LIBTYPE_STATIC)
  #define ALC_API
 #elif defined(_WIN32)
  #define ALC_API __declspec(dllimport)
 #else
  #define ALC_API extern
 #endif
#endif

#ifdef _WIN32
 #define ALC_APIENTRY __cdecl
#else
 #define ALC_APIENTRY
#endif

#ifndef AL_DISABLE_NOEXCEPT
 #define ALC_API_NOEXCEPT noexcept
#else
 #define ALC_API_NOEXCEPT
#endif

#ifndef AL_API
 #define AL_API ALC_API
#endif
#define AL_APIENTRY ALC_APIENTRY
#define AL_API_NOEXCEPT ALC_API_NOEXCEPT

#define ENUMDCL inline constexpr auto

export module openal.ext;

export import openal.efx;

import openal.std;

using alsoft_impl_int64_t = std::int64_t;
using alsoft_impl_uint64_t = std::uint64_t;

extern "C" struct _GUID; /* NOLINT(*-reserved-identifier) */
"""


@dataclasses.dataclass
class ResetRegistryState:
    registry_file: str


CONFIGURATION = [
    FileToGenerate(
        "../../include/AL/alc.h",
        True,
        "../xml/al.xml",
        "alc",
        False,
        preamble=PLATFORM_HEADER_PREAMBLE.format(prefix="ALC"),
    ),
    FileToGenerate(
        "../../include/AL/al.h",
        True,
        "../xml/al.xml",
        "al",
        False,
        preamble=PLATFORM_HEADER_PREAMBLE.format(prefix="AL"),
    ),
    FileToGenerate(
        "../../include/AL/alext.h",
        True,
        "../xml/al.xml",
        ["al", "alc"],
        True,
        preamble=EXT_HEADER_PREAMBLE,
    ),
    ResetRegistryState("../xml/al.xml"),
    FileToGenerate(
        "../../modules/al.cppm",
        False,
        "../xml/al.xml",
        "al",
        False,
        preamble=MAIN_MODULE_COMMENT
        + PLATFORM_MODULE_PREAMBLE.format(ns="AL", mod="al"),
    ),
    FileToGenerate(
        "../../modules/alc.cppm",
        False,
        "../xml/al.xml",
        "alc",
        False,
        preamble=ALC_MODULE_COMMENT
        + PLATFORM_MODULE_PREAMBLE.format(ns="ALC", mod="alc"),
    ),
    FileToGenerate(
        "../../modules/alext.cppm",
        False,
        "../xml/al.xml",
        ["al", "alc"],
        True,
        exclude=["ALC_EXT_EFX"],
        preamble=EXT_MODULE_PREAMBLE,
    ),
    FileToGenerate(
        "../../modules/efx.cppm",
        False,
        "../xml/al.xml",
        ["al", "alc"],
        True,
        include=["ALC_EXT_EFX"],
        preamble="\n#include <array>\n#include <cfloat>\n"
        + PLATFORM_MODULE_PREAMBLE.format(ns="AL", mod="efx")
        + "\nimport openal.std;\n",
    ),
]

STANDARD_HEADER = """/* This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

/* This file is auto-generated! Please do not edit it manually.
 * Instead, modify the API in al.xml and regenerate using genheaders.py.
 *
 * Last regenerated: {date}
 */
""".format(
    date=datetime.datetime.now(datetime.timezone.utc)
)

STANDARD_HEADER_TEMPLATE = (
    STANDARD_HEADER
    + """
#ifndef {guard}
#define {guard}

/* NOLINTBEGIN */
{preamble}
{content}
#ifdef __cplusplus
}} /* extern "C" */
#endif
/* NOLINTEND */

#endif /* {guard} */
"""
)

STANDARD_MODULE_TEMPLATE = (
    STANDARD_HEADER
    + """
module;
{preamble}
export extern "C" {{
{content}
}} /* extern "C" */
"""
)

ENUM_NAME_COLS = 49


def render_api(api: reg.Api, pfn: bool, registry: reg.Registry, header: bool) -> str:
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
    header : bool
        True if generating a header, false if generating a C++ module.

    Returns
    -------
    str
        The C declaration.
    """
    if isinstance(api, reg.Command):
        if len(api.parameters) == 0:
            params = "(void)" if header else "()"
        else:
            params = f"({', '.join(x.repr.strip() for x in api.parameters)})"
        is_void = api.return_type.strip() == "void"
        lhs_ret = "void" if is_void else "auto"
        rhs_ret = "" if is_void else f" -> {api.return_type}"
        if pfn:
            if header:
                return (
                    f"typedef {api.return_type} ({api.namespace}_APIENTRY *"
                    f"{api.pfn_name}){params} {api.namespace}_API_NOEXCEPT17;"
                )
            else:
                return (
                    f"using {api.pfn_name} = {lhs_ret} ({api.namespace}_APIENTRY*)"
                    f"{params} {api.namespace}_API_NOEXCEPT{rhs_ret};"
                )
        else:
            export = f"{api.namespace}_API " if api.export is not None else ""
            doc = reg.render_doc_comment(api.doc, registry)
            if header:
                return (
                    f"{doc}{export}{api.return_type} "
                    f"{api.namespace}_APIENTRY {api.name}{params} {api.namespace}_API_NOEXCEPT;"
                )
            else:
                return (
                    f"{doc}{export}{lhs_ret} "
                    f"{api.namespace}_APIENTRY {api.name}{params} {api.namespace}_API_NOEXCEPT{rhs_ret};"
                )
    elif isinstance(api, reg.Enum):
        doc = reg.render_doc_comment(api.doc, registry, api.property)
        if header:
            return f"{doc}{f'#define {api.name} ':<{ENUM_NAME_COLS}}{api.value}"
        else:
            deprecated = (
                f' [[deprecated("{api.deprecated}")]] '
                if api.deprecated is not None
                else ""
            )
            return f"{doc}{f'ENUMDCL {api.name}{deprecated} = ':<{ENUM_NAME_COLS}}{api.value};"
    elif header and (isinstance(api, reg.Typedef) or isinstance(api, reg.Verbatim)):
        return f"{reg.render_doc_comment(api.doc, registry)}{api.repr}"
    elif isinstance(api, reg.Typedef):
        return (
            f"{reg.render_doc_comment(api.doc, registry)}using {api.name} = {api.type};"
        )
    elif isinstance(api, reg.Verbatim):
        if api.category == "basetype":
            print(
                f"Warning: verbatim basetype not being handled for modules today, define {api.name} in preamble"
            )
            return ""
        if api.category != "funcpointer":
            # Is it a string constant?
            define = api.repr.replace(f" {api.name} ", "").strip()
            if define.startswith("#define"):
                define = define[len("#define") :].strip()
            if define[0] == '"' and define[-1] == '"':
                return (
                    f"{f'inline constexpr auto {api.name} = ':<{ENUM_NAME_COLS}}"
                    f"std::to_array<const {api.namespace}char>({define});"
                )
            return ""
        # Function pointers are defined in the XML as typedefs, we need to convert this typedef into a using.
        type = api.repr.strip()
        if not type.startswith("typedef"):
            raise ValueError("funcpointer did not start with typedef")
        type = type[len("typedef") :].strip()
        if api.name not in type:
            raise ValueError(
                "Could not identify name component of function pointer type"
            )
        type = type.replace(api.name, "").rstrip(";")
        return_type = type[: type.index("(")].strip()
        if return_type != "void":
            type = f"auto {type[type.index('('):]} -> {return_type}"
        type = type.replace("NOEXCEPT17", "NOEXCEPT")
        return f"{reg.render_doc_comment(api.doc, registry)}using {api.name} = {type};"

    raise TypeError


def render_set(
    api_set: reg.ApiSet,
    registry: reg.Registry,
    current_file: FileToGenerate,
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

    if not current_file.is_header:
        yield f"/*** {api_set.name} ***/"
    if include_guard and current_file.is_header:
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
        and current_file.is_header
        and os.path.basename(current_file.output_file) != annex_header
    ):
        ext_header_file = os.path.join(
            os.path.dirname(__file__),
            os.path.dirname(current_file.output_file),
            annex_header,
        )
        with open(ext_header_file, "w") as f:
            external_headers = "".join(
                f"#include <{h.name}>\n"
                for h in registry.apis.values()
                if isinstance(h, reg.Include)
            )
            other_headers = "".join(
                f'#include "{os.path.basename(h.output_file)}"\n'
                for h in CONFIGURATION
                if isinstance(h, FileToGenerate)
                and h.is_header
                and h.output_file != current_file.output_file
            )
            preamble = f'{external_headers}\n{other_headers}\n#ifdef __cplusplus\nextern "C" {{\n#endif'.strip()
            f.write(
                STANDARD_HEADER_TEMPLATE.format(
                    guard=f"AL_{os.path.basename(annex_header).upper().replace('.', '_')}",
                    preamble=preamble,
                    content="\n".join(
                        render_set(
                            api_set,
                            registry,
                            FileToGenerate(
                                ext_header_file,
                                True,
                                current_file.registry_file,
                                current_file.api,
                                current_file.include,
                                current_file.exclude,
                                preamble,
                            ),
                            False,
                        )
                    ),
                )
            )
        yield f'#include "{annex_header}"'
        if include_guard and current_file.is_header:
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
                and requirement.api_specific not in current_file.api
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
                        yield f"/* Pointer-to-function types, useful for storing dynamically loaded {api.namespace} entry"
                        yield " * points."
                        yield " */"
                    written_preamble = True
                if not written_comment and should_write_comment:
                    yield f"/* {requirement.comment} */"
                    written_comment = True
                api = render_api(
                    api, pass_name == "command-pfn", registry, current_file.is_header
                )
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
    if include_guard and current_file.is_header:
        yield "#endif"
    yield ""
    for requirement in api_set.require:
        for api in requirement.apis:
            registry.written[api] = api_set.name


def create_header(registry: reg.Registry, header: FileToGenerate):
    """
    Creates a header file using the given configuration and XML registry.

    Parameters
    ----------
    registry : reg.Registry
        The registry to get API declarations from.
    header : Header
        The header generation configuration.
    """
    output_file = os.path.join(os.path.dirname(__file__), header.output_file)
    template = (
        STANDARD_HEADER_TEMPLATE if header.is_header else STANDARD_MODULE_TEMPLATE
    )
    header = template.format(
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
    for registry in set(
        x.registry_file for x in CONFIGURATION if isinstance(x, FileToGenerate)
    ):
        # Read XML relative to this file
        registry_xml = xml.etree.ElementTree.parse(
            os.path.join(os.path.dirname(__file__), registry)
        )

        # Parse
        registries[registry] = reg.Registry(registry_xml)

    for header in CONFIGURATION:
        if isinstance(header, ResetRegistryState):
            registries[header.registry_file].written = dict()
            continue
        if isinstance(header.api, str):
            header.api = [header.api]
        if header.include is None:
            header.include = [
                k
                for k, v in registries[header.registry_file].sets.items()
                if not v.is_feature == header.is_extension_only
                and (header.exclude is None or k not in header.exclude)
                and any(a in v.api for a in header.api)
            ]
        print(f"Generating {header.output_file}...")
        create_header(registries[header.registry_file], header)


if __name__ == "__main__":
    main()
