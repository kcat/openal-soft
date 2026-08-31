## Context Error Functions
### Error Codes
| Error Code          | Description |
| ------------------- | ----------- |
| ALC_NO_ERROR        | There is not currently an error |
| ALC_INVALID_DEVICE  | A bad device was passed to an OpenAL function |
| ALC_INVALID_CONTEXT | A bad context was passed to an OpenAL function |
| ALC_INVALID_ENUM    | An unknown enum value was passed to an OpenAL function |
| ALC_INVALID_VALUE   | An invalid value was passed to an OpenAL function |
| ALC_OUT_OF_MEMORY   | The requested operation resulted in OpenAL running out of memory |

### Functions
* [alcGetError](#alcgeterror)

#### alcGetError
##### Description
This function retrieves the current context error state.

```cpp
ALCenum alcGetError(ALCdevice *device);
```

##### Parameters
* device - A pointer to the device to retrieve the error state from

##### Possible Error States
None

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
None
