#pragma once
#include <defs.hpp>

#if GLOBED_VOICE_SUPPORT

#include "frame.hpp"
#include "sample_queue.hpp"
#include "decoder.hpp"
#include "volume_estimator.hpp"

class AudioStream {
public:
    bool starving = false; // true if there aren't enough samples in the queue

    AudioStream(AudioDecoder&& decoder);
    ~AudioStream();

    // prevent copying since we manually free the sound
    AudioStream(const AudioStream&) = delete;
    AudioStream operator=(const AudioStream& other) = delete;

    // allow moving
    AudioStream(AudioStream&& other) noexcept {
        sound = other.sound;
        channel = other.channel;
        other.sound = nullptr;
        other.channel = nullptr;

        queue = std::move(other.queue);
        decoder = std::move(other.decoder);
        estimator = std::move(other.estimator);
    }

    AudioStream& operator=(AudioStream&& other) noexcept {
        if (this != &other) {
            if (this->sound) {
                this->sound->release();
            }

            if (this->channel) {
                this->channel->stop();
            }

            this->sound = other.sound;
            this->channel = other.channel;

            other.sound = nullptr;
            other.channel = nullptr;

            queue = std::move(other.queue);
            decoder = std::move(other.decoder);
            estimator = std::move(other.estimator);
        }

        return *this;
    }

    // start playing this stream
    void start();
    // write an audio frame to this stream
    void writeData(const EncodedAudioFrame& frame);
    // write raw audio data to this stream
    void writeData(const float* pcm, size_t samples);

    // set the volume of the stream (0.0f - 1.0f, beyond 1.0f amplifies)
    void setVolume(float volume);

    // get how loud the sound is being played
    float getLoudness();

private:
    FMOD::Sound* sound = nullptr;
    FMOD::Channel* channel = nullptr;
    AudioSampleQueue queue;
    AudioDecoder decoder;
    VolumeEstimator estimator;
    float volume = 0.f;
};


#endif // GLOBED_VOICE_SUPPORT
