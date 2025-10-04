#!/usr/bin/env python3
# -*- encoding utf-8 -*-
# Copyright (c) [2025] Dylan Perks (@Perksey) and Contributors
# SPDX-License-Identifier: Unlicense

import typing
import xml.etree.ElementTree
import dataclasses

"""
A purpose-built Khronos-style XML parser for al.xml. Full compatibility, completeness, and correctness with the official
Khronos scripts is an explicit non-goal. It is also expected that this script will not be adaptable to non-OpenAL uses.
"""

# Max columns a doc comment can take up (excluding the " * ")
DOC_MAX_COLS = 80 - 3


@dataclasses.dataclass
class Requirement:
    apis: typing.List[str]
    comment: typing.Optional[str] = None
    api_specific: typing.Optional[str] = None


@dataclasses.dataclass
class ApiSet:
    """
    A <feature> or an <extension>.

    Properties
    ----------
    is_feature : bool
        True if this API set is a feature, false if it is an extension.
    name : str
        The name of the set.
    api : list of str
        The names of the APIs (e.g. "al", "alc") that this API set is supported on. For features, this is usually just a
        single element (from the "api" attribute). For extensions, this may be multiple (from the "supported"
        attribute). Requirements may be API-specific.
    require : list of Requirement
        The APIs that are referenced by this API set.
    annex : str
        Optionally, an annex name for this feature/extension. This is used to extract this API set to a
        different header/module.
    doc : list of str, optional
        Optionally, lines of documentation about this API set.
    """

    is_feature: bool
    name: str
    api: typing.List[str]
    require: typing.List[Requirement]
    annex: typing.Optional[str] = None
    doc: typing.Optional[typing.List[str]] = None


@dataclasses.dataclass
class Parameter:
    """
    Represents a command parameter.

    Attributes
    ----------
    type : str
        The C type string.
    name : str
        The parameter name.
    repr : str
        The C representation of both the type and name.
    """

    type: str
    name: str
    repr: str


@dataclasses.dataclass
class Command:
    """
    Represents a <command> element.

    Attributes
    ----------
    name : str
        The name of the command.
    namespace : str
        The namespace in which the command resides (e.g. "AL", "ALC")
    return_type : str
        The C return type of this command.
    parameters : list of Parameter
        The parameters of this command.
    pfn_name : str
        The preferred name of the function pointer type for this command.
    export : str, optional
        The "api" value that, if matching that of the header being generated, indicates the command is exposed as a
        linkable native export.
    doc : list of str, optional
        A list of documentation lines for this command.
    deprecated : str, optional
        If present, the deprecation message for this command. If not present, this command is not deprecated.
    """

    name: str
    namespace: str
    return_type: str
    parameters: typing.List[Parameter]
    pfn_name: str
    export: typing.Optional[str] = None
    doc: typing.Optional[typing.List[str]] = None
    deprecated: typing.Optional[str] = None


@dataclasses.dataclass
class Include:
    """
    Represents a file include.

    Attributes
    ----------
    name : str
        The name of the include. This is actually the header to be included (i.e. this string is the ... within
        #include <...>)
    """

    name: str


@dataclasses.dataclass
class Property:
    """
    Additional metadata for enums that represent properties on classes. This is used to influence rendering of
    documentation comments.

    Attributes
    ----------
    on : str
        The "class" value on which this property resides.
    type : str
        The type of the property. e.g. ALuint, ALint, ALfloat, etc.
    range : str, optional
        The range of acceptable values for this property. This can be a numerical range like "0.0..=1.0" or an enum
        group name (for the latter, it's expected that the type attribute is "ALenum" or "ALCenum").
    default : str, optional
        The default value for this property.
    value_class : str, optional
        If "type" is a "ALuint" or "ALCuint", the "class" value of the handle stored in this property.
    """

    on: str
    type: str
    range: typing.Optional[str]
    default: typing.Optional[str]
    value_class: typing.Optional[str] = None


@dataclasses.dataclass
class Enum:
    """
    Represents an <enum>.

    Attributes
    ----------
    name : str
        The name of the enum.
    value : str
        The enum value. This is the RHS of the #define verbatim.
    property : Property, optional
        Additional property metadata, if applicable. See the Property class for more info.
    groups : list of str, optional
        A list of strongly-typed groups in which this enum resides.
    doc : list of str, optional
        A list of documentation lines for this enum.
    deprecated : str, optional
        If present, the deprecation message for this enum. If not present, this enum is not deprecated.
    """

    name: str
    value: str
    property: typing.Optional[Property]
    groups: typing.Optional[typing.List[str]] = None
    doc: typing.Optional[typing.List[str]] = None
    deprecated: typing.Optional[str] = None


def innertext(
    tag: xml.etree.ElementTree.Element,
    recurse_fn: typing.Optional[
        typing.Callable[[xml.etree.ElementTree.Element], bool]
    ] = None,
) -> str:
    """
    Recursively get the text representation of an element's contents. The Python built-in text property does not
    include the text within nested elements.

    Parameters
    ----------
    tag : xml.etree.ElementTree.Element
        The element to get the inner text contents of.
    recurse_fn : lambda
        An optional filter to determine which elements to recurse into.

    Returns
    -------
    str
        The inner text.
    """
    return (
        (tag.text or "")  # <-- get the text preceding nested elements.
        + "".join(
            # Concatenate the result of this function for each of the nested elements.
            innertext(e, recurse_fn)
            for e in tag
            if recurse_fn is None or recurse_fn(e)
        )
        + (tag.tail or "")  # <-- don't forget the text after the nested elements!
    )


@dataclasses.dataclass
class Typedef:
    """
    Represents a <type category="basetype"> i.e. a C typedef.

    Attributes
    ----------
    type : str
        The C type on the LHS of the typedef.
    name : str
        The name of this typedef i.e. the RHS.
    repr : str
        The C representation of this typedef verbatim.
    doc : list of str, optional
        A list of documentation lines for this typedef.
    deprecated : str, optional
        If present, the deprecation message for this typedef. If not present, this typedef is not deprecated.
    """

    type: str
    name: str
    repr: str
    doc: typing.Optional[typing.List[str]] = None
    deprecated: typing.Optional[str] = None


@dataclasses.dataclass
class Verbatim:
    """
    Represents any other type of <type>. The <type> element has always been somewhat abused in the Khronos XMLs in that
    any old verbatim C code can be shoved in there.

    Attributes
    ----------
    name : str
        The name used to refer to this verbatim C code.
    category : str
        A category string. Note that "basetype" is handled as a Typedef, see that class for more info.
    repr : str
        The verbatim C code.
    doc : list of str, optional
        A list of documentation lines for this definition.
    deprecated : str, optional
        If present, the deprecation message for this definition. If not present, this definition is not deprecated.
    """

    name: str
    category: str
    repr: str
    doc: typing.Optional[typing.List[str]] = None
    deprecated: typing.Optional[str] = None


def doc_from_element(
    element: xml.etree.ElementTree.Element,
) -> typing.Optional[typing.List[str]]:
    """
    Extracts the comments from an XML element. The "comment" attribute can be used to define a short documentation
    comment, as well as the <comment> element within the element provided. Both can be used simultaneously if desirable.

    Parameters
    ----------
    element : xml.etree.ElementTree.Element
        The element.

    Returns
    -------
    list of str or None
        The documentation lines, or None if none were found.
    """

    top_doc = element.attrib.get("comment")
    rest_of_doc = element.find("comment")
    if top_doc is None and rest_of_doc is None:
        return None
    doclines = []
    if top_doc is not None:
        doclines.append(top_doc)
        if rest_of_doc is not None:
            doclines.append("")
    if rest_of_doc is not None:
        doclines.extend(l.strip() for l in rest_of_doc.text.strip().splitlines())
    return doclines


Api = typing.Union[Command, Enum, Typedef, Verbatim]


class Registry:
    """
    Represents the OpenAL XML registry (al.xml).

    Attributes
    ----------
    apis : dict
        A mapping of API names to API declarations.
    sets : dict
        A mapping of API set names (i.e. <feature> or <extension> names) to their declarations.
    groups : dict
        A mapping of strongly-typed enum group names to the enum names contained within them.
    written : dict
        A mapping of API names that have already been output as part of generation to the API set that was being output
        that referenced the API.
    """

    # A map of API names to rendered strings.
    # Commands, enums, etc are all rolled into here given that C guarantees that none of these identifiers will collide
    apis: typing.Dict[str, Api]

    # A map of feature or extension names to API sets
    sets: typing.Dict[str, ApiSet]

    groups: typing.Dict[str, typing.List[str]]

    written: typing.Dict[str, str] = dict()

    def __init__(self, registry: xml.etree.ElementTree.ElementTree):
        """
        Parses an XML registry.

        Parameters
        ----------
        registry : xml.etree.ElementTree.ElementTree
            The parsed XML file.
        """
        self.groups = {}
        # Get all the <enum> declarations using XPath.
        for enum in registry.findall(".//enums/enum"):
            # Is this enum in any groups? Groups are comma separated in the group="..." attribute.
            for group in enum.attrib.get("group", "").split(","):
                group = group.strip()
                if group == "":
                    continue
                # If the group exists, retrieve the existing array. If not, use a new empty one.
                members = self.groups.get(group, [])
                if len(members) == 0:
                    # If the group didn't exist, make sure we track the new array we've created.
                    self.groups[group] = members
                # Add this enum to the group.
                members.append(enum.attrib["name"])

        def return_type(proto: xml.etree.ElementTree.Element) -> str:
            return innertext(proto, lambda x: x.tag != "name").strip()

        self.apis = {}
        # Get all the <type> declarations using XPath.
        for type in registry.findall(".//types/type"):
            # Find name
            name = type.attrib.get("name") or type.find("name")
            if name is None:
                print(f"Skipping: {type}")
                continue
            category = type.attrib.get("category")
            if category == "include":
                self.apis[name] = Include(name)
                continue
            if category == "basetype":
                type_deffed = innertext(type, lambda x: x.tag != "name").strip()
                if type_deffed.startswith("typedef"):
                    type_deffed = type_deffed[len("typedef") :].strip()
                self.apis[name] = Typedef(
                    type_deffed,
                    name,
                    innertext(type),
                    doc_from_element(type),
                    type.attrib.get("deprecated"),
                )
            # Types are verbatim
            self.apis[name.text.strip()] = Verbatim(
                name,
                category,
                innertext(type).strip(),
                doc_from_element(type),
                type.attrib.get("deprecated"),
            )

        # Get all the <commands> objects - we do nested iteration because we need to get the namespace attribute!
        for commands in registry.findall(".//commands"):
            namespace = commands.attrib.get("namespace", "AL")
            # Get all <command> declarations in this <commands> namespace.
            for command in commands.iter("command"):
                proto = command.find("proto")
                name = proto.find("name")
                params = command.findall("param")
                if name is None:
                    print(f"Skipping: {command}")
                    continue
                name = name.text.strip()
                self.apis[name] = Command(
                    name,
                    namespace,
                    return_type(proto),
                    [
                        Parameter(
                            innertext(p, lambda x: x.tag != "name"),
                            p.find("name").text,
                            innertext(p),
                        )
                        for p in params
                    ],
                    command.attrib.get("funcpointer", f"LP{name.upper()}"),
                    command.attrib.get("export"),
                    doc_from_element(command),
                    command.attrib.get("deprecated"),
                )

        for enum in registry.findall(".//enums/enum"):
            name = enum.attrib.get("name")
            value = enum.attrib.get("value")
            if name is None or value is None:
                continue
            value = value.strip()
            property = enum.find("property")
            if property is not None:
                property = Property(
                    property.attrib["on"],
                    property.attrib.get("type"),
                    property.attrib.get("group") or property.attrib.get("range"),
                    property.attrib.get("default"),
                    property.attrib.get("class"),
                )
            groups = enum.attrib.get("group")
            if groups is not None:
                groups = [x.strip() for x in groups.split(",")]
            self.apis[name.strip()] = Enum(
                name,
                value,
                property,
                groups,
                doc_from_element(enum),
                enum.attrib.get("deprecated"),
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
                [
                    Requirement(
                        [y.attrib["name"] for y in x if "name" in y.attrib],
                        x.attrib.get("comment"),
                        x.attrib.get("api"),
                    )
                    for x in api_set
                    if x.tag == "require"
                ],
                api_set.attrib.get("annex"),
                doc_from_element(api_set),
            )


def render_doc_comment(
    documentation: typing.Optional[typing.List[str]],
    registry: Registry,
    property: typing.Optional[Property] = None,
) -> str:
    """
    Converts the given documentation lines into a C documentation comment.

    Parameters
    ----------
    documentation : list of str, optional
        The documentation lines. If not provided, this function simply returns an empty string for ease of integration.
    registry : Registry
        The registry to use to look up enum range (group) values.
    property : Property, optional
        Information for an enum representing a class' property, if applicable.

    Returns
    -------
    str
        The C documentation comment.
    """

    if documentation is None or len(documentation) == 0:
        return ""

    def render_range(range_str: str, prefix_cols: int) -> typing.Generator[str]:
        # Is it a numeric range?
        if ".." in range_str:
            bounds = range_str.split("..")
            # Do we have two bounds (ignoring whitespace)?
            if sum(1 for x in bounds if x.strip() != "") > 1:
                # We have an upper bound.
                bounds[1] = bounds[1][1:] if "=" in bounds[1] else f"<{bounds[1][1:]}"
            else:
                # No upper bound.
                bounds[1] = ""
            yield f"{'Range: ':<{prefix_cols}}[{bounds[0]} - {bounds[1]}]"
            return
        # Otherwise, it is likely an enum range (we have handled class ranges in the outer function)
        value_range = [x.strip() for x in range_str.split(",")]
        ret = f"{'Range: ':<{prefix_cols}}"
        if len(value_range) > 1:
            # There are multiple acceptable ranges, so print them in array form.
            ret += "["
        for i, value in enumerate(value_range):
            if i == 0:
                # First acceptable value is always on the first line and doesn't need a comma.
                ret += value
            elif len(ret) + 2 + len(value) + 1 > DOC_MAX_COLS:
                # Adding the acceptable value to the current line would push us past the max columns for docs.
                # Use a new line...
                yield f"{ret},"
                ret = f"{' ' * (prefix_cols + 1)}{value}"
            else:
                # Use the current line!
                ret = f"{ret}, {value}"
        if len(value_range) > 1:
            ret += "]"
        yield ret

    # If the first line comes from the comment="..." attribute, we put this before the property info.
    # The way we can tell whether this is the case is whether the first line is followed by a blank line.
    doclines = (
        [documentation[0]]
        if documentation is not None and len(documentation) > 0
        else []
    )
    extended_docs = (
        documentation is not None
        and len(documentation) > 2
        and documentation[1].strip() == ""
    )
    if not extended_docs and documentation is not None and len(documentation) > 1:
        # It's not a blank line splitting the small comment (in the attribute) and the big comment (in the <comment>
        # block), so let's just put all the words atop the property info (if any).
        doclines.extend(documentation[1:])
        if property is not None:
            doclines.append("")
    if property is not None:
        # Figure out how many columns the right hand side of the colon should be indented by.
        cols = 0
        if property.default is not None:
            cols = len("Default: ")
        elif property.range is not None or property.value_class is not None:
            cols = len("Range: ")
        else:
            cols = len("Type: ")
        # Property type.
        if property.type is not None:
            doclines.append(f"{'Type: ':<{cols}}{property.type.replace(',', ', ')}")
        # Range of acceptable values.
        range_str = property.range
        if range_str is not None:
            # Does the range string refer to an enum group?
            gr = registry.groups.get(range_str)
            if gr is not None:
                # Yes, let's expand the enum group before going into render_range.
                range_str = ",".join(gr)
            doclines += [x for x in render_range(range_str, cols)]
        # The class of the ALuint handles.
        if property.value_class is not None:
            doclines.append(
                f"{'Range: ':<{cols}}any valid {property.value_class.title()} ID"
            )
        # Default value.
        default = property.default
        if default is not None:
            if "," in default:
                default = "{" + default.replace(",", ", ") + "}"
            doclines.append(f"{'Default: ':<{cols}}{default}")
    if extended_docs and documentation is not None:
        doclines.extend(documentation[1:])
    if len(doclines) == 0:
        return ""
    if len(doclines) == 1:
        return f"/** {doclines[0]} */\n"
    return f"/**\n{''.join(f' * {line}'.rstrip(' ') + '\n' for line in doclines)} */\n"
