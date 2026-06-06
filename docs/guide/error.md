## Error Functions
### Error Codes
| Error Code           | Description |
| -------------------- | ----------- |
| AL_NO_ERROR          | There is not currently an error |
| AL_INVALID_NAME      | A bad name (ID) was passed to an OpenAL function |
| AL_INVALID_ENUM      | An invalid enum value was passed to an OpenAL function |
| AL_INVALID_VALUE     | An invalid value was passed to an OpenAL function |
| AL_INVALID_OPERATION | The requested operation is not valid |
| AL_OUT_OF_MEMORY     | The requested operation resulted in OpenAL running out of memory |

### Functions
* [alGetError](#algeterror)

#### alGetError
##### Description
This function returns the current error state and then clears the error state.

```cpp
ALenum alGetError(ALvoid);
```

##### Parameters
None

##### Possible Error States
None

##### Version Requirements
OpenAL 1.0 or higher

##### Remarks
Returns an `Alenum` representing the error state.  When an OpenAL error occurs,
the error state is set and will not be changed until the error state is
retrieved using [alGetError](#algeterror).  Whenever [alGetError](#algeterror)
is called, the error state is cleared and the last state (the current state when
the call was made) is returned.  To isolate error detection to a specific
portion of code, [alGetError](#algeterror) should be called before the isolated
section to clear the current error state.
