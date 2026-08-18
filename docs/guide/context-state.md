## Context State Functions
### Functions
* [alcGetString](#alcgetstring)
* [alcGetIntegerv](#alcgetintegerv)

#### alcGetString
##### Description
This function returns pointers to strings related to the context.

```cpp
const ALCchar * alcGetString(
    ALCdevice *device,
    ALenum param
);
```

##### Parameters
* device - A pointer to the device to be queried
* param - An attribute to be retrieved:
    + `ALC_DEFAULT_DEVICE_SPECIFIER`
    + `ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER`
    + `ALC_DEVICE_SPECIFIER`
    + `ALC_CAPTURE_DEVICE_SPECIFIER`
    + `ALC_EXTENSIONS`

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| ALC_INVALID_ENUM | The specified parameter is not valid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
`ALC_DEFAULT_DEVICE_SPECIFIER` will return the name of the default output
device.

`ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER` will return the name of the default
capture device.

`ALC_DEVICE_SPECIFIER` will return the name of the specified output device if a
pointer is supplied, or will return a list of all available devices if a `NULL`
device pointer is supplied.  A list is a pointer to a series of strings
separated by `NULL` characters, with the list terminated by two `NULL`
characters.  See Enumeration Extension for more details.

`ALC_CAPTURE_DEVICE_SPECIFIER` will return the name of the specified capture
device if a pointer is supplied, or will return a list of all available devices
if a `NULL` device pointer is supplied.

`ALC_EXTENSIONS` returns a list of available context extensions, with each
extension separated by a space and the list terminated by a `NULL` character.

#### alcGetIntegerv
##### Description
This function returns integers related to the context.

```cpp
void alcGetIntegerv(
    ALCdevice* device,
    ALCenum param,
    ALCsizei size,
    ALCint* data
);
```

##### Parameters
* device - A pointer to the device to be queried
* param - An attribute to be retrieved:
    + `ALC_MAJOR_VERSION`
    + `ALC_MINOR_VERSION`
    + `ALC_ATTRIBUTES_SIZE`
    + `ALC_ALL_ATTRIBUTES`
* size - The size of the destination buffer provided
* data - A pointer to the data to be returned

##### Possible Error States
| State               | Description |
| ------------------- | ----------- |
| ALC_INVALID_VALUE   | The specified data pointer or size is not valid. |
| ALC_INVALID_ENUM    | The specified parameter is not valid. |
| ALC_INVALID_DEVICE  | The specified device is not valid. |
| ALC_INVALID_CONTEXT | The specified context is not valid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The versions returned refer to the specification version that the implementation
meets.
