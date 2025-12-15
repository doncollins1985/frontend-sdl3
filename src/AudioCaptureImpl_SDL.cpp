#include "AudioCaptureImpl_SDL.h"

#include <Poco/Util/Application.h>

#include <projectM-4/projectM.h>
#include <vector>

AudioCaptureImpl::AudioCaptureImpl()
    : _requestedSampleCount(projectm_pcm_get_max_samples())
{
    auto targetFps = Poco::Util::Application::instance().config().getUInt("projectM.fps", 60);
    if (targetFps > 0)
    {
        _requestedSampleCount = std::min(_requestedSampleFrequency / targetFps, _requestedSampleCount);
        // Don't let the buffer get too small to prevent excessive updates calls.
        // 300 samples is enough for 144 FPS.
        _requestedSampleCount = std::max(_requestedSampleCount, 300U);
    }

#ifdef SDL_HINT_AUDIO_INCLUDE_MONITORS
    SDL_SetHint(SDL_HINT_AUDIO_INCLUDE_MONITORS, "1");
#endif
    SDL_InitSubSystem(SDL_INIT_AUDIO);
}

AudioCaptureImpl::~AudioCaptureImpl()
{
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::map<int, std::string> AudioCaptureImpl::AudioDeviceList()
{
    std::map<int, std::string> deviceList{
        {-1, "Default capturing device"}};

    int recordingDeviceCount = 0;
    SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&recordingDeviceCount);

    if (devices) {
        for (int i = 0; i < recordingDeviceCount; i++)
        {
            const char* deviceName = SDL_GetAudioDeviceName(devices[i]);
            if (deviceName)
            {
                deviceList.insert(std::make_pair(i, deviceName));
            }
            else
            {
                poco_error_f2(_logger, "Could not get device name for device index %d: %s", i, std::string(SDL_GetError()));
            }
        }
        SDL_free(devices);
    }

    return deviceList;
}

void AudioCaptureImpl::StartRecording(projectm* projectMHandle, int audioDeviceIndex)
{
    _projectMHandle = projectMHandle;
    _currentAudioDeviceIndex = audioDeviceIndex;

    const char* driver = SDL_GetCurrentAudioDriver();
    poco_debug_f1(_logger, "Using SDL audio driver \"%s\".", std::string(driver ? driver : "unknown"));

    if (OpenAudioDevice())
    {
        SDL_ResumeAudioStreamDevice(_currentAudioStream);
        poco_debug(_logger, "Started audio recording.");
    }
}

void AudioCaptureImpl::StopRecording()
{
    if (_currentAudioStream)
    {
        SDL_DestroyAudioStream(_currentAudioStream);
        _currentAudioStream = nullptr;

        poco_debug(_logger, "Stopped audio recording and closed device.");
    }
}

void AudioCaptureImpl::NextAudioDevice()
{
    StopRecording();

    int count = 0;
    SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
    if (devices) SDL_free(devices);

    // Will wrap around to default capture device (-1).
    int nextAudioDeviceId = ((_currentAudioDeviceIndex + 2) % (count + 1)) - 1;

    StartRecording(_projectMHandle, nextAudioDeviceId);
}

void AudioCaptureImpl::AudioDeviceIndex(int index)
{
    int count = 0;
    SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
    if (devices) SDL_free(devices);

    if (index >= -1 && index < count)
    {
        StopRecording();
        _currentAudioDeviceIndex = index;
        StartRecording(_projectMHandle, index);
    }
}

int AudioCaptureImpl::AudioDeviceIndex() const
{
    return _currentAudioDeviceIndex;
}

std::string AudioCaptureImpl::AudioDeviceName() const
{
    if (_currentAudioDeviceIndex >= 0)
    {
        int count = 0;
        SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
        std::string name = "Unknown Device";
        if (devices) {
             if (_currentAudioDeviceIndex < count) {
                 const char* n = SDL_GetAudioDeviceName(devices[_currentAudioDeviceIndex]);
                 if (n) name = n;
             }
             SDL_free(devices);
        }
        return name;
    }
    else
    {
        return "Default capturing device";
    }
}

bool AudioCaptureImpl::OpenAudioDevice()
{
    SDL_AudioSpec requestedSpecs{};
    
    requestedSpecs.freq = _requestedSampleFrequency;
    requestedSpecs.format = SDL_AUDIO_F32LE;
    requestedSpecs.channels = 2;

    SDL_AudioDeviceID deviceID = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    const char *deviceName = "System default capturing device";

    if (_currentAudioDeviceIndex >= 0) {
        int count = 0;
        SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
        if (devices) {
            if (_currentAudioDeviceIndex < count) {
                deviceID = devices[_currentAudioDeviceIndex];
                deviceName = SDL_GetAudioDeviceName(deviceID);
            }
            SDL_free(devices);
        }
    }
    
    _currentAudioStream = SDL_OpenAudioDeviceStream(deviceID, &requestedSpecs, AudioCaptureImpl::AudioInputCallback, this);

    if (!_currentAudioStream)
    {
        poco_error_f3(_logger, R"(Failed to open audio device \"%s\" (Index %?d): %s)",
                      std::string(deviceName ? deviceName : "Unknown"),
                      _currentAudioDeviceIndex,
                      std::string(SDL_GetError()));
        return false;
    }

    _channels = requestedSpecs.channels;

    poco_information_f4(_logger, R"(Opened audio recording device \"%s\" (Index %?d) with %?d channels at %?d Hz.)",
                        std::string(deviceName ? deviceName : "Unknown"),
                        _currentAudioDeviceIndex,
                        _channels,
                        requestedSpecs.freq);

    return true;
}

void AudioCaptureImpl::AudioInputCallback(void* userData, SDL_AudioStream* stream, int additional_amount, int total_amount)
{
    poco_assert_dbg(userData);
    auto instance = reinterpret_cast<AudioCaptureImpl*>(userData);
    
    if (total_amount > 0) {
        std::vector<float> buffer(total_amount / sizeof(float));
        int bytesRead = SDL_GetAudioStreamData(stream, buffer.data(), total_amount);
        
        if (bytesRead > 0) {
            unsigned int samples = bytesRead / sizeof(float) / instance->_channels;
            projectm_pcm_add_float(instance->_projectMHandle, buffer.data(), samples,
                                   static_cast<projectm_channels>(instance->_channels));
        }
    }
}