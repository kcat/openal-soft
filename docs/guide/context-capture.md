## Context Capture Functions
### Functions
* [alcCaptureOpenDevice](#alccaptureopendevice)
* [alcCaptureCloseDevice](#alccaptureclosedevice)
* [alcCaptureStart](#alccapturestart)
* [alcCaptureStop](#alccapturestop)
* [alcCaptureSamples](#alccapturesamples)

#### alcCaptureOpenDevice
##### Description
This function opens a capture device by name.

```cpp
ALCdevice* alcCaptureOpenDevice(
    const ALCchar* devicename,
    ALCuint frequency,
    ALCenum format,
    ALCsizei buffersize
);
```

##### Parameters
* devicename - A pointer to a device name string
* frequency - The frequency that the data should be captured at
* format - The requested capture buffer format
* buffersize - The size of the capture buffer

##### Possible Error States
| State             | Description |
| ----------------- | ----------- |
| ALC_INVALID_VALUE | One of the parameters has an invalid value. |
| ALC_OUT_OF_MEMORY | The specified device is invalid, or can not capture audio. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
Returns the capture device pointer, or `NULL` on failure.

##### See Also
[alcCaptureCloseDevice](#alcCaptureCloseDevice)

#### alcCaptureCloseDevice
##### Description
This function closes the specified capture device.

```cpp
ALCboolean alcCaptureCloseDevice(
    ALCdevice* device
);
```

##### Parameters
* device - A pointer to a capture device

##### Possible Error States
| State              | Description |
| ------------------ | ----------- |
| ALC_INVALID_DEVICE | The specified device is not a valid capture device. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
Returns `ALC_TRUE` if the close operation was successful, `ALC_FALSE` on
failure.

##### See Also
[alcCaptureOpenDevice](#alccaptureopendevice)

#### alcCaptureStart
##### Description
This function begins a capture operation.

```cpp
void alcCaptureStart(
    ALCdevice *device
);
```

##### Parameters
* device - A pointer to a capture device

##### Possible Error States
| State              | Description |
| ------------------ | ----------- |
| ALC_INVALID_DEVICE | The specified device is not a valid capture device. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
[alcCaptureStart](#alccapturestart) will begin recording to an internal ring
buffer of the size specified when opening the capture device.  The application
can then retrieve the number of samples currently available using the
`ALC_CAPTURE_SAMPLES` token with
[alcGetIntegerv](context-state.md#alcgetintegerv). When the application
determines that enough samples are available for processing, then it can obtain
them with a call to [alcCaptureSamples](#alccapturesamples).

##### See Also
[alcCaptureStop](#alccapturestop), [alcCaptureSamples](#alccapturesamples)

#### alcCaptureStop
##### Description
This function stops a capture operation.

```cpp
void alcCaptureStop(
    ALCdevice *device
);
```

##### Parameters
* device - A pointer to a capture device

##### Possible Error States
| State              | Description |
| ------------------ | ----------- |
| ALC_INVALID_DEVICE | The specified device is not a valid capture device. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alcCaptureStart](#alccapturestart), [alcCaptureSamples](#alccapturesamples)

#### alcCaptureSamples
##### Description
This function completes a capture operation, and does not block.

```cpp
void alcCaptureSamples(
    ALCdevice* device,
    ALCvoid* buffer,
    ALCsizei samples
);
```

##### Parameters
* device - A pointer to a capture device
* buffer - A pointer to a data buffer, which must be large enough to accommodate
           samples number of samples
* samples - The number of samples to be retrieved

##### Possible Error States
| State              | Description |
| ------------------ | ----------- |
| ALC_INVALID_VALUE  | The specified number of samples is larger than the number of available samples. |
| ALC_INVALID_DEVICE | The specified device is not a valid capture device. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alcCaptureStart](#alccapturestart), [alcCaptureStop](#alccapturestop)
