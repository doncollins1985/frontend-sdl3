#include "AudioCaptureImpl_SDL.h"

#include "notifications/AudioDataAvailableNotification.h"

#include <Poco/NotificationCenter.h>
#include <Poco/Util/Application.h>

#ifdef USE_SDL3
#include <vector>
#endif

#ifdef USE_SDL3
namespace {
/**
 * @brief Returns the number of available audio recording devices.
 *
 * SDL3 doesn't offer a function to only query the device count, so the device
 * list has to be enumerated and freed again.
 */
int RecordingDeviceCount()
{
    int deviceCount = 0;
    auto deviceIds = SDL_GetAudioRecordingDevices(&deviceCount);
    if (deviceIds)
    {
        SDL_free(deviceIds);
    }
    return deviceCount;
}
} // namespace
#endif

AudioCaptureImpl::AudioCaptureImpl()
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
    StopRecording();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::map<int, std::string> AudioCaptureImpl::AudioDeviceList()
{
    std::map<int, std::string> deviceList{
        {-1, "Default recording device"}};

#ifdef USE_SDL3
    int deviceCount = 0;
    auto deviceIds = SDL_GetAudioRecordingDevices(&deviceCount);
    if (deviceIds)
    {
        for (int i = 0; i < deviceCount; i++)
        {
            auto deviceName = SDL_GetAudioDeviceName(deviceIds[i]);
            if (deviceName)
            {
                deviceList.insert(std::make_pair(i, deviceName));
            }
            else
            {
                poco_error_f2(_logger, "Could not get device name for device ID %d: %s", i, std::string(SDL_GetError()));
            }
        }
        SDL_free(deviceIds);
    }
#else
    auto recordingDeviceCount = SDL_GetNumAudioDevices(true);

    for (int i = 0; i < recordingDeviceCount; i++)
    {
        auto deviceName = SDL_GetAudioDeviceName(i, true);
        if (deviceName)
        {
            deviceList.insert(std::make_pair(i, deviceName));
        }
        else
        {
            poco_error_f2(_logger, "Could not get device name for device ID %d: %s", i, std::string(SDL_GetError()));
        }
    }
#endif

    return deviceList;
}

void AudioCaptureImpl::StartRecording(int audioDeviceIndex)
{
    _currentAudioDeviceIndex = audioDeviceIndex;

    poco_debug_f1(_logger, "Using SDL audio driver \"%s\".", std::string(SDL_GetCurrentAudioDriver()));

    if (OpenAudioDevice())
    {
        poco_debug(_logger, "Started audio recording.");
    }
}

void AudioCaptureImpl::StopRecording()
{
#ifdef USE_SDL3
    if (_recordingStream)
    {
        SDL_DestroyAudioStream(_recordingStream);
        _recordingStream = nullptr;
        poco_debug(_logger, "Stopped audio recording and closed device.");
    }
#else
    if (_currentAudioDeviceID)
    {
        SDL_PauseAudioDevice(_currentAudioDeviceID, true);
        SDL_CloseAudioDevice(_currentAudioDeviceID);
        _currentAudioDeviceID = 0;
        poco_debug(_logger, "Stopped audio recording and closed device.");
    }
#endif
}

void AudioCaptureImpl::NextAudioDevice()
{
    StopRecording();

#ifdef USE_SDL3
    int nextAudioDeviceId = ((_currentAudioDeviceIndex + 2) % (RecordingDeviceCount() + 1)) - 1;
#else
    // Will wrap around to default capture device (-1).
    int nextAudioDeviceId = ((_currentAudioDeviceIndex + 2) % (SDL_GetNumAudioDevices(true) + 1)) - 1;
#endif

    StartRecording(nextAudioDeviceId);
}

void AudioCaptureImpl::AudioDeviceIndex(int index)
{
#ifdef USE_SDL3
    if (index >= -1 && index < RecordingDeviceCount())
#else
    if (index >= -1 && index < SDL_GetNumAudioDevices(true))
#endif
    {
        StopRecording();
        _currentAudioDeviceIndex = index;
        StartRecording(index);
    }
}

int AudioCaptureImpl::AudioDeviceIndex() const
{
    return _currentAudioDeviceIndex;
}

std::string AudioCaptureImpl::AudioDeviceName() const
{
#ifdef USE_SDL3
    if (_currentAudioDeviceIndex >= 0)
    {
        int deviceCount = 0;
        auto deviceIds = SDL_GetAudioRecordingDevices(&deviceCount);
        if (deviceIds && _currentAudioDeviceIndex < deviceCount)
        {
            auto name = SDL_GetAudioDeviceName(deviceIds[_currentAudioDeviceIndex]);
            std::string result(name ? name : "Unknown device");
            SDL_free(deviceIds);
            return result;
        }
        if (deviceIds)
        {
            SDL_free(deviceIds);
        }
    }
    return "Default recording device";
#else
    if (_currentAudioDeviceIndex >= 0)
    {
        return SDL_GetAudioDeviceName(_currentAudioDeviceIndex, true);
    }
    return "Default recording device";
#endif
}

bool AudioCaptureImpl::OpenAudioDevice()
{
#ifdef USE_SDL3
    SDL_AudioSpec spec{};
    spec.freq = static_cast<int>(_requestedSampleFrequency);
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;

    SDL_AudioDeviceID deviceId = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    if (_currentAudioDeviceIndex >= 0)
    {
        int deviceCount = 0;
        auto deviceIds = SDL_GetAudioRecordingDevices(&deviceCount);
        if (deviceIds && _currentAudioDeviceIndex < deviceCount)
        {
            deviceId = deviceIds[_currentAudioDeviceIndex];
        }
        if (deviceIds)
        {
            SDL_free(deviceIds);
        }
    }

    _recordingStream = SDL_OpenAudioDeviceStream(deviceId, &spec, nullptr, nullptr);
    if (!_recordingStream)
    {
        poco_error_f2(_logger, R"(Failed to open audio recording device (ID %?d): %s)",
                      _currentAudioDeviceIndex,
                      std::string(SDL_GetError()));
        return false;
    }

    _channels = spec.channels;

    SDL_ResumeAudioStreamDevice(_recordingStream);

    auto deviceName = SDL_GetAudioDeviceName(deviceId);
    poco_information_f4(_logger, R"(Opened audio recording device "%s" (ID %?d) with %?d channels at %?d Hz.)",
                        std::string(deviceName ? deviceName : "System default recording device"),
                        _currentAudioDeviceIndex,
                        spec.channels,
                        spec.freq);

    return true;
#else
    SDL_AudioSpec requestedSpecs{};
    SDL_AudioSpec actualSpecs{};

    requestedSpecs.freq = _requestedSampleFrequency;
    requestedSpecs.format = AUDIO_F32;
    requestedSpecs.channels = 2;
    requestedSpecs.samples = _requestedSampleCount;
    requestedSpecs.callback = AudioCaptureImpl::AudioInputCallback;
    requestedSpecs.userdata = this;

    // Will be NULL on error, which happens if the requested index is -1. This automatically selects the default device.
    auto deviceName = SDL_GetAudioDeviceName(_currentAudioDeviceIndex, true);
    _currentAudioDeviceID = SDL_OpenAudioDevice(deviceName, true, &requestedSpecs, &actualSpecs, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);

    if (_currentAudioDeviceID == 0)
    {
        poco_error_f3(_logger, R"(Failed to open audio device "%s" (ID %?d): %s)",
                      std::string(deviceName != nullptr ? deviceName : "System default recording device"),
                      _currentAudioDeviceIndex,
                      std::string(SDL_GetError()));
        return false;
    }

    _channels = actualSpecs.channels;

    poco_information_f4(_logger, R"(Opened audio recording device "%s" (ID %?d) with %?d channels at %?d Hz.)",
                        std::string(deviceName != nullptr ? deviceName : "System default recording device"),
                        _currentAudioDeviceIndex,
                        actualSpecs.channels,
                        actualSpecs.freq);

    return true;
#endif
}

void AudioCaptureImpl::AudioInputCallback(void* userData, unsigned char* stream, int len)
{
    poco_assert_dbg(userData);

    if (len < static_cast<int>(sizeof(float)))
    {
        return;
    }

    auto instance = reinterpret_cast<AudioCaptureImpl*>(userData);

    unsigned int samples = len / sizeof(float);

    Poco::NotificationCenter::defaultCenter().postNotification(
        new AudioDataAvailableNotification(instance->_channels,
                                           reinterpret_cast<float*>(stream),
                                           samples));
}

void AudioCaptureImpl::FillBuffer()
{
#ifdef USE_SDL3
    if (!_recordingStream)
    {
        return;
    }

    int available = SDL_GetAudioStreamAvailable(_recordingStream);
    if (available < static_cast<int>(sizeof(float)))
    {
        return;
    }

    std::vector<float> buffer(static_cast<size_t>(available) / sizeof(float));
    int bytesRead = SDL_GetAudioStreamData(_recordingStream, buffer.data(), available);

    if (bytesRead > 0)
    {
        // Reuse the same conversion and notification code as the SDL2 callback.
        AudioInputCallback(this, reinterpret_cast<unsigned char*>(buffer.data()), bytesRead);
    }
#endif
}

void AudioCaptureImpl::RefreshDeviceList()
{
    // The device list is re-enumerated on the next AudioDeviceList() call.
}
