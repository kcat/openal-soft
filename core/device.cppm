module;

#include "core/device.h"

export module core.device;

export {

    using ::MinOutputRate;
    using ::MaxOutputRate;
    using ::DefaultOutputRate;

    using ::DefaultUpdateSize;
    using ::DefaultNumUpdates;

    using ::DeviceType;
    using ::RenderMode;
    using ::StereoEncoding;

    using ::DistanceComp;

    using ::AmbiRotateMatrix;

    using ::AmbiDecPostProcess;
    using ::HrtfPostProcess;
    using ::UhjPostProcess;
    using ::TsmePostProcess;
    using ::StablizerPostProcess;
    using ::Bs2bPostProcess;
    using ::PostProcess;

    using ::DeviceFlag;
    using ::DeviceState;

    using ::DeviceBase;

    using ::GetMixerThreadName;
    using ::GetRecordThreadName;

    using ::Channel;

    using ::DevFmtType;
    using ::DevFmtChannels;
    using ::MaxOutputChannels;

    using ::DevFmtTypeTraits;
    using ::DevFmtType_t;

    using ::BytesFromDevFmt;
    using ::ChannelsFromDevFmt;
    using ::FrameSizeFromDevFmt;

    using ::DevFmtTypeString;
    using ::DevFmtChannelsString;

    using ::DevAmbiLayout;
    using ::DevAmbiScaling;

}
