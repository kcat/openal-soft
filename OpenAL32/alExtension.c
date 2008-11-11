/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "alExtension.h"
#include "alError.h"
#include "alMain.h"
#include "alFilter.h"
#include "alEffect.h"
#include "alAuxEffectSlot.h"
#include "alSource.h"
#include "alBuffer.h"
#include "AL/al.h"
#include "AL/alc.h"

static ALfunction  function[]=   {
    { "alEnable",                   (ALvoid *) alEnable                  },
    { "alDisable",                  (ALvoid *) alDisable                 },
    { "alIsEnabled",                (ALvoid *) alIsEnabled               },
    { "alGetString",                (ALvoid *) alGetString               },
    { "alGetBooleanv",              (ALvoid *) alGetBooleanv             },
    { "alGetIntegerv",              (ALvoid *) alGetIntegerv             },
    { "alGetFloatv",                (ALvoid *) alGetFloatv               },
    { "alGetDoublev",               (ALvoid *) alGetDoublev              },
    { "alGetBoolean",               (ALvoid *) alGetBoolean              },
    { "alGetInteger",               (ALvoid *) alGetInteger              },
    { "alGetFloat",                 (ALvoid *) alGetFloat                },
    { "alGetDouble",                (ALvoid *) alGetDouble               },
    { "alGetError",                 (ALvoid *) alGetError                },
    { "alIsExtensionPresent",       (ALvoid *) alIsExtensionPresent      },
    { "alGetProcAddress",           (ALvoid *) alGetProcAddress          },
    { "alGetEnumValue",             (ALvoid *) alGetEnumValue            },
    { "alListenerf",                (ALvoid *) alListenerf               },
    { "alListener3f",               (ALvoid *) alListener3f              },
    { "alListenerfv",               (ALvoid *) alListenerfv              },
    { "alListeneri",                (ALvoid *) alListeneri               },
    { "alListener3i",               (ALvoid *) alListener3i              },
    { "alListeneriv",               (ALvoid *) alListeneriv              },
    { "alGetListenerf",             (ALvoid *) alGetListenerf            },
    { "alGetListener3f",            (ALvoid *) alGetListener3f           },
    { "alGetListenerfv",            (ALvoid *) alGetListenerfv           },
    { "alGetListeneri",             (ALvoid *) alGetListeneri            },
    { "alGetListener3i",            (ALvoid *) alGetListener3i           },
    { "alGetListeneriv",            (ALvoid *) alGetListeneriv           },
    { "alGenSources",               (ALvoid *) alGenSources              },
    { "alDeleteSources",            (ALvoid *) alDeleteSources           },
    { "alIsSource",                 (ALvoid *) alIsSource                },
    { "alSourcef",                  (ALvoid *) alSourcef                 },
    { "alSource3f",                 (ALvoid *) alSource3f                },
    { "alSourcefv",                 (ALvoid *) alSourcefv                },
    { "alSourcei",                  (ALvoid *) alSourcei                 },
    { "alSource3i",                 (ALvoid *) alSource3i                },
    { "alSourceiv",                 (ALvoid *) alSourceiv                },
    { "alGetSourcef",               (ALvoid *) alGetSourcef              },
    { "alGetSource3f",              (ALvoid *) alGetSource3f             },
    { "alGetSourcefv",              (ALvoid *) alGetSourcefv             },
    { "alGetSourcei",               (ALvoid *) alGetSourcei              },
    { "alGetSource3i",              (ALvoid *) alGetSource3i             },
    { "alGetSourceiv",              (ALvoid *) alGetSourceiv             },
    { "alSourcePlayv",              (ALvoid *) alSourcePlayv             },
    { "alSourceStopv",              (ALvoid *) alSourceStopv             },
    { "alSourceRewindv",            (ALvoid *) alSourceRewindv           },
    { "alSourcePausev",             (ALvoid *) alSourcePausev            },
    { "alSourcePlay",               (ALvoid *) alSourcePlay              },
    { "alSourceStop",               (ALvoid *) alSourceStop              },
    { "alSourceRewind",             (ALvoid *) alSourceRewind            },
    { "alSourcePause",              (ALvoid *) alSourcePause             },
    { "alSourceQueueBuffers",       (ALvoid *) alSourceQueueBuffers      },
    { "alSourceUnqueueBuffers",     (ALvoid *) alSourceUnqueueBuffers    },
    { "alGenBuffers",               (ALvoid *) alGenBuffers              },
    { "alDeleteBuffers",            (ALvoid *) alDeleteBuffers           },
    { "alIsBuffer",                 (ALvoid *) alIsBuffer                },
    { "alBufferData",               (ALvoid *) alBufferData              },
    { "alBufferf",                  (ALvoid *) alBufferf                 },
    { "alBuffer3f",                 (ALvoid *) alBuffer3f                },
    { "alBufferfv",                 (ALvoid *) alBufferfv                },
    { "alBufferi",                  (ALvoid *) alBufferi                 },
    { "alBuffer3i",                 (ALvoid *) alBuffer3i                },
    { "alBufferiv",                 (ALvoid *) alBufferiv                },
    { "alGetBufferf",               (ALvoid *) alGetBufferf              },
    { "alGetBuffer3f",              (ALvoid *) alGetBuffer3f             },
    { "alGetBufferfv",              (ALvoid *) alGetBufferfv             },
    { "alGetBufferi",               (ALvoid *) alGetBufferi              },
    { "alGetBuffer3i",              (ALvoid *) alGetBuffer3i             },
    { "alGetBufferiv",              (ALvoid *) alGetBufferiv             },
    { "alDopplerFactor",            (ALvoid *) alDopplerFactor           },
    { "alDopplerVelocity",          (ALvoid *) alDopplerVelocity         },
    { "alSpeedOfSound",             (ALvoid *) alSpeedOfSound            },
    { "alDistanceModel",            (ALvoid *) alDistanceModel           },

    { "alGenFilters",               (ALvoid *) alGenFilters              },
    { "alDeleteFilters",            (ALvoid *) alDeleteFilters           },
    { "alIsFilter",                 (ALvoid *) alIsFilter                },
    { "alFilteri",                  (ALvoid *) alFilteri                 },
    { "alFilteriv",                 (ALvoid *) alFilteriv                },
    { "alFilterf",                  (ALvoid *) alFilterf                 },
    { "alFilterfv",                 (ALvoid *) alFilterfv                },
    { "alGetFilteri",               (ALvoid *) alGetFilteri              },
    { "alGetFilteriv",              (ALvoid *) alGetFilteriv             },
    { "alGetFilterf",               (ALvoid *) alGetFilterf              },
    { "alGetFilterfv",              (ALvoid *) alGetFilterfv             },

    { "alGenEffects",               (ALvoid *) alGenEffects              },
    { "alDeleteEffects",            (ALvoid *) alDeleteEffects           },
    { "alIsEffect",                 (ALvoid *) alIsEffect                },
    { "alEffecti",                  (ALvoid *) alEffecti                 },
    { "alEffectiv",                 (ALvoid *) alEffectiv                },
    { "alEffectf",                  (ALvoid *) alEffectf                 },
    { "alEffectfv",                 (ALvoid *) alEffectfv                },
    { "alGetEffecti",               (ALvoid *) alGetEffecti              },
    { "alGetEffectiv",              (ALvoid *) alGetEffectiv             },
    { "alGetEffectf",               (ALvoid *) alGetEffectf              },
    { "alGetEffectfv",              (ALvoid *) alGetEffectfv             },

    { "alGenAuxiliaryEffectSlots",  (ALvoid *) alGenAuxiliaryEffectSlots },
    { "alDeleteAuxiliaryEffectSlots",(ALvoid *) alDeleteAuxiliaryEffectSlots},
    { "alIsAuxiliaryEffectSlot",    (ALvoid *) alIsAuxiliaryEffectSlot   },
    { "alAuxiliaryEffectSloti",     (ALvoid *) alAuxiliaryEffectSloti    },
    { "alAuxiliaryEffectSlotiv",    (ALvoid *) alAuxiliaryEffectSlotiv   },
    { "alAuxiliaryEffectSlotf",     (ALvoid *) alAuxiliaryEffectSlotf    },
    { "alAuxiliaryEffectSlotfv",    (ALvoid *) alAuxiliaryEffectSlotfv   },
    { "alGetAuxiliaryEffectSloti",  (ALvoid *) alGetAuxiliaryEffectSloti },
    { "alGetAuxiliaryEffectSlotiv", (ALvoid *) alGetAuxiliaryEffectSlotiv},
    { "alGetAuxiliaryEffectSlotf",  (ALvoid *) alGetAuxiliaryEffectSlotf },
    { "alGetAuxiliaryEffectSlotfv", (ALvoid *) alGetAuxiliaryEffectSlotfv},

    { "alBufferSubDataEXT",         (ALvoid *) alBufferSubDataEXT        },

    { NULL,                         (ALvoid *) NULL                      } };

static ALenums enumeration[]={
    // Types
    { (ALchar *)"AL_INVALID",                           AL_INVALID                          },
    { (ALchar *)"AL_NONE",                              AL_NONE                             },
    { (ALchar *)"AL_FALSE",                             AL_FALSE                            },
    { (ALchar *)"AL_TRUE",                              AL_TRUE                             },

    // Source and Listener Properties
    { (ALchar *)"AL_SOURCE_RELATIVE",                   AL_SOURCE_RELATIVE                  },
    { (ALchar *)"AL_CONE_INNER_ANGLE",                  AL_CONE_INNER_ANGLE                 },
    { (ALchar *)"AL_CONE_OUTER_ANGLE",                  AL_CONE_OUTER_ANGLE                 },
    { (ALchar *)"AL_PITCH",                             AL_PITCH                            },
    { (ALchar *)"AL_POSITION",                          AL_POSITION                         },
    { (ALchar *)"AL_DIRECTION",                         AL_DIRECTION                        },
    { (ALchar *)"AL_VELOCITY",                          AL_VELOCITY                         },
    { (ALchar *)"AL_LOOPING",                           AL_LOOPING                          },
    { (ALchar *)"AL_BUFFER",                            AL_BUFFER                           },
    { (ALchar *)"AL_GAIN",                              AL_GAIN                             },
    { (ALchar *)"AL_MIN_GAIN",                          AL_MIN_GAIN                         },
    { (ALchar *)"AL_MAX_GAIN",                          AL_MAX_GAIN                         },
    { (ALchar *)"AL_ORIENTATION",                       AL_ORIENTATION                      },
    { (ALchar *)"AL_REFERENCE_DISTANCE",                AL_REFERENCE_DISTANCE               },
    { (ALchar *)"AL_ROLLOFF_FACTOR",                    AL_ROLLOFF_FACTOR                   },
    { (ALchar *)"AL_CONE_OUTER_GAIN",                   AL_CONE_OUTER_GAIN                  },
    { (ALchar *)"AL_MAX_DISTANCE",                      AL_MAX_DISTANCE                     },
    { (ALchar *)"AL_SEC_OFFSET",                        AL_SEC_OFFSET                       },
    { (ALchar *)"AL_SAMPLE_OFFSET",                     AL_SAMPLE_OFFSET                    },
    { (ALchar *)"AL_BYTE_OFFSET",                       AL_BYTE_OFFSET                      },
    { (ALchar *)"AL_SOURCE_TYPE",                       AL_SOURCE_TYPE                      },
    { (ALchar *)"AL_STATIC",                            AL_STATIC                           },
    { (ALchar *)"AL_STREAMING",                         AL_STREAMING                        },
    { (ALchar *)"AL_UNDETERMINED",                      AL_UNDETERMINED                     },
    { (ALchar *)"AL_METERS_PER_UNIT",                   AL_METERS_PER_UNIT                  },

    // Source EFX Properties
    { (ALchar *)"AL_DIRECT_FILTER",                     AL_DIRECT_FILTER                    },
    { (ALchar *)"AL_AUXILIARY_SEND_FILTER",             AL_AUXILIARY_SEND_FILTER            },
    { (ALchar *)"AL_AIR_ABSORPTION_FACTOR",             AL_AIR_ABSORPTION_FACTOR            },
    { (ALchar *)"AL_ROOM_ROLLOFF_FACTOR",               AL_ROOM_ROLLOFF_FACTOR              },
    { (ALchar *)"AL_CONE_OUTER_GAINHF",                 AL_CONE_OUTER_GAINHF                },
    { (ALchar *)"AL_DIRECT_FILTER_GAINHF_AUTO",         AL_DIRECT_FILTER_GAINHF_AUTO        },
    { (ALchar *)"AL_AUXILIARY_SEND_FILTER_GAIN_AUTO",   AL_AUXILIARY_SEND_FILTER_GAIN_AUTO  },
    { (ALchar *)"AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO", AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO},


    // Source State information
    { (ALchar *)"AL_SOURCE_STATE",                      AL_SOURCE_STATE                     },
    { (ALchar *)"AL_INITIAL",                           AL_INITIAL                          },
    { (ALchar *)"AL_PLAYING",                           AL_PLAYING                          },
    { (ALchar *)"AL_PAUSED",                            AL_PAUSED                           },
    { (ALchar *)"AL_STOPPED",                           AL_STOPPED                          },

    // Queue information
    { (ALchar *)"AL_BUFFERS_QUEUED",                    AL_BUFFERS_QUEUED                   },
    { (ALchar *)"AL_BUFFERS_PROCESSED",                 AL_BUFFERS_PROCESSED                },

    // Buffer Formats
    { (ALchar *)"AL_FORMAT_MONO8",                      AL_FORMAT_MONO8                     },
    { (ALchar *)"AL_FORMAT_MONO16",                     AL_FORMAT_MONO16                    },
    { (ALchar *)"AL_FORMAT_MONO_FLOAT32",               AL_FORMAT_MONO_FLOAT32              },
    { (ALchar *)"AL_FORMAT_STEREO8",                    AL_FORMAT_STEREO8                   },
    { (ALchar *)"AL_FORMAT_STEREO16",                   AL_FORMAT_STEREO16                  },
    { (ALchar *)"AL_FORMAT_STEREO_FLOAT32",             AL_FORMAT_STEREO_FLOAT32            },
    { (ALchar *)"AL_FORMAT_MONO_IMA4",                  AL_FORMAT_MONO_IMA4                 },
    { (ALchar *)"AL_FORMAT_STEREO_IMA4",                AL_FORMAT_STEREO_IMA4               },
    { (ALchar *)"AL_FORMAT_QUAD8_LOKI",                 AL_FORMAT_QUAD8_LOKI                },
    { (ALchar *)"AL_FORMAT_QUAD16_LOKI",                AL_FORMAT_QUAD16_LOKI               },
    { (ALchar *)"AL_FORMAT_QUAD8",                      AL_FORMAT_QUAD8                     },
    { (ALchar *)"AL_FORMAT_QUAD16",                     AL_FORMAT_QUAD16                    },
    { (ALchar *)"AL_FORMAT_QUAD32",                     AL_FORMAT_QUAD32                    },
    { (ALchar *)"AL_FORMAT_51CHN8",                     AL_FORMAT_51CHN8                    },
    { (ALchar *)"AL_FORMAT_51CHN16",                    AL_FORMAT_51CHN16                   },
    { (ALchar *)"AL_FORMAT_51CHN32",                    AL_FORMAT_51CHN32                   },
    { (ALchar *)"AL_FORMAT_61CHN8",                     AL_FORMAT_61CHN8                    },
    { (ALchar *)"AL_FORMAT_61CHN16",                    AL_FORMAT_61CHN16                   },
    { (ALchar *)"AL_FORMAT_61CHN32",                    AL_FORMAT_61CHN32                   },
    { (ALchar *)"AL_FORMAT_71CHN8",                     AL_FORMAT_71CHN8                    },
    { (ALchar *)"AL_FORMAT_71CHN16",                    AL_FORMAT_71CHN16                   },
    { (ALchar *)"AL_FORMAT_71CHN32",                    AL_FORMAT_71CHN32                   },
    { (ALchar *)"AL_FORMAT_REAR8",                      AL_FORMAT_REAR8                     },
    { (ALchar *)"AL_FORMAT_REAR16",                     AL_FORMAT_REAR16                    },
    { (ALchar *)"AL_FORMAT_REAR32",                     AL_FORMAT_REAR32                    },

    // Buffer attributes
    { (ALchar *)"AL_FREQUENCY",                         AL_FREQUENCY                        },
    { (ALchar *)"AL_BITS",                              AL_BITS                             },
    { (ALchar *)"AL_CHANNELS",                          AL_CHANNELS                         },
    { (ALchar *)"AL_SIZE",                              AL_SIZE                             },

    // Buffer States (not supported yet)
    { (ALchar *)"AL_UNUSED",                            AL_UNUSED                           },
    { (ALchar *)"AL_PENDING",                           AL_PENDING                          },
    { (ALchar *)"AL_PROCESSED",                         AL_PROCESSED                        },

    // AL Error Messages
    { (ALchar *)"AL_NO_ERROR",                          AL_NO_ERROR                         },
    { (ALchar *)"AL_INVALID_NAME",                      AL_INVALID_NAME                     },
    { (ALchar *)"AL_INVALID_ENUM",                      AL_INVALID_ENUM                     },
    { (ALchar *)"AL_INVALID_VALUE",                     AL_INVALID_VALUE                    },
    { (ALchar *)"AL_INVALID_OPERATION",                 AL_INVALID_OPERATION                },
    { (ALchar *)"AL_OUT_OF_MEMORY",                     AL_OUT_OF_MEMORY                    },

    // Context strings
    { (ALchar *)"AL_VENDOR",                            AL_VENDOR                           },
    { (ALchar *)"AL_VERSION",                           AL_VERSION                          },
    { (ALchar *)"AL_RENDERER",                          AL_RENDERER                         },
    { (ALchar *)"AL_EXTENSIONS",                        AL_EXTENSIONS                       },

    // Global states
    { (ALchar *)"AL_DOPPLER_FACTOR",                    AL_DOPPLER_FACTOR                   },
    { (ALchar *)"AL_DOPPLER_VELOCITY",                  AL_DOPPLER_VELOCITY                 },
    { (ALchar *)"AL_DISTANCE_MODEL",                    AL_DISTANCE_MODEL                   },
    { (ALchar *)"AL_SPEED_OF_SOUND",                    AL_SPEED_OF_SOUND                   },

    // Distance Models
    { (ALchar *)"AL_INVERSE_DISTANCE",                  AL_INVERSE_DISTANCE                 },
    { (ALchar *)"AL_INVERSE_DISTANCE_CLAMPED",          AL_INVERSE_DISTANCE_CLAMPED         },
    { (ALchar *)"AL_LINEAR_DISTANCE",                   AL_LINEAR_DISTANCE                  },
    { (ALchar *)"AL_LINEAR_DISTANCE_CLAMPED",           AL_LINEAR_DISTANCE_CLAMPED          },
    { (ALchar *)"AL_EXPONENT_DISTANCE",                 AL_EXPONENT_DISTANCE                },
    { (ALchar *)"AL_EXPONENT_DISTANCE_CLAMPED",         AL_EXPONENT_DISTANCE_CLAMPED        },

    // Filter types
    { (ALchar *)"AL_FILTER_TYPE",                       AL_FILTER_TYPE                      },
    { (ALchar *)"AL_FILTER_NULL",                       AL_FILTER_NULL                      },
    { (ALchar *)"AL_FILTER_LOWPASS",                    AL_FILTER_LOWPASS                   },
    { (ALchar *)"AL_FILTER_HIGHPASS",                   AL_FILTER_HIGHPASS                  },
    { (ALchar *)"AL_FILTER_BANDPASS",                   AL_FILTER_BANDPASS                  },

    // Filter params
    { (ALchar *)"AL_LOWPASS_GAIN",                      AL_LOWPASS_GAIN                     },
    { (ALchar *)"AL_LOWPASS_GAINHF",                    AL_LOWPASS_GAINHF                   },

    // Effect types
    { (ALchar *)"AL_EFFECT_TYPE",                       AL_EFFECT_TYPE                      },
    { (ALchar *)"AL_EFFECT_NULL",                       AL_EFFECT_NULL                      },
    { (ALchar *)"AL_EFFECT_REVERB",                     AL_EFFECT_REVERB                    },
    { (ALchar *)"AL_EFFECT_CHORUS",                     AL_EFFECT_CHORUS                    },
    { (ALchar *)"AL_EFFECT_DISTORTION",                 AL_EFFECT_DISTORTION                },
    { (ALchar *)"AL_EFFECT_ECHO",                       AL_EFFECT_ECHO                      },
    { (ALchar *)"AL_EFFECT_FLANGER",                    AL_EFFECT_FLANGER                   },
    { (ALchar *)"AL_EFFECT_FREQUENCY_SHIFTER",          AL_EFFECT_FREQUENCY_SHIFTER         },
    { (ALchar *)"AL_EFFECT_VOCAL_MORPHER",              AL_EFFECT_VOCAL_MORPHER             },
    { (ALchar *)"AL_EFFECT_PITCH_SHIFTER",              AL_EFFECT_PITCH_SHIFTER             },
    { (ALchar *)"AL_EFFECT_RING_MODULATOR",             AL_EFFECT_RING_MODULATOR            },
    { (ALchar *)"AL_EFFECT_AUTOWAH",                    AL_EFFECT_AUTOWAH                   },
    { (ALchar *)"AL_EFFECT_COMPRESSOR",                 AL_EFFECT_COMPRESSOR                },
    { (ALchar *)"AL_EFFECT_EQUALIZER",                  AL_EFFECT_EQUALIZER                 },

    // Reverb params
    { (ALchar *)"AL_REVERB_DENSITY",                    AL_REVERB_DENSITY                   },
    { (ALchar *)"AL_REVERB_DIFFUSION",                  AL_REVERB_DIFFUSION                 },
    { (ALchar *)"AL_REVERB_GAIN",                       AL_REVERB_GAIN                      },
    { (ALchar *)"AL_REVERB_GAINHF",                     AL_REVERB_GAINHF                    },
    { (ALchar *)"AL_REVERB_DECAY_TIME",                 AL_REVERB_DECAY_TIME                },
    { (ALchar *)"AL_REVERB_DECAY_HFRATIO",              AL_REVERB_DECAY_HFRATIO             },
    { (ALchar *)"AL_REVERB_REFLECTIONS_GAIN",           AL_REVERB_REFLECTIONS_GAIN          },
    { (ALchar *)"AL_REVERB_REFLECTIONS_DELAY",          AL_REVERB_REFLECTIONS_DELAY         },
    { (ALchar *)"AL_REVERB_LATE_REVERB_GAIN",           AL_REVERB_LATE_REVERB_GAIN          },
    { (ALchar *)"AL_REVERB_LATE_REVERB_DELAY",          AL_REVERB_LATE_REVERB_DELAY         },
    { (ALchar *)"AL_REVERB_AIR_ABSORPTION_GAINHF",      AL_REVERB_AIR_ABSORPTION_GAINHF     },
    { (ALchar *)"AL_REVERB_ROOM_ROLLOFF_FACTOR",        AL_REVERB_ROOM_ROLLOFF_FACTOR       },
    { (ALchar *)"AL_REVERB_DECAY_HFLIMIT",              AL_REVERB_DECAY_HFLIMIT             },


    // Default
    { (ALchar *)NULL,                                   (ALenum)0                           }
};



ALAPI ALboolean ALAPIENTRY alIsExtensionPresent(const ALchar *extName)
{
    ALboolean bIsSupported = AL_FALSE;
    ALCcontext *pContext;
    const char *ptr;
    size_t len;

    if (!extName)
    {
        alSetError(AL_INVALID_VALUE);
        return AL_FALSE;
    }

    pContext = alcGetCurrentContext();
    if(!pContext)
    {
        alSetError(AL_INVALID_OPERATION);
        return AL_FALSE;
    }

    SuspendContext(pContext);

    len = strlen(extName);
    ptr = pContext->ExtensionList;
    while(ptr && *ptr)
    {
        if(strncasecmp(ptr, extName, len) == 0 &&
           (ptr[len] == '\0' || isspace(ptr[len])))
        {
            bIsSupported = AL_TRUE;
            break;
        }
        if((ptr=strchr(ptr, ' ')) != NULL)
        {
            do {
                ++ptr;
            } while(isspace(*ptr));
        }
    }

    ProcessContext(pContext);

    return bIsSupported;
}


ALAPI ALvoid * ALAPIENTRY alGetProcAddress(const ALchar *funcName)
{
    ALsizei i = 0;

    while(function[i].funcName &&
          strcmp((char*)function[i].funcName, (char*)funcName) != 0)
        i++;

    return function[i].address;
}

/* NOTE: This function must be able to run without a context! */
ALAPI ALenum ALAPIENTRY alGetEnumValue(const ALchar *enumName)
{
    ALsizei i = 0;

    while(enumeration[i].enumName &&
          strcmp(enumeration[i].enumName, enumName) != 0)
        i++;

    return enumeration[i].value;
}
