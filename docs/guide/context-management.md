## Context Management Functions
### Properties
| Property           | Data Type | Description |
| ------------------ | --------- | ----------- |
| ALC_FREQUENCY      | `i`       | Output frequency |
| ALC_MONO_SOURCES   | `i`       | Requested number of mono sources |
| ALC_STEREO_SOURCES | `i`       | Requested number of stereo sources |
| ALC_REFRESH        | `i`       | Update rate of context processing |
| ALC_SYNC           | `i`       | Flag indicating a synchronous context |

### Functions
* [alcCreateContext](#alccreatecontext)
* [alcMakeContextCurrent](#alcmakecontextcurrent)
* [alcProcessContext](#alcprocesscontext)
* [alcSuspendContext](#alcsuspendcontext)
* [alcDestroyContext](#alcdestroycontext)
* [alcGetCurrentContext](#alcgetcurrentcontext)
* [alcGetContextsDevice](#alcgetcontextsdevice)

#### alcCreateContext
##### Description
This function creates a context using a specified device.

```cpp
ALCcontext* alcCreateContext(
    ALCdevice* device,
    ALCint* attrlist
);
```

##### Parameters
* device - A pointer to a device
* attrlist - A pointer to a set of attributes:
    + `ALC_FREQUENCY`
	+ `ALC_MONO_SOURCES`
	+ `ALC_REFRESH`
	+ `ALC_STEREO_SOURCES`
	+ `ALC_SYNC`

##### Possible Error States
| State              | Description |
| ------------------ | ----------- |
| ALC_INVALID_VALUE  | An additional context can not be created for this device. |
| ALC_INVALID_DEVICE | The specified device is not a valid output device. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns a pointer to the new context (`NULL` on failure).

The attribute list can be `NULL`, or a zero terminated list of integer pairs
composed of valid ALC attribute tokens and requested values.

##### See Also
[alcDestroyContext](#alcdestroycontext),
[alcMakeContextCurrent](#alcmakecontextcurrent)

#### alcMakeContextCurrent
##### Description
This function makes a specified context the current context.

```cpp
ALCboolean alcMakeContextCurrent(
    ALCcontext *context
);
```

##### Parameters
* context - A pointer to the new context

##### Possible Error States
| State               | Description |
| ------------------- | ----------- |
| ALC_INVALID_CONTEXT | The specified context is invalid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns `ALC_TRUE` on success, or `ALC_FALSE` on failure.

##### See Also
[alcCreateContext](#alccreatecontext),
[alcDestroyContext](#alcdestroycontext)

#### alcProcessContext
##### Description
This function tells a context to begin processing.

```cpp
void alcProcessContext(
    ALCcontext *context
);
```

##### Parameters
* context - A pointer to the context

##### Possible Error States
| State               | Description |
| ------------------- | ----------- |
| ALC_INVALID_CONTEXT | The specified context is invalid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
When a context is suspended, changes in OpenAL state will be accepted but will
not be processed.  [alcSuspendContext](#alcsuspendcontext) can be used to
suspend a context, and then all the OpenAL state changes can be applied at once,
followed by a call to [alcProcessContext](#alcProcessContext) to apply all the
state changes immediately.  In some cases, this procedure may be more efficient
than application of properties in a non-suspended state.  In some
implementations, process and suspend calls are each a NOP.

##### See Also
[alcSuspendContext](#alcsuspendcontext)

#### alcSuspendContext
##### Description
This function suspends processing on a specified context.

```cpp
void alcSuspendContext(
    ALCcontext *context
);
```

##### Parameters
* context - A pointer to the context to be suspended

##### Possible Error States
| State               | Description |
| ------------------- | ----------- |
| ALC_INVALID_CONTEXT | The specified context is invalid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
When a context is suspended, changes in OpenAL state will be accepted but will
not be processed.  A typical use of [alcSuspendContext](#alcsuspendcontext)
would be to suspend a context, apply all the OpenAL state changes at once, and
then call [alcProcessContext](#alcprocesscontext) to apply all the state changes
at once.  In some cases, this procedure may be more efficient than application
of properties in a non-suspended state. In some implementations, process and
suspend calls are each a NOP.

##### See Also
[alcProcessContext](#alcprocesscontext)

#### alcDestroyContext
##### Description
This function destroys a context.

```cpp
void alcDestroyContext(
    ALCcontext *context
);
```

##### Parameters
* context - A pointer to the context

##### Possible Error States
| State               | Description |
| ------------------- | ----------- |
| ALC_INVALID_CONTEXT | The specified context is invalid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
A context which is not current can be destroyed at any time (all sources within
that context will also be deleted).
[alcMakeContextCurrent](#alcmakecontextcurrent) should be used to make sure the
context to be destroyed is not current (`NULL` is valid for
[alcMakeContextCurrent](#alcmakecontextcurrent)).

##### See Also
[alcCreateContext](#alccreatecontext),
[alcMakeContextCurrent](#alcmakecontextcurrent)

#### alcGetCurrentContext
##### Description
This function retrieves the current context.

```cpp
ALCcontext* alcGetCurrentContext(ALCvoid);
```

##### Parameters
None

##### Possible Error States
None

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns a pointer to the current context.

##### See Also
[alcGetContextsDevice](#alcgetcontextsdevice)

#### alcGetContextsDevice
##### Description
This function retrieves a context's device pointer.

```cpp
ALCdevice* alcGetContextsDevice(ALCcontext *context);
```

##### Parameters
* context - A pointer to a context

##### Possible Error States
| State               | Description |
| ------------------- | ----------- |
| ALC_INVALID_CONTEXT | The specified context is invalid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns a pointer to the specified context's device.

##### See Also
[alcGetCurrentContext](#alcgetcurrentcontext)
