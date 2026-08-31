## Listener Functions
### Properties
| Property       | Data Type              | Description |
| -------------- | ---------------------- | ----------- |
| AL_GAIN        | `f`, `fv`              | Master gain. Value should be positive |
| AL_POSITION    | `fv`, `3f`, `iv`, `3i` | X, Y, Z position |
| AL_VELOCITY    | `fv`, `3f`, `iv`, `3i` | Velocity vector |
| AL_ORIENTATION | `fv`, `fv`             | Orientation expressed as "at" and "up" vectors |

### Functions
* [alListenerf](#allistenerf)
* [alListener3f](#allistenerf)
* [alListenerfv](#allistenerfv)
* [alListeneri](#allisteneri)
* [alListener3i](#allistener3i)
* [alListeneriv](#allisteneriv)
* [alGetListenerf](#algetlistenerf)
* [alGetListener3f](#algetlistener3f)
* [alGetListenerfv](#algetlistenerfv)
* [alGetListeneri](#algetlisteneri)
* [alGetListener3i](#algetlistener3i)
* [alGetListeneriv](#algetlisteneriv)

#### alListenerf
##### Description
This function sets a floating point property for the listener.

```cpp
void alListenerf(
    ALenum param,
	ALfloat value
);
```

##### Parameters
* param - The name of the attribute to be set:
	+ `AL_GAIN`
* value - The ALfloat value to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alListener3f](#allistener3f), [alListenerfv](#allistenerfv),
[alGetListenerf](#algetlistenerf), [alGetListener3f](#algetlistener3f),
[alGetListenerfv](#algetlistenerfv)

#### alListener3f
##### Description
This function sets a floating point property for the listener.

```cpp
void alListener3f(
    ALenum param,
	ALfloat v1,
	ALfloat v2,
	ALfloat v3
);
```

##### Parameters
* param - The name of the attribute to be set:
	+ `AL_POSITION`
	+ `AL_VELOCITY`
* v1, v2, v3 - The value to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alListenerf](#allistenerf), [alListenerfv](#allistenerfv),
[alGetListenerf](#algetlistenerf), [alGetListener3f](#algetlistener3f),
[alGetListenerfv](#algetlistenerfv)

#### alListenerfv
##### Description
This function sets a floating point-vector property for the listener.

```cpp
void alListenerfv(
    ALenum param,
	ALfloat* values
);
```

##### Parameters
* param - The name of the attribute to be set:
	+ `AL_POSITION`
	+ `AL_VELOCITY`
	+ `AL_ORIENTATION`
* values - Pointer to floating point-vector values

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alListenerf](#allistenerf), [alListener3f](#allistenerf),
[alGetListenerf](#algetlistenerf), [alGetListener3f](#algetlistener3f),
[alGetListenerfv](#algetlistenerfv)

#### alListeneri
##### Description
This function sets an integer property for the listener.

```cpp
void alListeneri(
    ALenum param,
	ALint value
);
```

##### Parameters
* param - The name of the attribute to be set
* value - The integer value to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
There are no integer listener attributes defined for OpenAL 1.1, but this
function may be used by an extension.

##### See Also
[alListener3i](#allistener3i), [alListeneriv](#allisteneriv),
[alGetListeneri](#algetlisteneri), [alGetListener3i](#algetlistener3i),
[alGetListeneriv](#algetlisteneriv)

#### alListener3i
##### Description
This function sets an integer property for the listener.

```cpp
void alListener3i(
    ALenum param,
	ALint v1,
	ALint v2,
	ALint v3
);
```

##### Parameters
* param - The name of the attribute to be set:
	+ `AL_POSITION`
	+ `AL_VELOCITY`
* v1, v2, v3 - The integer values to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alListeneri](#allisteneri), [alListeneriv](#allisteneriv),
[alGetListeneri](#algetlisteneri), [alGetListener3i](#algetlistener3i),
[alGetListeneriv](#algetlisteneriv)

#### alListeneriv
##### Description
This function sets an integer property for the listener.

```cpp
void alListeneriv(
    ALenum param,
	ALint* values
);
```

##### Parameters
* param - The name of the attribute to be set:
	+ `AL_POSITION`
	+ `AL_VELOCITY`
	+ `AL_ORIENTATION`
* values - Pointer to the integer values to set the attribute to

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alListeneri](#allisteneri), [alListener3i](#allisteneri),
[alGetListeneri](#algetlisteneri), [alGetListener3i](#algetlistener3i),
[alGetListeneriv](#algetlisteneriv)

#### alGetListenerf
##### Description
This function retrieves a floating point property of the listener.

```cpp
void alGetListenerf(
    ALenum param,
	ALfloat* value
);
```

##### Parameters
* param - The name of the attribute to be retrieved:
	+ `AL_GAIN`
* values - Pointer to the floating-point value being retrieved

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alListenerf](#allistenerf), [alListener3f](#allistenerf),
[alListenerfv](#allistenerfv), [alGetListener3f](#algetlistener3f),
[alGetListenerfv](#algetlistenerfv)

#### alGetListener3f
##### Description
This function retrieves a set of three floating point values from a property of
the listener.

```cpp
void alGetListener3f(
    ALenum param,
	ALfloat* v1,
	ALfloat* v2,
	ALfloat* v3
);
```

##### Parameters
* param - The name of the attribute to be retrieved:
	+ `AL_POSITION`
	+ `AL_VELOCITY`
* v1, v2, v3 - Pointers to the three floating point being retrieved

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alListenerf](#allistenerf), [alListener3f](#allistenerf),
[alListenerfv](#allistenerfv), [alGetListenerf](#algetlistenerf),
[alGetListenerfv](#algetlistenerfv)

#### alGetListenerfv
##### Description
This function retrieves a floating point-vector property of the listener.

```cpp
void alGetListenerfv(
    ALenum param,
	ALfloat* values
);
```

##### Parameters
* param - The name of the attribute to be retrieved:
	+ `AL_POSITION`
	+ `AL_VELOCITY`
	+ `AL_ORIENTATION`
* values - Pointers to the three floating point being retrieved

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None

##### See Also
[alListenerf](#allistenerf), [alListener3f](#allistenerf),
[alListenerfv](#allistenerfv), [alGetListenerf](#algetlistenerf),
[alGetListener3f](#algetlistener3f)

#### alGetListeneri
##### Description
This function retrieves an integer property of the listener.

```cpp
void alGetListeneri(
    ALenum param,
	ALint* value
);
```

##### Parameters
* param - The name of the attribute to be retrieved:
* values - Pointer to the integer value being retrieved

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
There are no integer listener attributes defined for OpenAL 1.1, but this
function may be used by an extension.

##### See Also
[alListeneri](#allisteneri), [alListener3i](#allisteneri),
[alListeneriv](#allisteneriv), [alGetListener3i](#algetlistener3i),
[alGetListeneriv](#algetlisteneriv)

#### alGetListener3i
##### Description
This function retrieves an integer property of the listener.

```cpp
void alGetListener3i(
    ALenum param,
	ALint* v1,
	ALint* v2,
	ALint* v3
);
```

##### Parameters
* param - The name of the attribute to be retrieved:
	+ `AL_POSITION`
	+ `AL_VELOCITY`
* v1, v2, v3 - Pointers to the integer values being retrieved

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alListeneri](#allisteneri), [alListener3i](#allisteneri),
[alListeneriv](#allisteneriv), [alGetListeneri](#algetlisteneri),
[alGetListeneriv](#algetlisteneriv)

#### alGetListeneriv
##### Description
This function retrieves an integer property of the listener.

```cpp
void alGetListeneriv(
    ALenum param,
	ALint* values
);
```

##### Parameters
* param - The name of the attribute to be retrieved:
	+ `AL_POSITION`
	+ `AL_VELOCITY`
	+ `AL_ORIENTATION`
* values - Pointers to the integer values being retrieved

##### Possible Error States
| State                | Description |
| -------------------- | ----------- |
| AL_INVALID_VALUE     | The value given is not valid. |
| AL_INVALID_ENUM      | The specified parameter is not valid. |
| AL_INVALID_OPERATION | There is no current context. |

##### Version Requirements
OpenAL 1.1 or higher

##### Remarks
None

##### See Also
[alListeneri](#allisteneri), [alListener3i](#allisteneri),
[alListeneriv](#allisteneriv), [alGetListeneri](#algetlisteneri),
[alGetListener3i](#algetlistener3i)
