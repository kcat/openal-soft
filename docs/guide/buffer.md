## Buffer Functions
### Properties

| Property     | Data Type | Description |
| ------------ | --------- | ----------- |
| AL_FREQUENCY | `i`, `iv` | Frequency of buffer in Hz |
| AL_BITS      | `i`, `iv` | Bit depth of buffer |
| AL_CHANNELS  | `i`, `iv` | Number of channels in buffer. > 1 is valid, but buffer won’t be positioned when played |
| AL_SIZE      | `i`, `iv` | Size of buffer in bytes |
| AL_DATA      | `i`, `iv` | Original location where data was copied from generally useless, as was probably freed after buffer creation |

### Functions
* [alGenBuffers](#algenbuffers)
* [alDeleteBuffers](#aldeletebuffers)
* [alIsBuffer](#alisbuffer)
* [alBufferData](#albufferdata)
* [alBufferf](#albufferf)
* [alBuffer3f](#albuffer3f)
* [alBufferfv](#albufferfv)
* [alBufferi](#albufferi)
* [alBuffer3i](#albuffer3i)
* [alBufferiv](#albufferiv)
* [alGetBufferf](#algetbufferf)
* [alGetBuffer3f](#algetbuffer3f)
* [alGetBufferfv](#algetbufferfv)
* [alGetBufferi](#algetbufferi)
* [alGetBuffer3i](#algetbuffer3i)
* [alGetBufferiv](#algetbufferiv)

#### alGenBuffers
##### Description
This function generates one or more buffers, which contain audio data (see
[alBufferData](#albufferdata)).  References to buffers are `ALuint` values,
which are used wherever a buffer reference is needed (in calls such as
[alDeleteBuffers](#aldeletebuffers), [alSourcei](source.md#alsourcei),
[alSourceQueueBuffers](source.md#alsourcequeuebuffers), and
[alSourceUnqueueBuffers](source.md#alsourceunqueuebuffers)).

```cpp
void alGenBuffers(
    ALsizei n,
    ALuint *buffers
);
```

##### Parameters
* n - The number of buffers to be generated
* buffers - Pointer to an array of ALuint values which will store the names of
            the new buffers

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_INVALID_VALUE | The buffer array isn't large enough to hold the number of buffers requested. |
| AL_OUT_OF_MEMORY | There is not enough memory available to generate all the buffers requested. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
If the requested number of buffers cannot be created, an error will be generated
which can be detected with [alGetError](error.md#algeterror).  If an error
occurs, no buffers will be generated.  If `n` equals zero,
[alGenBuffers](#algenbuffers) does nothing and does not return an error.

##### See Also
[alDeleteBuffers](#aldeletebuffers), [alIsBuffer](#alisbuffer)

#### alDeleteBuffers
##### Description
This function deletes one or more buffers, freeing the resources used by the
buffer.  Buffers which are attached to a source can not be deleted.  See
[alSourcei](source.md#alsourcei) and
[alSourceUnqueueBuffers](source.md#alsourceunqueuebuffers) for information on
how to detach a buffer from a source.

```cpp
void alDeleteBuffers(
    ALsizei n,
    ALuint *buffers
);
```

##### Parameters
* n - The number of buffers to be deleted
* buffers - Pointer to an array of buffer names identifying the buffers to be
            deleted

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_OPERATION | The buffer is still in use and can not be deleted. |
| AL_INVALID_NAME      | A buffer name is invalid. |
| AL_INVALID_VALUE     | The requested number of buffers can not be deleted. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
If the requested number of buffers cannot be deleted, an error will be generated
which can be detected with [alGetError](error.md#algeterror).  If an error
occurs, no buffers will be deleted.  If `n` equals zero,
[alDeleteBuffers](#alDeleteBuffers) does nothing and will not return an error.

##### See Also
[alGenBuffers](#alGenBuffers), [alIsBuffer](#alIsBuffer)

#### alIsBuffer
##### Description
This function tests if a buffer name is valid, returning `AL_TRUE` if valid,
`AL_FALSE` if not.

```cpp
ALboolean alIsBuffer(
    ALuint buffer
);
```

##### Parameters
* buffer - A buffer name to be tested for validity

##### Possible Error States
None

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The `NULL` buffer is always valid (see [alSourcei](source.md#alsourcei) for
information on how the NULL `buffer` is used).

##### See Also
[alGenBuffers](#alGenBuffers), [alDeleteBuffers](#aldeletebuffers)

#### alBufferData
##### Description
This function fills a buffer with audio data.  All the pre-defined formats are
PCM data, but this function may be used by extensions to load other data types
as well.

```cpp
void alBufferData(
    ALuint buffer,
    ALenum format,
    const ALvoid *data,
    ALsizei size,
    ALsizei freq
);
```

##### Parameters
* buffer - Buffer name to be filled with data
* format - Format type from among the following:
    + `AL_FORMAT_MONO8`
	+ `AL_FORMAT_MONO16`
	+ `AL_FORMAT_STEREO8`
	+ `AL_FORMAT_STEREO16`
* data - Pointer to the audio data
* size - The size of the data in bytes
* freq - The frequency of the audio data

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_OUT_OF_MEMORY | There is not enough memory available to create this buffer. |
| AL_INVALID_VALUE | The size parameter is not valid for the format specified, the buffer is in use, or the data is a `NULL` pointer. |
| AL_INVALID_ENUM  | The specified format does not exist. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
8-bit PCM data is expressed as an unsigned value over the range 0 to 255, 128
being an audio output level of zero.  16-bit PCM data is expressed as a signed
value over the range -32768 to 32767, 0 being an audio output level of zero.
Stereo data is expressed in interleaved format, left channel first.  Buffers
containing more than one channel of data will be played without 3D
spatialization.

#### alBufferf
##### Description
This function sets a floating point property of a buffer.

```cpp
void alBufferf(
    ALuint buffer,
    ALenum param,
    ALfloat value
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be set
* value - The `ALfloat` value to be set

##### Possible Error States
| State           | Description |
| --------------- | ----------- |
| AL_INVALID_ENUM | The specified parameter is not valid. |
| AL_INVALID_NAME | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBuffer3f](#alBuffer3f), [alBufferfv](#alBufferfv),
[alGetBufferf](#alGetBufferf), [alGetBuffer3f](#alGetBuffer3f),
[alGetBufferfv](#alGetBufferfv)

#### alBuffer3f
##### Description
This function sets a floating point property of a buffer.

```cpp
void alBuffer3f(
    ALuint buffer,
    ALenum param,
    ALfloat v1,
    ALfloat v2,
    ALfloat v3
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be set
* v1, v2, v3 - The `ALfloat` values to be set

##### Possible Error States
| State           | Description |
| --------------- | ----------- |
| AL_INVALID_ENUM | The specified parameter is not valid. |
| AL_INVALID_NAME | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBufferf](#alBufferf), [alBufferfv](#alBufferfv),
[alGetBufferf](#alGetBufferf), [alGetBuffer3f](#alGetBuffer3f),
[alGetBufferfv](#alGetBufferfv)

#### alBufferfv
##### Description
This function sets a floating point property of a buffer.

```cpp
void alBuffer3f(
    ALuint buffer,
    ALenum param,
    ALfloat* value
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be set
* values - A pointer to the `ALfloat` values to be set

##### Possible Error States
| State           | Description |
| --------------- | ----------- |
| AL_INVALID_ENUM | The specified parameter is not valid. |
| AL_INVALID_NAME | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBufferf](#alBufferf), [alBuffer3f](#alBuffer3f),
[alGetBufferf](#alGetBufferf), [alGetBuffer3f](#alGetBuffer3f),
[alGetBufferfv](#alGetBufferfv)

#### alBufferi
##### Description
This function sets an integer property of a buffer.

```cpp
void alBufferi(
    ALuint buffer,
    ALenum param,
    ALint value
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be set
* values - The `ALint` value to be set

##### Possible Error States
| State           | Description |
| --------------- | ----------- |
| AL_INVALID_ENUM | The specified parameter is not valid. |
| AL_INVALID_NAME | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBuffer3i](#alBuffer3i), [alBufferiv](#alBufferiv),
[alGetBufferi](#alGetBufferf), [alGetBuffer3i](#alGetBuffer3i),
[alGetBufferiv](#alGetBufferiv)

#### alBuffer3i
##### Description
This function sets an integer property of a buffer.

```cpp
void alBuffer3i(
    ALuint buffer,
    ALenum param,
    ALint v1,
    ALint v2,
    ALint v3,
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be set
* v1, v2, v3 - The `ALint` values to be set

##### Possible Error States
| State           | Description |
| --------------- | ----------- |
| AL_INVALID_ENUM | The specified parameter is not valid. |
| AL_INVALID_NAME | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBufferi](#alBufferi), [alBufferiv](#alBufferiv),
[alGetBufferi](#alGetBufferf), [alGetBuffer3i](#alGetBuffer3i),
[alGetBufferiv](#alGetBufferiv)

#### alBufferiv
##### Description
This function sets an integer property of a buffer.

```cpp
void alBufferiv(
    ALuint buffer,
    ALenum param,
    ALint* values
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be set
* values - A pointer to the `ALint` values to be set

##### Possible Error States
| State           | Description |
| --------------- | ----------- |
| AL_INVALID_ENUM | The specified parameter is not valid. |
| AL_INVALID_NAME | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBufferi](#alBufferi), [alBuffer3i](#alBuffer3i),
[alGetBufferi](#alGetBufferf), [alGetBuffer3i](#alGetBuffer3i),
[alGetBufferiv](#alGetBufferiv)

#### alGetBufferf
##### Description
This function retrieves a floating point property of a buffer.

```cpp
void alGetBufferf(
    ALuint buffer,
    ALenum pname,
    ALfloat* value
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be retrieved
* value - A pointer to an `ALfloat` to hold the retrieved data

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_INVALID_ENUM  | The specified parameter is not valid. |
| AL_INVALID_NAME  | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |
| AL_INVALID_VALUE | The specified value pointer is not valid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBufferf](#alBufferf), [alBuffer3f](#alBuffer3f),
[alBufferfv](#alBufferfv), [alGetBuffer3f](#alGetBuffer3f),
[alGetBufferfv](#alGetBufferfv)

#### alGetBuffer3f
##### Description
This function retrieves a floating point property of a buffer.

```cpp
void alGetBuffer3f(
    ALuint buffer,
    ALenum pname,
    ALfloat* v1,
	ALfloat* v2,
	ALfloat* v3
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be retrieved
* v1, v2, v3 - Pointers to `ALfloat` values to hold the retrieved data

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_INVALID_ENUM  | The specified parameter is not valid. |
| AL_INVALID_NAME  | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |
| AL_INVALID_VALUE | The specified value pointer is not valid. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBufferf](#alBufferf), [alBuffer3f](#alBuffer3f),
[alBufferfv](#alBufferfv), [alGetBufferf](#alGetBufferf),
[alGetBufferfv](#alGetBufferfv)

#### alGetBufferfv
##### Description
This function retrieves a floating point property of a buffer.

```cpp
void alGetBufferfv(
    ALuint buffer,
    ALenum pname,
    ALfloat* values
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be retrieved
* values - Pointer to an `ALfloat` vector to hold the retrieved data

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_INVALID_ENUM  | The specified parameter is not valid. |
| AL_INVALID_NAME  | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |
| AL_INVALID_VALUE | The specified value pointer is not valid. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
affected by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBufferf](#alBufferf), [alBuffer3f](#alBuffer3f),
[alBufferfv](#alBufferfv), [alGetBufferf](#alGetBufferf),
[alGetBuffer3f](#alGetBuffer3f)

#### alGetBufferi
##### Description
This function retrieves an integer property of a buffer.

```cpp
void alGetBufferi(
    ALuint buffer,
    ALenum pname,
    ALint* value
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be retrieved:
	+ `AL_FREQUENCY`
	+ `AL_BITS`
	+ `AL_CHANNELS`
	+ `AL_SIZE`
	+ `AL_DATA`
* value - Pointer to an `ALint` to hold the retrieved data

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_INVALID_ENUM  | The specified parameter is not valid. |
| AL_INVALID_NAME  | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |
| AL_INVALID_VALUE | The specified value pointer is not valid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alBufferi](#alBufferi), [alBuffer3i](#alBuffer3i),
[alBufferiv](#alBufferiv), [alGetBuffer3i](#alGetBuffer3i),
[alGetBufferiv](#alGetBufferiv)

#### alGetBuffer3i
##### Description
This function retrieves an integer property of a buffer.

```cpp
void alGetBuffer3i(
    ALuint buffer,
    ALenum pname,
    ALint* v1,
	ALint* v2,
	ALint* v3
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be retrieved
* v1, v2, v3 - Pointer to `ALint` values to hold the retrieved data

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_INVALID_ENUM  | The specified parameter is not valid. |
| AL_INVALID_NAME  | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |
| AL_INVALID_VALUE | The specified value pointer is not valid. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
There are no relevant buffer properties defined in OpenAL 1.1 which can be
retrieved by this call, but this function may be used by OpenAL extensions.

##### See Also
[alBufferi](#alBufferi), [alBuffer3i](#alBuffer3i),
[alBufferiv](#alBufferiv), [alGetBufferi](#alGetBufferi),
[alGetBufferiv](#alGetBufferiv)

#### alGetBufferiv
##### Description
This function retrieves an integer property of a buffer.

```cpp
void alGetBufferiv(
    ALuint buffer,
    ALenum pname,
    ALint* values
);
```

##### Parameters
* buffer - Buffer name whose attribute is being retrieved
* param - The name of the attribute to be retrieved:
    + `AL_FREQUENCY`
	+ `AL_BITS`
	+ `AL_CHANNELS`
	+ `AL_SIZE`
	+ `AL_DATA`
* values - Pointer to an `ALint` vector to hold the retrieved data

##### Possible Error States
| State            | Description |
| ---------------- | ----------- |
| AL_INVALID_ENUM  | The specified parameter is not valid. |
| AL_INVALID_NAME  | The specified buffer doesn't have parameters (the `NULL` buffer), or doesn't exist. |
| AL_INVALID_VALUE | The specified value pointer is not valid. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alBufferi](#alBufferi), [alBuffer3i](#alBuffer3i),
[alBufferiv](#alBufferiv), [alGetBufferi](#alGetBufferi),
[alGetBuffer3i](#alGetBuffer3i)
