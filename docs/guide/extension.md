## Extension Functions
### Functions
* [alIsExtensionPresent](#alisextensionpresent)
* [alGetProcAddress](#algetprocaddress)
* [alGetEnumValue](#algetenumvalue)

#### alIsExtensionPresent
##### Description
This function tests if a specific extension is available for the OpenAL driver.

```cpp
ALboolean alIsExtensionPresent(
    const ALchar *extname
);
```

##### Parameters
* extname - A null-terminated string describing the desired extension

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_INVALID_VALUE | The specified extension string is not a valid pointer. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns `AL_TRUE` if the extension is available, `AL_FALSE` if the extension is not
available.

##### See Also
[alGetProcAddress](#algetprocaddress), [alGetEnumValue](#algetenumvalue)

#### alGetProcAddress
##### Description
This function returns the address of an OpenAL extension function.

```cpp
void* alGetProcAddress(
    const ALchar *fname
);
```

##### Parameters
* fname - A null-terminated string containing the function name

##### Possible Error States
None

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The return value is a pointer to the specified function.  The return value will
be `NULL` if the function is not found.

##### See Also
[alIsExtensionPresent](#alisextensionpresent), [alGetEnumValue](#algetenumvalue)

#### alGetEnumValue
##### Description
This function returns the enumeration value of an OpenAL enum described by a
string.

```cpp
ALenum alGetEnumValue(
    const ALchar *ename
);
```

##### Parameters
* ename - A null-terminated string describing an OpenAL enum

##### Possible Error States
None

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns the actual `ALenum` described by a string.  Returns `NULL` if the string
doesn’t describe a valid OpenAL enum.

##### See Also
[alIsExtensionPresent](#alisextensionpresent),
[alGetProcAddress](#algetprocaddress)
