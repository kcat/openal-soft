## Source Functions
### Properties
| Property              | Data Type              | Description |
| --------------------- | ---------------------- | ----------- |
| AL_PITCH              | `f`, `fv`              | Pitch multiplier. Always positive |
| AL_GAIN               | `f`, `fv`              | Source gain. Value should be positive |
| AL_MAX_DISTANCE       | `f`, `fv`, `i`, `iv`   | Used with the Inverse Clamped Distance Model to set the distance where there will no longer be any attenuation of the source |
| AL_ROLLOFF_FACTOR     | `f`, `fv`, `i`, `iv`   | The rolloff rate for the source. Default is 1.0 |
| AL_REFERENCE_DISTANCE | `f`, `fv`, `i`, `iv`   | The distance under which the volume for the source would normally drop by half (before being influenced by rolloff factor or `AL_MAX_DISTANCE`) |
| AL_MIN_GAIN           | `f`, `fv`              | The minimum gain for this source |
| AL_MAX_GAIN           | `f`, `fv`              | The maximum gain for this source |
| AL_CONE_OUTER_GAIN    | `f`, `fv`              | The gain when outside the oriented cone |
| AL_CONE_INNER_ANGLE   | `f`, `fv`, `i`, `iv`   | The gain when inside the oriented cone |
| AL_CONE_OUTER_ANGLE   | `f`, `fv`, `i`, `iv`   | Outer angle of the sound cone, in degrees. Default is 360 |
| AL_POSITION           | `fv`, `3f`             | X, Y, Z position |
| AL_VELOCITY           | `fv`, `3f`             | Velocity vector |
| AL_DIRECTION          | `fv`, `3f`, `iv`, `3i` | Direction vector |
| AL_SOURCE_RELATIVE    | `i`, `iv`              | Determines if the positions are relative to the listener. Default is `AL_FALSE` |
| AL_SOURCE_TYPE        | `i`, `iv`              | The source type: `AL_UNDETERMINED`, `AL_STATIC` or `AL_STREAMING` |
| AL_LOOPING            | `i`, `iv`              | Turns looping on (`AL_TRUE`) or off (`AL_FALSE`) |
| AL_BUFFER             | `i`, `iv`              | The ID of the attached buffer |
| AL_SOURCE_STATE       | `i`, `iv`              | The state of the source: `AL_STOPPED`, `AL_PLAYING`, ... |
| AL_BUFFERS_QUEUED     | `i`, `iv`              | The number of buffers queued on this source |
| AL_BUFFERS_PROCESSED  | `i`, `iv`              | The number of buffers in the queue that have been processed |
| AL_SEC_OFFSET         | `f`, `fv`, `i`, `iv`   | The playback position, expressed in seconds |
| AL_SAMPLE_OFFSET      | `f`, `fv`, `i`, `iv`   | The playback position, expressed in samples |
| AL_BYTE_OFFSET        | `f`, `fv`, `i`, `iv`   | The playback position, expressed in bytes |

### Functions
* [alGenSources](#algensources)
* [alDeleteSources](#aldeletesources)
* [alIsSource](#alissource)
* [alSourcef](#alsourcef)
* [alSource3f](#alsource3f)
* [alSourcefv](#alsourcefv)
* [alSourcei](#alsourcei)
* [alSource3i](#alsource3i)
* [alSourceiv](#alsourceiv)
* [alGetSourcef](#algetsourcef)
* [alGetSource3f](#algetsource3f)
* [alGetSourcefv](#algetsourcefv)
* [alGetSourcei](#algetsourcei)
* [alGetSource3i](#algetsource3i)
* [alGetSourceiv](#algetsourceiv)
* [alSourcePlay](#alsourceplay)
* [alSourcePlayv](#alsourceplayv)
* [alSourcePause](#alsourcepause)
* [alSourcePausev](#alsourcepausev)
* [alSourceStop](#alsourcestop)
* [alSourceStopv](#alsourcestopv)
* [alSourceRewind](#alsourcerewind)
* [alSourceRewindv](#alsourcerewindv)
* [alSourceQueueBuffers](#alsourcequeuebuffers)
* [alSourceUnqueueBuffers](#alsourceunqueuebuffers)

#### alGenSources
##### Description
This function generates one or more sources.   References to sources are
`ALuint` values, which are used wherever a source reference is needed (in calls
such as [alDeleteSources](#aldeletesources) and [alSourcei](#alsourcei)).

```cpp
void alGenSources(
    ALsizei n,
    ALuint *sources
);
```

##### Parameters
* n - The number of sources to be generated
* sources - Pointer to an array of `ALuint` vlaues which will store the names of
            the new sources

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_OUT_OF_MEMORY     | There is not enough memory to generate all the requested sources. |
| AL_INVALID_VALUE     | There are not enough non-memory resources to create all the requested sources, or the array pointer is not valid. |
| AL_INVALID_OPERATION | There is no context to create sources in. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
If the requested number of sources cannot be created, an error will be generated
which can be detected with [alGetError](error.md#algeterror).  If an error
occurs, no sources will be generated.  If `n` equals zero,
[alGenSources](#algensources) does nothing and does not return an error.

##### See Also
[alDeleteSources](#aldeletesources), [alIsSource](#alissource)

#### alDeleteSources
##### Description
This function deletes one or more sources.

```cpp
void alDeleteSources(
    ALsizei n,
    ALuint *sources
);
```

##### Parameters
* n - The number of sources to be deleted
* sources - Pointer to an array of source names identifying the sources to be
            deleted

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_NAME      | At least one specified source is not valid, or an attempt is being made to delete more sources than exist. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
If the requested number of sources cannot be deleted, an error will be generated
which can be detected with [alGetError](error.md#algeterror).  If an error
occurs, no sources will be deleted.  If `n` equals zero,
[alDeleteSources](#aldeletesources) does nothing and will not return an error.

A playing source can be deleted – the source will be stopped and then deleted.

##### See Also
[alGenSources](#algensources), [alIsSource](#alissource)

#### alIsSource
##### Description
This function tests if a source name is valid, returning `AL_TRUE` if valid and
`AL_FALSE` if not.

```cpp
boolean alIsSource(
    ALuint source
);
```

##### Parameters
* source - A source name to be tested for validity

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alGenSources](#algensources), [alDeleteSources](#aldeletesources)

#### alSourcei
##### Description
This function sets an integer property of a source.

```cpp
void alSourcef(
    ALuint source,
    ALenum param,
    ALfloat value
);
```

##### Parameters
* source - Source name whose attribute is being set
* param - The name of the attribute to set:
    + `AL_PITCH`
    + `AL_GAIN`
    + `AL_MIN_GAIN`
    + `AL_MAX_GAIN`
    + `AL_MAX_DISTANCE`
    + `AL_ROLLOFF_FACTOR`
    + `AL_CONE_OUTER_GAIN`
    + `AL_CONE_INNER_ANGLE`
    + `AL_CONE_OUTER_ANGLE`
    + `AL_REFERENCE_DISTANCE`
* value - The value to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is out of range. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alSource3f](#alsource3f), [alSourcefv](#alSourcefv),
[alGetSourcef](#algetsourcef), [alGetSource3f](#algetsource3f),
[alGetSourcefv](#algetsourcefv)

#### alSource3f
##### Description
This function sets a source property requiring three floating point values.

```cpp
void alSource3f(
    ALuint source,
    ALenum param,
    ALfloat v1,
	ALfloat v2,
	ALfloat v3
);
```

##### Parameters
* source - Source name whose attribute is being set
* param - The name of the attribute to set:
    + `AL_POSITION`
    + `AL_VELOCITY`
    + `AL_DIRECTION`
* v1, v2, v3 - The three `ALfloat` values to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is out of range. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
This function is an alternative to [alSourcefv](3alsourcefv).

##### See Also
[alSourcef](#alsourcef), [alSourcefv](#alSourcefv),
[alGetSourcef](#algetsourcef), [alGetSource3f](#algetsource3f),
[alGetSourcefv](#algetsourcefv)

#### alSourcefv
##### Description
This function sets a floating point-vector property of a source.

```cpp
void alSourcefv(
    ALuint source,
    ALenum param,
    ALfloat* values
);
```

##### Parameters
* source - Source name whose attribute is being set
* param - The name of the attribute to set:
    + `AL_POSITION`
    + `AL_VELOCITY`
    + `AL_DIRECTION`
* values - A pointer to the vector to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is out of range. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
This function is an alternative to [alSource3f](#alsource3f).

##### See Also
[alSourcef](#alsourcef), [alSource3f](#alSource3f),
[alGetSourcef](#algetsourcef), [alGetSource3f](#algetsource3f),
[alGetSourcefv](#algetsourcefv)

#### alSourcei
##### Description
This function sets an integer property of a source.

```cpp
void alSourcei(
    ALuint source,
    ALenum param,
    ALint value
);
```

##### Parameters
* source - Source name whose attribute is being set
* param - The name of the attribute to set:
    + `AL_SOURCE_RELATIVE`
    + `AL_CONE_INNER_ANGLE`
    + `AL_CONE_OUTER_ANGLE`
    + `AL_LOOPING`
    + `AL_BUFFER`
    + `AL_SOURCE_STATE`
* value - The value to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is out of range. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The buffer name zero is reserved as a “NULL Buffer" and is accepted by
`alSourcei(…, AL_BUFFER, …)` as a valid buffer of zero length.  The `NULL`
Buffer is extremely useful for detaching buffers from a source which were
attached using this call or with [alSourceQueueBuffers](#alsourcequeuebuffers).

##### See Also
[alSource3i](#alsource3i), [alSourceiv](#alSourceiv),
[alGetSourcei](#algetsourcei), [alGetSource3i](#algetsource3i),
[alGetSourceiv](#algetsourceiv)

#### alSource3i
##### Description
This function sets a source property requiring three integer values.

```cpp
void alSource3i(
    ALuint source,
    ALenum param,
    ALint v1,
	ALint v2,
	ALint v3
);
```

##### Parameters
* source - Source name whose attribute is being set
* param - The name of the attribute to set:
    + `AL_POSITION`
    + `AL_VELOCITY`
    + `AL_DIRECTION`
* v1, v2, v3 - The three `ALint` values to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is out of range. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alSourcei](#alsourcei), [alSourceiv](#alSourceiv),
[alGetSourcei](#algetsourcei), [alGetSource3i](#algetsource3i),
[alGetSourceiv](#algetsourceiv)

#### alSourceiv
##### Description
This function sets an integer-vector property of a source.

```cpp
void alSourceiv(
    ALuint source,
    ALenum param,
    ALint* values
);
```

##### Parameters
* source - Source name whose attribute is being set
* param - The name of the attribute to set:
    + `AL_POSITION`
    + `AL_VELOCITY`
    + `AL_DIRECTION`
* values - A pointer to the vector to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is out of range. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alSourcei](#alsourcei), [alSource3i](#alSource3i),
[alGetSourcei](#algetsourcei), [alGetSource3i](#algetsource3i),
[alGetSourceiv](#algetsourceiv)

#### alGetSourcei
##### Description
This function sets an integer property of a source.

```cpp
void alGetSourcei(
    ALuint source,
    ALenum param,
    ALint* value
);
```

##### Parameters
* source - Source name whose attribute is being retrieved
* param - The name of the attribute to retrieve:
    + `AL_SOURCE_RELATIVE`
    + `AL_BUFFER`
    + `AL_SOURCE_STATE`
    + `AL_BUFFERS_QUEUED`
    + `AL_BUFFERS_PROCESSED`
* values - A pointer to the integer value being retrieved

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value pointer given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alSourcei](#alsourcei), [alSource3i](#alSource3i),
[alSourceiv](#algetsourceiv), [alGetSource3i](#algetsource3i),
[alGetSourceiv](#algetsourceiv)

#### alGetSource3i
##### Description
This function retrieves three integer values representing a property of a source.

```cpp
void alGetSource3i(
    ALuint source,
    ALenum param,
    ALint* v1,
	ALint* v2,
	ALint* v3
);
```

##### Parameters
* source - Source name whose attribute is being retrieved
* param - The name of the attribute to retrieve:
    + `AL_POSITION`
    + `AL_VELOCITY`
    + `AL_DIRECTION`
* values - Pointer to the values to retrieve

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value pointer given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alSourcei](#alsourcei), [alSource3i](#alSource3i),
[alSourceiv](#algetsourceiv), [alGetSourcei](#algetsourcei),
[alGetSourceiv](#algetsourceiv)

#### alGetSourceiv
##### Description
This function retrieves an integer property of a source.

```cpp
void alGetSourceiv(
    ALuint source,
    ALenum param,
    ALint* values
);
```

##### Parameters
* source - Source name whose attribute is being retrieved
* param - The name of the attribute to retrieve:
    + `AL_POSITION`
    + `AL_VELOCITY`
    + `AL_DIRECTION`
* values - Pointer to the vector to retrieve

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value pointer given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alSourcei](#alsourcei), [alSource3i](#alSource3i),
[alSourceiv](#algetsourceiv), [alGetSourcei](#algetsourcei),
[alGetSource3i](#algetsource3i)

#### alSourcePlay
##### Description
This function plays a source.

```cpp
void alSourcePlay(
    ALuint source
);
```

##### Parameters
* source - The name of the source to be played

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The playing source will have its state changed to `AL_PLAYING`. When called on a
source which is already playing, the source will restart at the beginning. When
the attached buffer(s) are done playing, the source will progress to the
`AL_STOPPED` state.

##### See Also
[alSourcePlayv](#alsourceplayv), [alSourcePause](#alsourcepause),
[alSourcePausev](#alsourcepausev), [alSourceRewind](#alsourcerewind),
[alSourceRewindv](#alsourcerewindv), [alSourceStop](#alsourcestop),
[alsourcestopv](#alsourcestopv)

#### alSourcePlayv
##### Description
This function plays a set of sources.

```cpp
void alSourcePlayv(
	ALsizei n,
    ALuint *sources
);
```

##### Parameters
* n - The number of sources to be played
* source - A pointer to an array of sources to be played

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value pointer given is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The playing sources will have their state changed to `AL_PLAYING`. When called
on a source which is already playing, the source will restart at the beginning.
When the attached buffer(s) are done playing, the source will progress to the
`AL_STOPPED` state.

##### See Also
[alSourcePlay](#alsourceplay), [alSourcePause](#alsourcepause),
[alSourcePausev](#alsourcepausev), [alSourceRewind](#alsourcerewind),
[alSourceRewindv](#alsourcerewindv), [alSourceStop](#alsourcestop),
[alsourcestopv](#alsourcestopv)

#### alSourcePause
##### Description
This function pauses a source.

```cpp
void alSourcePause(
    ALuint source
);
```

##### Parameters
* source - The name of the source to be paused

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The paused source will have its state changed to `AL_PAUSED`.

##### See Also
[alSourcePlay](#alsourceplay), [alSourcePlayv](#alsourceplayv),
[alSourcePausev](#alsourcepausev), [alSourceRewind](#alsourcerewind),
[alSourceRewindv](#alsourcerewindv), [alSourceStop](#alsourcestop),
[alsourcestopv](#alsourcestopv)

#### alSourcePausev
##### Description
This function pauses a set of sources.

```cpp
void alSourcePausev(
	ALsizei n,
    ALuint *sources
);
```

##### Parameters
* n - The number of sources to be paused
* source - A pointer to an array of sources to be paused

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value pointer given is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The paused sources will have their state changed to `AL_PAUSED`.

##### See Also
[alSourcePlay](#alsourceplay), [alSourcePlayv](#alsourceplayv),
[alSourcePause](#alsourcepause), [alSourceRewind](#alsourcerewind),
[alSourceRewindv](#alsourcerewindv), [alSourceStop](#alsourcestop),
[alsourcestopv](#alsourcestopv)

#### alSourceStop
##### Description
This function stops a source.

```cpp
void alSourceStop(
    ALuint source
);
```

##### Parameters
* source - The name of the source to be stopped

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The paused source will have its state changed to `AL_STOPPED`.

##### See Also
[alSourcePlay](#alsourceplay), [alSourcePlayv](#alsourceplayv),
[alSourcePause](#alsourcepause), [alSourcePausev](#alsourcepausev),
[alSourceRewind](#alsourcerewind), [alSourceRewindv](#alsourcerewindv),
[alsourcestopv](#alsourcestopv)

#### alSourceStopv
##### Description
This function stops a set of sources.

```cpp
void alSourceStopv(
	ALsizei n,
    ALuint *sources
);
```

##### Parameters
* n - The number of sources to stop
* source - A pointer to an array of sources to be stopped

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value pointer given is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The paused sources will have their state changed to `AL_STOPPED`.

##### See Also
[alSourcePlay](#alsourceplay), [alSourcePlayv](#alsourceplayv),
[alSourcePause](#alsourcepause), [alSourceRewind](#alsourcerewind),
[alSourceRewindv](#alsourcerewindv), [alSourceStop](#alsourcestop),
[alsourcestop](#alsourcestop)

#### alSourceRewind
##### Description
This function stops the source and sets its state to `AL_INITIAL`.

```cpp
void alSourceRewind(
    ALuint source
);
```

##### Parameters
* source - The name of the source to be rewound

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alSourcePlay](#alsourceplay), [alSourcePlayv](#alsourceplayv),
[alSourcePause](#alsourcepause), [alSourcePausev](#alsourcepausev),
[alSourceRewindv](#alsourcerewindv), [alsourcestop](#alsourcestop),
[alsourcestopv](#alsourcestopv)

#### alSourceRewindv
##### Description
his function stops a set of sources and sets all their states to `AL_INITIAL`.

```cpp
void alSourceRewindv(
	ALsizei n,
    ALuint *sources
);
```

##### Parameters
* n - The number of sources to be rewound
* source - A pointer to an array of sources to be rewound

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value pointer given is not valid. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alSourcePlay](#alsourceplay), [alSourcePlayv](#alsourceplayv),
[alSourcePause](#alsourcepause), [alSourcePausev](#alsourcepausev),
[alSourceRewind](#alsourcerewind), [alSourceStop](#alsourcestop),
[alsourcestop](#alsourcestop)

#### alSourceQueueBuffers
##### Description
This function queues a set of buffers on a source.  All buffers attached to a
source will be played in sequence, and the number of processed buffers can be
detected using an [alSourcei](#alsourcei) call to retrieve
`AL_BUFFERS_PROCESSED`.

```cpp
void alSourceQueueBuffers(
    ALuint source,
	ALsizei n,
	ALuint* buffers
);
```

##### Parameters
* source - The name of the source to queue buffers onto
* n - The number of buffers to be queued
* buffers - A pointer to an array of buffer names to be queued

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_NAME      | At least one specified buffer name is not valid, or the specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context, an attempt was made to add a new buffer which is not the same format as the buffers already in the queue, or the source already has a static buffer attached. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
When first created, a source will be of type `AL_UNDETERMINED`.  A successful
[alSourceQueueBuffers](#alsourcequeuebuffers) call will change the source type
to `AL_STREAMING`.

##### See Also
[alSourceUnqueueBuffers](#alsourceunqueuebuffers)

#### alSourceUnqueueBuffers
##### Description
This function unqueues a set of buffers attached to a source.  The number of
processed buffers can be detected using an [alSourcei](#alsourcei) call to
retrieve `AL_BUFFERS_PROCESSED`, which is the maximum number of buffers that can
be unqueued using this call.

```cpp
void alSourceUnqueueBuffers(
    ALuint source,
	ALsizei n,
	ALuint* buffers
);
```

##### Parameters
* source - The name of the source to unqueue buffers from
* n - The number of buffers to be unqueued
* buffers - A pointer to an array of buffer names that were removed

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | At least one buffer can not be unqueued because it has not been processed yet. |
| AL_INVALID_NAME      | The specified source name is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The unqueue operation will only take place if all n buffers can be removed from
the queue.

##### See Also
[alSourceQueueBuffers](#alsourcequeuebuffers)
