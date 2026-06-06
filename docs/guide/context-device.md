## Context Device Functions
### Functions
* [alcOpenDevice](#alcopendevice)
* [alcCloseDevice](#alcclosedevice)

#### alcOpenDevice
##### Description
This function opens a device by name.

```cpp
ALCdevice* alcOpenDevice(
    const ALCchar* devicename
);
```

##### Parameters
* devicename - A null-terminated string describing a device

##### Possible Error States
The return value will be `NULL` if there is an error.

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns a pointer to the opened device.  Will return `NULL` if a device can not be opened.

##### See Also
[alcCloseDevice](#alcclosedevice)

#### alcCloseDevice
##### Description
This function closes an opened device.

```cpp
ALCboolean alcCloseDevice(
    ALCdevice* device
);
```

##### Parameters
* device - A pointer to an opened device

##### Possible Error States
| State              | Description |
| ------------------ | ----------- |
| ALC_INVALID_DEVICE | The specified device name doesn't exist. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
`ALC_TRUE` will be returned on success or `ALC_FALSE` on failure.  Closing a
device will fail if the device contains any contexts or buffers.

##### See Also
[alcOpenDevice](#alcopendevice)
