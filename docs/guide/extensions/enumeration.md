## Enumeration Extension
The Enumeration Extension enables the application developer to retrieve a list
of device strings identifying the different OpenAL rendering and capture devices
present on the user’s PC.  The OpenAL router takes care of querying the user’s
system to find valid device implementations.  Any of the strings returned by the enumeration extension can be used to create a device during
initialization via [alcOpenDevice](../context-device.md#alcopendevice).  This
extension is critical if you want to enable the user to select at run-time which
device should be used to render your OpenAL audio.   

Naturally device enumeration is a very platform-specific topic.  The mechanism
might not be implemented on platforms such as games consoles with fixed
capabilities, where multiple rendering devices are unnecessary.

Note that on PC the standard Enumeration Extension will not identify every
potential OpenAL output path.  It will not return all the possible outputs in
situations where the user has more than one audio device installed, or under
Windows Vista where the audio system specifies different “endpoints” for sound
such as Speakers, S/PDIF, etc...  If you require complete control over the
choice of output path, use the “Enumerate All” extension.

For full details on making use of the different devices you might come across on
the Windows PC platform, see the accompanying OpenAL Deployment Guide (PC
Windows).

### Detecting the Enumeration Extension
To check whether the OpenAL libraries expose the Enumeration extension, use the
OpenAL function call
[alcIsExtensionPresent](../context-extension.md#alcisextensionpresent) and the
name `"ALC_ENUMERATION_EXT"`.
```cpp
if (alcIsExtensionPresent(NULL, “ALC_ENUMERATION_EXT") == AL_TRUE) {
    // Enumeration Extension Found
}
```

### Retrieving device names
If the extension is found, the developer can retrieve a string containing
`NULL`-separated device name strings (the list is terminated with two
consecutive `NULL` characters), and a string containing the name of the default
device.

To retrieve the string listing all the devices present, the developer should use
the OpenAL function call [alcGetString](../context-state.md#alcgetstring) with
the name `"ALC_DEVICE_SPECIFIER"`.

To retrieve the string containing the name of the default device, the developer
should use the OpenAL function call
[alcGetString](../context-state.md#alcgetstring) with the name
`"ALC_DEFAULT_DEVICE_SPECIFIER"`.

```cpp
const ALCchar* devices;
const ALCchar* defaultDeviceName;

// Pass in NULL device handle to get list of devices
devices = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
// devices contains the device names, separated by NULL and terminated by two
// consecutive NULLs.

defaultDeviceName = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
// defaultDeviceName contains the name of the default device
```

### Parsing the device string
It is trivial to parse the device string and retrieve the names of the
individual devices.  Ideally these will be presented to the user in the
application configuration GUI, to enable the user to select the desired device
at initialization time.

### Checking the current device name
The developer can check to see the name of the device that was actually opened
using the function call [alcGetString](../context-state.md#alcgetstring) with a
pointer to an open device and the name `"ALC_DEVICE_SPECIFIER"`.

```cpp
ALCdevice* pMyDevice;
const ALCchar* actualDeviceName;

// Open the default device
pMyDevice=alcOpenDevice(NULL);

// Pass in valid device pointer to get the name of the open device

actualDeviceName = alcGetString(pMyDevice, ALC_DEVICE_SPECIFIER);
// actualDeviceName contains the name of the open device
```

### Enumeration Names
#### ALC_ENUMERATION_EXT
Use with [alcIsExtensionPresent](../context-extension.md#alcisextensionpresent)
to detect if the enumeration extension is available.

#### ALC_DEVICE_SPECIFIER
Use with [alcGetString](../context-state.md#alcgetstring) and a NULL device
pointer to retrieve a string containing the available device names, separated
with `NULL` characters and terminated by two consecutive `NULL` characters.

Use with [alcGetString](../context-state.md#alcgetstring) and a pointer to a
previously-opened device to ascertain the device’s name.

#### ALC_CAPTURE_DEVICE_SPECIFIER
Use with [alcGetString](../context-state.md#alcgetstring) and a `NULL` device
pointer to retrieve a string containing the available capture device names,
separated with `NULL` characters and terminated by two consecutive `NULL`
characters.

Use with [alcGetString](../context-state.md#alcgetstring) and a pointer to a
previously-opened capture device to ascertain the device’s name.

#### ALC_DEFAULT_DEVICE_SPECIFIER
Use with [alcGetString](../context-state.md#alcgetstring) with a NULL Device
identifier to retrieve a `NULL`-terminated string containing the name of the
default device.

#### ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER
Use with [alcGetString](../context-state.md#alcgetstring) with a `NULL` Device
identifier to retrieve a `NULL`-terminated string containing the name of the
default capture device.
