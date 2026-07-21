#include "AudioCaptureImpl_SDL.h"

#include <Poco/Util/Application.h>

#include <projectM-4/projectM.h>
#include <cmath>
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

    // Include monitor/loopback devices (system audio output capture) in the recording device list.
    // This is a music visualizer — capturing system audio is the primary use case.
    //
    // SDL_HINT_AUDIO_INCLUDE_MONITORS is only implemented by the PulseAudio driver,
    // so we also force SDL3 to use PulseAudio (which PipeWire provides via its
    // pulse-compat layer) to ensure monitor sources appear.
    SDL_SetHint(SDL_HINT_AUDIO_INCLUDE_MONITORS, "1");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "pulseaudio");
    SDL_InitSubSystem(SDL_INIT_AUDIO);
}

AudioCaptureImpl::~AudioCaptureImpl()
{
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::map<int, std::string> AudioCaptureImpl::AudioDeviceList()
{
    if (_deviceListDirty)
    {
        RebuildDeviceList();
    }
    return _cachedDeviceList;
}

void AudioCaptureImpl::RebuildDeviceList()
{
    _cachedDeviceList.clear();
    _cachedDeviceList.insert({-1, "Default system audio output"});

    int recordingDeviceCount = 0;
    SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&recordingDeviceCount);

    if (devices) {
        for (int i = 0; i < recordingDeviceCount; i++)
        {
            const char* deviceName = SDL_GetAudioDeviceName(devices[i]);
            if (deviceName)
            {
                std::string name(deviceName);
                // Distinguish monitor/loopback sources (system audio output) from
                // physical microphones. Detection strategies vary by audio backend:
                //
                // - PipeWire: monitors are playback devices listed as recording-capable;
                //   SDL_IsAudioDevicePlayback() returns true.
                // - PulseAudio: monitors appear as recording devices with "Monitor of"
                //   in their name; SDL_IsAudioDevicePlayback() returns false.
                if (SDL_IsAudioDevicePlayback(devices[i]) ||
                    name.find("Monitor of") != std::string::npos ||
                    name.find("Monitor Source") != std::string::npos)
                {
                    name += " (System Audio)";
                }
                else
                {
                    name += " (Microphone)";
                }
                _cachedDeviceList.insert(std::make_pair(i, name));
            }
            else
            {
                poco_error_f2(_logger, "Could not get device name for device index %d: %s", i, std::string(SDL_GetError()));
            }
        }
        SDL_free(devices);
    }

    _deviceListDirty = false;
}

void AudioCaptureImpl::RefreshDeviceList()
{
    _deviceListDirty = true;
    RebuildDeviceList();

    // Check if the current device index is still valid in the new list.
    int maxIndex = static_cast<int>(_cachedDeviceList.size()) - 2; // exclude the -1 default entry
    if (_currentAudioDeviceIndex > maxIndex)
    {
        // Current index is out of bounds — try to find the device by its raw name.
        int newIndex = -1;
        if (!_currentAudioDeviceRawName.empty())
        {
            int count = 0;
            SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
            if (devices)
            {
                for (int i = 0; i < count; i++)
                {
                    const char* name = SDL_GetAudioDeviceName(devices[i]);
                    if (name && _currentAudioDeviceRawName == name)
                    {
                        newIndex = i;
                        break;
                    }
                }
                SDL_free(devices);
            }
        }

        if (newIndex < 0 && _currentAudioDeviceIndex != -1)
        {
            poco_information_f1(_logger,
                                "Audio device \"%s\" was removed. Switching to default device.",
                                _currentAudioDeviceRawName);
        }

        StopRecording();
        StartRecording(_projectMHandle, newIndex);
    }
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

    int count = static_cast<int>(AudioDeviceList().size()) - 1; // exclude the -1 default entry

    // Will wrap around to default capture device (-1).
    int nextAudioDeviceId = ((_currentAudioDeviceIndex + 2) % (count + 1)) - 1;

    StartRecording(_projectMHandle, nextAudioDeviceId);
}

void AudioCaptureImpl::AudioDeviceIndex(int index)
{
    int maxIndex = static_cast<int>(AudioDeviceList().size()) - 2; // exclude the -1 default entry

    if (index >= -1 && index <= maxIndex)
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
        return "Default system audio output";
    }
}

bool AudioCaptureImpl::OpenAudioDevice()
{
    SDL_AudioSpec requestedSpecs{};
    
    requestedSpecs.freq = _requestedSampleFrequency;
    requestedSpecs.format = SDL_AUDIO_F32LE;
    requestedSpecs.channels = 2;

    SDL_AudioDeviceID deviceID = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    const char *deviceName = "System default recording device";

    if (_currentAudioDeviceIndex == -1) {
        // Default device: prefer a monitor/loopback source (system audio output) for
        // music visualization. Fall back to the default recording device if none found.
        //
        // Two-pass selection: first look for Speaker/Analog monitors (what the user
        // actually hears), then fall back to any monitor (e.g. HDMI/DisplayPort).
        int count = 0;
        SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
        if (devices) {
            auto isMonitor = [&](int i) -> bool {
                const char* n = SDL_GetAudioDeviceName(devices[i]);
                std::string ns(n ? n : "");
                return SDL_IsAudioDevicePlayback(devices[i]) ||
                       ns.find("Monitor of") != std::string::npos ||
                       ns.find("Monitor Source") != std::string::npos;
            };
            auto prefersSpeaker = [&](int i) -> bool {
                const char* n = SDL_GetAudioDeviceName(devices[i]);
                std::string ns(n ? n : "");
                // Prefer the user's actual listening device over HDMI/DisplayPort.
                return ns.find("Speaker") != std::string::npos ||
                       ns.find("Headphone") != std::string::npos ||
                       ns.find("Headset") != std::string::npos ||
                       ns.find("Analog") != std::string::npos;
            };
            auto isHDMI = [&](int i) -> bool {
                const char* n = SDL_GetAudioDeviceName(devices[i]);
                std::string ns(n ? n : "");
                return ns.find("HDMI") != std::string::npos ||
                       ns.find("DisplayPort") != std::string::npos;
            };

            // Pass 1: prefer Speaker/Headphone/Analog monitors
            for (int i = 0; i < count; i++) {
                if (isMonitor(i) && prefersSpeaker(i)) {
                    deviceID = devices[i];
                    deviceName = SDL_GetAudioDeviceName(deviceID);
                    break;
                }
            }

            // Pass 2: fall back to any non-HDMI monitor
            if (deviceID == SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
                for (int i = 0; i < count; i++) {
                    if (isMonitor(i) && !isHDMI(i)) {
                        deviceID = devices[i];
                        deviceName = SDL_GetAudioDeviceName(deviceID);
                        break;
                    }
                }
            }

            // Pass 3: last resort, any monitor (including HDMI)
            if (deviceID == SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
                for (int i = 0; i < count; i++) {
                    if (isMonitor(i)) {
                        deviceID = devices[i];
                        deviceName = SDL_GetAudioDeviceName(deviceID);
                        break;
                    }
                }
            }

            if (deviceID != SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
                poco_debug_f1(_logger, "Default audio device: selected monitor source \"%s\".",
                              std::string(deviceName ? deviceName : "Unknown"));
            }

            SDL_free(devices);
        }
    }
    else if (_currentAudioDeviceIndex >= 0) {
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

    // Store raw device name for recovery on hotplug removal.
    _currentAudioDeviceRawName = deviceName ? deviceName : "";
    
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

            // Track peak audio level for the level indicator.
            float peak = 0.0f;
            for (int i = 0; i < bytesRead / static_cast<int>(sizeof(float)); i++) {
                float absVal = std::fabs(buffer[i]);
                if (absVal > peak) peak = absVal;
            }
            instance->_currentAudioLevel = peak;
        }
    }
}