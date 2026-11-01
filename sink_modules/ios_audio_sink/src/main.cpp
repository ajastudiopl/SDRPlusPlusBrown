#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <dsp/sink.h>
#include <utils/flog.h>
#include <config.h>
#include <core.h>
#include <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <cstring>

SDRPP_MOD_INFO{
    /* Name:            */ "ios_audio_sink",
    /* Description:     */ "RemoteIO audio sink for iOS",
    /* Author:          */ "SDR++ Brown",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

extern "C" void sdrppIosActivateAudioSession();

class IosAudioSink : public SinkManager::Sink {
public:
    IosAudioSink(SinkManager::Stream* stream, std::string streamName) {
        _stream = stream;
        sink.owner = this;
        sink.init(_stream->sinkOut);
    }

    ~IosAudioSink() {
        stop();
    }

    void start() {
        if (running) { return; }
        sdrppIosActivateAudioSession();
        _stream->setSampleRate(sampleRate);

        AudioComponentDescription desc = {};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_RemoteIO;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent comp = AudioComponentFindNext(NULL, &desc);
        if (!comp || AudioComponentInstanceNew(comp, &unit) != noErr) {
            flog::error("iOS audio: cannot create RemoteIO unit");
            return;
        }

        AudioStreamBasicDescription fmt = {};
        fmt.mSampleRate = sampleRate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        fmt.mFramesPerPacket = 1;
        fmt.mChannelsPerFrame = 2;
        fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = 8;
        fmt.mBytesPerPacket = 8;
        AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));

        AURenderCallbackStruct cb = { renderCallback, this };
        AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));

        if (AudioUnitInitialize(unit) != noErr || AudioOutputUnitStart(unit) != noErr) {
            flog::error("iOS audio: cannot start RemoteIO unit");
            AudioComponentInstanceDispose(unit);
            unit = nullptr;
            return;
        }
        writeIndex = 0;
        readIndex = 0;
        sink.start();
        running = true;
        flog::info("iOS audio sink started at {} Hz", sampleRate);
    }

    void stop() {
        if (!running) { return; }
        sink.stop();
        AudioOutputUnitStop(unit);
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        unit = nullptr;
        running = false;
    }

    void menuHandler() {
        ImGui::Text("iOS audio output (%d Hz)", (int)sampleRate);
    }

private:
    static const int RING = 1 << 16; // frames, power of two

    class RingWriter : public dsp::Sink<dsp::stereo_t> {
    public:
        IosAudioSink* owner = nullptr;
        int run() override {
            int count = _in->read();
            if (count < 0) { return -1; }
            owner->push(_in->readBuf, count);
            _in->flush();
            return count;
        }
    };

    void push(const dsp::stereo_t* data, int count) {
        unsigned w = writeIndex.load(std::memory_order_relaxed);
        unsigned r = readIndex.load(std::memory_order_acquire);
        for (int i = 0; i < count; i++) {
            if (w - r >= RING) { break; } // full, drop
            ring[w & (RING - 1)] = data[i];
            w++;
        }
        writeIndex.store(w, std::memory_order_release);
    }

    static OSStatus renderCallback(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* io) {
        IosAudioSink* self = (IosAudioSink*)ref;
        float* out = (float*)io->mBuffers[0].mData;
        unsigned w = self->writeIndex.load(std::memory_order_acquire);
        unsigned r = self->readIndex.load(std::memory_order_relaxed);
        for (UInt32 i = 0; i < frames; i++) {
            if (r != w) {
                out[2 * i] = self->ring[r & (RING - 1)].l;
                out[2 * i + 1] = self->ring[r & (RING - 1)].r;
                r++;
            }
            else {
                out[2 * i] = 0;
                out[2 * i + 1] = 0;
            }
        }
        self->readIndex.store(r, std::memory_order_release);
        return noErr;
    }

    SinkManager::Stream* _stream;
    RingWriter sink;
    AudioUnit unit = nullptr;
    dsp::stereo_t ring[RING];
    std::atomic<unsigned> writeIndex{ 0 };
    std::atomic<unsigned> readIndex{ 0 };
    float sampleRate = 48000.0f;
    bool running = false;
};

class IosAudioSinkModule : public ModuleManager::Instance {
public:
    IosAudioSinkModule(std::string name) {
        this->name = name;
        provider.create = create_sink;
        provider.ctx = this;
        sigpath::sinkManager.registerSinkProvider("Audio", provider);
    }

    ~IosAudioSinkModule() {
        sigpath::sinkManager.unregisterSinkProvider("Audio");
    }

    void postInit() {}
    void enable() { enabled = true; }
    void disable() { enabled = false; }
    bool isEnabled() { return enabled; }

private:
    static SinkManager::Sink* create_sink(SinkManager::Stream* stream, std::string streamName, void* ctx) {
        return new IosAudioSink(stream, streamName);
    }

    std::string name;
    bool enabled = true;
    SinkManager::SinkProvider provider;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/ios_audio_sink_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new IosAudioSinkModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (IosAudioSinkModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
