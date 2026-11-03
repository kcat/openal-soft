## Context Extension Functions
### Functions
* [alcIsExtensionPresent](#alcisextensionpresent)
* [alcGetProcAddress](#alcgetprocaddress)
* [alcGetEnumValue](#alcgetenumvalue)

#### alcIsExtensionPresent
##### Description
This function queries if a specified context extension is available.

```cpp
ALCboolean alcIsExtensionPresent(
    ALCdevice *device,
    const ALCchar *extName
);
```

##### Parameters
* device - A pointer to the device to be queried for an extension
* extName - A null-terminated string describing the extension

##### Possible Error States
| State             | Description |
| ----------------- | ----------- |
| ALC_INVALID_VALUE | The string pointer is not valid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns `ALC_TRUE` if the extension is available, `ALC_FALSE` if the extension
is not available.

##### See Also
[alcGetProcAddress](#alcgetprocaddress), [alcGetEnumValue](#alcgetenumvalue)

#### alcGetProcAddress
##### Description
This function retrieves the address of a specified context extension function.

```cpp
void* alcGetProcAddress(
    ALCdevice* device,
    const ALCchar* funcName
);
```

##### Parameters
* device - A pointer to the device to be queried for the function
* funcName - a null-terminated string describing the function

##### Possible Error States
| State             | Description |
| ----------------- | ----------- |
| ALC_INVALID_VALUE | The string pointer is not valid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns the address of the function, or `NULL` if it is not found.

##### See Also
[alcIsExtensionPresent](#alcisextensionpresent),
[alcGetEnumValue](#alcgetenumvalue)

#### alcGetEnumValue
##### Description
This function retrieves the enum value for a specified enumeration name.

```cpp
ALCenum alcGetEnumValue(
    ALCdevice* device,
    const ALCchar* enumName
);
```

##### Parameters
* device - A pointer to the device to be queried
* enumName - A null terminated string describing the enum value

##### Possible Error States
| State             | Description |
| ----------------- | ----------- |
| ALC_INVALID_VALUE | The string pointer is not valid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns the enum value described by the enumName string.  This is most often
used for querying an enum value for an ALC extension.

##### See Also
[alcIsExtensionPresent](#alcisextensionpresent),
[alcGetProcAddress](#alcgetprocaddress)
