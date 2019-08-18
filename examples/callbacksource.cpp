#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <atomic>
#include <iostream>

ALCcontext* alcontext = nullptr;
ALCdevice* aldevice = nullptr;
ALCint attrs[16];

std::atomic_bool done = false;

typedef ALsizei(*alSourceFunc_t)(ALuint abo, ALbyte* to_fill, ALsizei stride, ptrdiff_t samples, ALvoid* usr_ptr /*, ALuint alignment, Alenum format, ...*/);
typedef ALvoid(*PalBufferCallbackSOFT)(ALuint buffer, ALenum format, ALsizei freq, ALuint flags, alSourceFunc_t callback, ALvoid* usr_ptr);

PalBufferCallbackSOFT alBufferCallbackSOFT = nullptr;

volatile size_t cur = 0;
constexpr double freq = 240;
constexpr double nu = 1 / freq;
constexpr double interv = nu *4* 3.1415926535;

volatile char type = 'n';

ALsizei callback(ALuint abo, ALbyte* to_fill, ALsizei stride, ptrdiff_t samples, ALvoid* usr_ptr) {
	for (size_t i = 0; i < samples; ++i) {
		switch (type) {
		case 's':
			*((float*)to_fill) = sin(interv*(cur + i));
			break;
		case 'q':
			*((float*)to_fill) = (((((i + cur) / 200) & 0x01) << 0x01) - 1.0) * 0.125;
			break;
		case 'w':
			*((float*)to_fill) = ((((i + cur) % 200) / 200.0) - 0.5)*0.25;
			break;
		case 'n':
			*((float*)to_fill) = 0;
			break;
		default:
			*((float*)to_fill) = 0;
			break;
		}
		to_fill += stride;
	}
	cur += samples;
	return samples;
}

GLFWwindow* window;

void glfwErrorCallback(int err_code, const char* info) {
	std::cerr << "Error " << err_code << ":\n" << info << '\n' << std::endl;
}
GLFWcharfun;

void Key(GLFWwindow * window, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS || action==GLFW_REPEAT) {
		switch (key) {
		case GLFW_KEY_Q:
			type = 'q';
			break;
		case GLFW_KEY_W:
			type = 'w';
			break;
		case GLFW_KEY_S:
			type = 's';
			break;
		default:
			type = 'n';
			break;
		}
	}
	else if(action == GLFW_RELEASE) {
		switch (key) {
		case GLFW_KEY_Q:
			if (type == 'q')
				type = 'n';
			break;
		case GLFW_KEY_W:
			if (type == 'w')
				type = 'n';
			break;
		case GLFW_KEY_S:
			if (type == 's')
				type = 'n';
			break;
		}
	}
}

void Close(GLFWwindow* window) {
	done.store(true);
}

void init_gl() {
	glfwSetErrorCallback(glfwErrorCallback);
	std::clog << "GLFW Version string:\n"
		<< glfwGetVersionString() << '\n' << std::endl;
	if (!glfwInit()) {
		std::cerr << "Error initializing glfw!\n" << std::endl;
		exit(EXIT_FAILURE);
	}
	std::clog << "Creating OpenGL Context!\n" << std::endl;
	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow(512, 512, "Input context", nullptr, nullptr);
	if (!window) {
		std::cerr << "Failed creating GLFW window!";
		return;
	}
	glfwSetKeyCallback(window, Key);
	glfwSetWindowCloseCallback(window, Close);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	GLenum glewinit = glewInit();
	if (glewinit != GLEW_OK) {
		std::cerr << "Error initializing glew!:\n"
			<< glewGetErrorString(glewinit) << '\n' << std::endl;
		return;
	}
	std::clog << "GLEW initialized!\n" << std::endl;
	glViewport(0, 0, 512, 512);
}

void get_color(float& r, float& g, float& b, char key) {
	switch (key) {
	case 's':
		r = 1.0;
		g = 0.0;
		b = 0.0;
		break;
	case 'q':
		r = 0.0;
		g = 1.0;
		b = 0.0;
		break;
	case 'w':
		r = 0.0;
		g = 0.0;
		b = 1.0;
		break;
	default:
		r = 0.0;
		g = 0.0;
		b = 0.0;
	}
}

int main() {
	init_gl();
	alBufferCallbackSOFT = (PalBufferCallbackSOFT)alcGetProcAddress(nullptr, "alBufferCallbackSOFT");
	aldevice = alcOpenDevice(nullptr);
	//Quick and dirty: assume stereo floating point format at 48000Hz is available on device.
	attrs[0] = ALC_FORMAT_CHANNELS_SOFT;
	attrs[1] = ALC_STEREO_SOFT;
	attrs[2] = ALC_FORMAT_TYPE_SOFT;
	attrs[3] = ALC_FLOAT_SOFT;
	attrs[4] = ALC_FREQUENCY;
	attrs[5] = 48000;
	attrs[6] = ALC_HRTF_SOFT;
	attrs[7] = ALC_TRUE;
	attrs[8] = 0;
	alcontext = alcCreateContext(aldevice, attrs);
	alcMakeContextCurrent(alcontext);
	ALuint buffer;
	ALuint source;
	alGenBuffers(1, &buffer);
	alBufferCallbackSOFT(buffer, AL_FORMAT_MONO_FLOAT32, 48000, 0, callback, nullptr);
	alGenSources(1, &source);
	alSourcei(source, AL_BUFFER, buffer);
	alSourcePlayv(1,&source);
	while (!done.load(std::memory_order_relaxed)) {
		float r, g, b;
		get_color(r, g, b, type);
		glClearColor(r, g, b, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		glfwSwapBuffers(window);
		glfwWaitEvents();
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	alSourceStop(source);
	alDeleteSources(1, &source);
	alDeleteBuffers(1, &buffer);
	alcDestroyContext(alcontext);
	alcCloseDevice(aldevice);
	return 0;
}



