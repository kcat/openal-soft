## State Functions
### Properties
| Property          | Data Type | Description |
| ----------------- | --------- | ----------- |
| AL_DOPPLER_FACTOR | `f`       | Global Doppler factor |
| AL_SPEED_OF_SOUND | `f`       | Speed of sound in units per second |
| AL_DISTANCE_MODEL | `f`       | Distance model enumeration value |

### Functions
* [alEnable](#alenable)
* [alDisable](#aldisable)
* [alIsEnabled](#alisenabled)
* [alGetBoolean](#algetboolean)
* [alGetDouble](#algetdouble)
* [alGetFloat](#algetfloat)
* [alGetInteger](#algetinteger)
* [alGetBooleanv](#algetbooleanv)
* [alGetDoublev](#algetdoublev)
* [alGetFloatv](#algetfloatv)
* [alGetIntegerv](#algetintegerv)
* [alGetString](#algetstring)
* [alDistanceModel](#aldistancemodel)
* [alDopplerFactor](#aldopplerfactor)
* [alSpeedOfSound](#alspeedofsound)

#### alEnable
##### Description
This function enables a feature of the OpenAL driver.

```cpp
void alEnable(
    ALenum capability
);
```

##### Parameters
* capability - the name of a capability to enable

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified capability is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
There are no capabilities defined in OpenAL 1.1 to be used with this function,
but it may be used by an extension.

##### See Also
[alDisable](#aldisable), [alIsEnabled](#alisenabled)

#### alDisable
##### Description
This function disables a feature of the OpenAL driver.

```cpp
void alDisable(
    ALenum capability
);
```

##### Parameters
* capability - the name of a capability to distable

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified capability is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
There are no capabilities defined in OpenAL 1.1 to be used with this function,
but it may be used by an extension.

##### See Also
[alEnable](#alenabled), [alIsEnabled](#alisenabled)

#### alIsEnabled
##### Description
This function returns a boolean indicating if a specific feature is enabled in
the OpenAL driver.

```cpp
ALboolean alIsEnabled(
    ALenum capability
);
```

##### Parameters
* capability - the name of a capability to check

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified capability is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns `AL_TRUE` if the capability is enabled, `AL_FALSE` if the capability is
disabled.  There are no capabilities defined in OpenAL 1.1 to be used with this
function, but it may be used by an extension.

##### See Also
[alEnable](#alenabled), [alDisable](#aldisable)

#### alGetBoolean
##### Description
This function returns a boolean OpenAL state.

```cpp
ALboolean alGetBoolean(
    ALenum param
);
```

##### Parameters
* param - the state to be queried:
    + `AL_DOPPLER_FACTOR`
	+ `AL_SPEED_OF_SOUND`
	+ `AL_DISTANCE_MODEL`

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The boolean state described by `param` will be returned.

##### See Also
[alGetBooleanv](#algetbooleanv), [alGetDouble](#algetdouble),
[alGetDoublev](#algetdoublev), [alGetFloat](#algetfloat),
[alGetFloatv](#algetfloatv), [alGetInteger](#algetinteger),
[alGetIntegerv](#algetintegerv)

#### alGetDouble
##### Description
This function returns a double precision floating point OpenAL state.

```cpp
ALdouble alGetDouble(
    ALenum param
);
```

##### Parameters
* param - the state to be queried:
    + `AL_DOPPLER_FACTOR`
	+ `AL_SPEED_OF_SOUND`
	+ `AL_DISTANCE_MODEL`

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The double value described by `param` will be returned.

##### See Also
[alGetBoolean](#algetboolean), [alGetBooleanv](#algetbooleanv),
[alGetDoublev](#algetdoublev), [alGetFloat](#algetfloat),
[alGetFloatv](#algetfloatv), [alGetInteger](#algetinteger),
[alGetIntegerv](#algetintegerv)

#### alGetFloat
##### Description
This function returns a floating point OpenAL state.

```cpp
ALfloat alGetFloat(
    ALenum param
);
```

##### Parameters
* param - the state to be queried:
    + `AL_DOPPLER_FACTOR`
	+ `AL_SPEED_OF_SOUND`
	+ `AL_DISTANCE_MODEL`

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The floating point state described by `param` will be returned.

##### See Also
[alGetBoolean](#algetboolean), [alGetBooleanv](#algetbooleanv),
[alGetDouble](#algetdouble), [alGetDoublev](#algetdoublev),
[alGetFloatv](#algetfloatv), [alGetInteger](#algetinteger),
[alGetIntegerv](#algetintegerv)

#### alGetInteger
##### Description
This function returns an integer OpenAL state.

```cpp
ALint alGetInteger(
    ALenum param
);
```

##### Parameters
* param - the state to be queried:
    + `AL_DOPPLER_FACTOR`
	+ `AL_SPEED_OF_SOUND`
	+ `AL_DISTANCE_MODEL`

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The integer state described by `param` will be returned.

##### See Also
[alGetBoolean](#algetboolean), [alGetBooleanv](#algetbooleanv),
[alGetDouble](#algetdouble), [alGetDoublev](#algetdoublev),
[alGetFloat](#algetfloat), [alGetFloatv](#algetfloatv),
[alGetIntegerv](#algetintegerv)

#### alGetBooleanv
##### Description
This function returns an integer OpenAL state.

```cpp
void alGetBooleanv(
    ALenum param,
	ALboolean* data
);
```

##### Parameters
* param - the state to be queried:
    + `AL_DOPPLER_FACTOR`
	+ `AL_SPEED_OF_SOUND`
	+ `AL_DISTANCE_MODEL`
* data - A pointer to the location where the state will be stored

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_VALUE     | The specified data pointer is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alGetBoolean](#algetboolean), [alGetDouble](#algetdouble),
[alGetDoublev](#algetdoublev), [alGetFloat](#algetfloat),
[alGetFloatv](#algetfloatv), [alGetInteger](#algetinteger),
[alGetIntegerv](#algetintegerv)

#### alGetDoublev
##### Description
This function retrieves a double precision floating point OpenAL state.

```cpp
void alGetDoublev(
    ALenum param,
	ALdouble* data
);
```

##### Parameters
* param - the state to be queried:
    + `AL_DOPPLER_FACTOR`
	+ `AL_SPEED_OF_SOUND`
	+ `AL_DISTANCE_MODEL`
* data - A pointer to the location where the state will be stored

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_VALUE     | The specified data pointer is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alGetBoolean](#algetboolean), [alGetBooleanv](#algetbooleanv),
[alGetDouble](#algetdouble), [alGetFloat](#algetfloat),
[alGetFloatv](#algetfloatv), [alGetInteger](#algetinteger),
[alGetIntegerv](#algetintegerv)

#### alGetFloatv
##### Description
This function retrieves a floating point OpenAL state.

```cpp
void alGetFloatv(
    ALenum param,
	ALfloat* data
);
```

##### Parameters
* param - the state to be queried:
    + `AL_DOPPLER_FACTOR`
	+ `AL_SPEED_OF_SOUND`
	+ `AL_DISTANCE_MODEL`
* data - A pointer to the location where the state will be stored

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_VALUE     | The specified data pointer is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alGetBoolean](#algetboolean), [alGetBooleanv](#algetbooleanv),
[alGetDouble](#algetdouble), [alGetDoublev](#algetdoublev),
[alGetFloat](#algetfloat), [alGetInteger](#algetinteger),
[alGetIntegerv](#algetintegerv)

#### alGetIntegerv
##### Description
This function retrieves an integer OpenAL state.

```cpp
void alGetIntegerv(
    ALenum param,
	ALint* data
);
```

##### Parameters
* param - the state to be queried:
    + `AL_DOPPLER_FACTOR`
	+ `AL_SPEED_OF_SOUND`
	+ `AL_DISTANCE_MODEL`
* data - A pointer to the location where the state will be stored

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_VALUE     | The specified data pointer is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alGetBoolean](#algetboolean), [alGetBooleanv](#algetbooleanv),
[alGetDouble](#algetdouble), [alGetDoublev](#algetdoublev),
[alGetFloat](#algetfloat), [alGetFloatv](#algetfloatv),
[alGetInteger](#algetinteger)

#### alGetString
##### Description
This function retrieves an OpenAL string property.

```cpp
const ALchar* alGetString(
    ALenum param
);
```

##### Parameters
* param - the state to be queried:
    + `AL_VENDOR`
	+ `AL_VERSION`
	+ `AL_RENDERER`
	+ `AL_EXTENSIONS`

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_ENUM      | The specified parameter is not valid. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns a pointer to a null-terminated string.

#### alDistanceModel
##### Description
This function selects the OpenAL distance model – `AL_INVERSE_DISTANCE`,
`AL_INVERSE_DISTANCE_CLAMPED`, `AL_LINEAR_DISTANCE`,
`AL_LINEAR_DISTANCE_CLAMPED`, `AL_EXPONENT_DISTANCE`,
`AL_EXPONENT_DISTANCE_CLAMPED`, or `AL_NONE`.

The `AL_INVERSE_DISTANCE` model works according to the following formula:
```cpp
gain = AL_REFERENCE_DISTANCE / (AL_REFERENCE_DISTANCE
     + AL_ROLLOFF_FACTOR * (distance - AL_REFERENCE_DISTANCE));
```

The `AL_INVERSE_DISTANCE_CLAMPED` model works according to the following
formula:
```cpp
distance = max(distance, AL_REFERENCE_DISTANCE);
distance = min(distance, AL_MAX_DISTANCE);
gain = AL_REFERENCE_DISTANCE / (AL_REFERENCE_DISTANCE
     + AL_ROLLOFF_FACTOR * (distance - AL_REFERENCE_DISTANCE));
```

Here is a graph showing the inverse distance curve:
![Inverse Distance Curve](images/figure-inverse-distance.jpg)

The `AL_LINEAR_DISTANCE` model works according to the following formula:
```cpp
distance = min(distance, AL_MAX_DISTANCE);	// Avoid negative gain
gain = (1 - AL_ROLLOFF_FACTOR * (distance -
            AL_REFERENCE_DISTANCE) /
			(AL_MAX_DISTANCE - AL_REFERENCE_DISTANCE));
```

The `AL_LINEAR_DISTANCE_CLAMPED` model works according to the following formula:
```cpp
distance = max(distance, AL_REFERENCE_DISTANCE);
distance = min(distance, AL_MAX_DISTANCE);
gain = (1 - AL_ROLLOFF_FACTOR * (distance -
            AL_REFERENCE_DISTANCE) /
           (AL_MAX_DISTANCE - AL_REFERENCE_DISTANCE));
```

Here is a graph showing the linear distance curve:
![Inverse Distance Curve](images/figure-linear-distance.jpg)

The `AL_EXPONENT_DISTANCE` model works according to the following forumula:
```cpp
gain = pow(distance / AL_REFERENCE_DISTANCE, -AL_ROLLOFF_FACTOR);
```

The `AL_EXPONENT_DISTANCE_CLAMPED` model works according to the following
forumla:
```cpp
distance = max(distance, AL_REFERENCE_DISTANCE);
distance = min(distance, AL_MAX_DISTANCE);
gain = pow(distance / AL_REFERENCE_DISTANCE, -AL_ROLLOFF_FACTOR);
```

Here is a graph showing the exponential distance curve:
![Inverse Distance Curve](images/figure-exponential-distance.jpg)

The `AL_NONE` distance model works according to the following formula:
```cpp
gain = 1;
```

```cpp
void alDistanceModel(
    ALenum value
);
```

##### Parameters
* value - The distance model to be set:
    + `AL_INVERSE_DISTANCE`
	+ `AL_INVERSE_DISTANCE_CLAMPED`
	+ `AL_LINEAR_DISTANCE`
	+ `AL_LINEAR_DISTANCE_CLAMPED`
	+ `AL_EXPONENT_DISTANCE`
	+ `AL_EXPONENT_DISTANCE_CLAMPED`
	+ `AL_NONE`

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The specified distance model is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The default distance model in OpenAL is `AL_INVERSE_DISTANCE_CLAMPED`.

#### alDopplerFactor
##### Description
This function selects the OpenAL Doppler factor value.

```cpp
void alDopplerFactor(
    ALfloat value
);
```

##### Parameters
* value - The Doppler scale value to set

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The specified distance model is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
The default Doppler factor value is 1.0.

#### alSpeedOfSound
##### Description
This function selects the speed of sound for use in Doppler calculations.

```cpp
void alSpeedOfSound(
    ALfloat value
);
```

##### Parameters
* value - The speed of sound value to set

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The specified distance model is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
The default speed of sound value is 343.3.
