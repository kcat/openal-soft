#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <thread>

ALCcontext* alcontext = nullptr;
ALCdevice* aldevice = nullptr;
ALCint attrs[16];

typedef ALsizei(*alSourceFunc_t)(ALuint abo, ALfloat* to_fill, ALsizei size, ALvoid* usr_ptr /*, ALuint alignment, Alenum format, ...*/);

typedef ALvoid(*PalBufferCallbackSOFT)(ALuint buffer, ALenum format, ALsizei freq, ALuint flags, alSourceFunc_t callback, ALvoid* usr_ptr);

PalBufferCallbackSOFT alBufferCallbackSOFT = nullptr;


volatile size_t cur = 0;
constexpr double freq = 220;
constexpr double interv = 1 / freq * 2 * 3.1415926535;
float* samples;
ALsizei callback(ALuint abo, ALfloat* to_fill, ALsizei size, ALvoid* usr_ptr) {
	size_t start = cur % 48000;
	size_t frames = size / (4);
	for (size_t i = 0; i < frames; ++i) {
		to_fill[i] = sin(interv*(cur + i));
	}
	cur += frames;
	return size;
}

void gen_buffer() {
	samples = new float[48000];
	for (size_t i = 0; i < 48000; ++i) {
		samples[i] = sin(interv*i);
	}
}

int main() {
	gen_buffer();
	alBufferCallbackSOFT = (PalBufferCallbackSOFT)alcGetProcAddress(nullptr, "alBufferCallbackSOFT");
	aldevice = alcOpenDevice(nullptr);
	//Quick and dirty: assume stereo floating point format at 48000Hz is available on device.
	attrs[0] = ALC_FORMAT_CHANNELS_SOFT;
	attrs[1] = ALC_MONO_SOFT;
	attrs[2] = ALC_FORMAT_TYPE_SOFT;
	attrs[3] = ALC_FLOAT_SOFT;
	attrs[4] = ALC_FREQUENCY;
	attrs[5] = 48000;
	attrs[6] = 0;
	alcontext = alcCreateContext(aldevice, attrs);
	alcMakeContextCurrent(alcontext);
	ALuint buffers[2];
	ALuint source;
	alGenBuffers(2, buffers);
	alBufferCallbackSOFT(buffers[0], AL_FORMAT_MONO_FLOAT32, 48000, 0, callback, nullptr);
	alBufferData(buffers[1], AL_FORMAT_MONO_FLOAT32, samples, 48000 * 4, 48000);
	alGenSources(1, &source);
	alSourcei(source, AL_BUFFER, buffers[0]);
//	alSourcei(source, AL_LOOPING, AL_TRUE);
	alSourcePlay(source);
	std::this_thread::sleep_for(std::chrono::hours(10));
	alSourceStop(source);
	alcDestroyContext(alcontext);
	alcCloseDevice(aldevice);
	return 0;
}



