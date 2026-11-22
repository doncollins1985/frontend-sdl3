#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include <Poco/Logger.h>

#include <map>
#include <string>

/**
 * @brief SDL-based audio capturing thread.
 *
 * Uses SDL's audio API to capture PCM data from any supported drivers.
 */
class AudioCaptureImpl
{
public:
    AudioCaptureImpl();

    ~AudioCaptureImpl();

    /**
     * @brief Returns a map of available recording devices.
     * @return A vector of available audio device IDs and names.
     */
    std::map<int, std::string> AudioDeviceList();

    /**
     * @brief Starts audio capturing with the first available device.
     * @param audioDeviceIndex The initial audio device ID to capture from. Use -1 to select the implementation's
     *                      default device.
     */
    void StartRecording(int audioDeviceIndex);

    /**
     * @brief Stops audio recording.
     */
    void StopRecording();

    /**
     * @brief Switches to the next available audio recording device.
     */
    void NextAudioDevice();

    /**
     * @brief Activates the audio device with the given idnex for recording.
     * @param index The index, as listed by @a AudioDeviceList()
     */
    void AudioDeviceIndex(int index);

    /**
     * @brief Returns the currently used Audio device index.
     * @return The index of the current audio device, as listed by @a AudioDeviceList()
     */
    int AudioDeviceIndex() const;

    /**
     * @brief Retrieves the current audio device name.
     * @return The name of the currently selected audio recording device.
     */
    std::string AudioDeviceName() const;

    /**
     * @brief Asks the capture client to fill projectM's audio buffer for the next frame.
     *
     * SDL2 uses async callbacks to directly fill the staging audio buffer(s) as soon as new data is available.
     * SDL3 has no audio callback and polls the recording stream here, once per frame.
     */
    void FillBuffer();

    /**
     * @brief Refreshes the audio device list (e.g. after a hotplug event).
     */
    void RefreshDeviceList();

protected:
    /**
     * @brief Opens the SDL audio device with the currently selected index.
     * @return
     */
    bool OpenAudioDevice();

    /**
     * @brief Passes new audio data to the notification center.
     *
     * Called from the SDL2 audio thread via the audio callback, and from FillBuffer() on SDL3.
     *
     * @param userData Pointer to the AudioCaptureImpl instance.
     * @param stream The captured audio data.
     * @param len The number of valid bytes in @a stream.
     */
    static void AudioInputCallback(void* userData, unsigned char* stream, int len);

    int32_t _currentAudioDeviceIndex{-1}; //!< Currently selected audio device index.
#ifdef USE_SDL3
    SDL_AudioStream* _recordingStream{nullptr}; //!< SDL3 audio stream for recording.
#else
    SDL_AudioDeviceID _currentAudioDeviceID{0}; //!< Device ID of the currently opened audio device.
#endif
    uint32_t _channels{2};

    constexpr static uint32_t _requestedSampleFrequency{44100}; //!< Requested sample frequency. Currently hardcoded as 44100 Hz, as this is what the spectrum analyzer expects.
    uint32_t _requestedSampleCount{44100U / 60U}; //!< Requested audio buffer size. Determines how often SDL will call AudioInputCallback() with new data, and how much data is delivered on each call.

    Poco::Logger& _logger{Poco::Logger::get("AudioCapture.SDL")}; //!< The class logger.
};
