#include "GenerationController.h"

#include <memory>
#include <mutex>
#include <string>

#include "rtg/decision/plan.h"
#include "rtg/synth/synth_engine.h"
#include "rtg/utils/rng.h"

namespace rtg::app {

//==============================================================================
struct GenerationController::Impl {
    // --- Audio device / playback chain ---------------------------------------
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::MixerAudioSource mixer;
    juce::AudioTransportSource mainTransport;     // last generation result
    juce::AudioTransportSource previewTransport;  // library audition preview

    juce::AudioBuffer<float> mainBuffer;
    juce::AudioBuffer<float> previewBuffer;
    std::unique_ptr<juce::MemoryAudioSource> mainSource;
    std::unique_ptr<juce::MemoryAudioSource> previewSource;

    // --- State ---------------------------------------------------------------
    std::optional<rtg::GenerationResult> result;
    rtg::SoundLibrary library;

    std::atomic<bool> cancelFlag { false };
    std::atomic<bool> running { false };
    std::atomic<float> progress { 0.0f };
    mutable std::mutex stageMutex;
    std::string stage;

    // Guards message-thread callbacks fired from worker threads.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    GenerationController& owner;

    //--------------------------------------------------------------------------
    struct GenThread : juce::Thread {
        Impl& impl;
        rtg::Params params;
        explicit GenThread(Impl& i) : juce::Thread("rtg-generation"), impl(i) {}

        void run() override {
            auto progressFn = [this](float p, const std::string& s) {
                impl.progress.store(p);
                std::lock_guard<std::mutex> lk(impl.stageMutex);
                impl.stage = s;
            };
            auto res = rtg::generateTrack(params, impl.library, impl.cancelFlag, progressFn);
            impl.running.store(false);

            std::weak_ptr<bool> alive = impl.alive;
            Impl* self = &impl;
            juce::MessageManager::callAsync([alive, self, r = std::move(res)]() mutable {
                if (alive.expired()) return;
                self->onGenerationFinished(std::move(r));
            });
        }
    };
    std::unique_ptr<GenThread> genThread;

    //--------------------------------------------------------------------------
    explicit Impl(GenerationController& o)
        : library(dataDir().getChildFile("library").getFullPathName().toStdString()),
          owner(o) {
        dataDir().getChildFile("library").createDirectory();
        library.load();

        deviceManager.initialise(0, 2, nullptr, true);
        mixer.addInputSource(&mainTransport, false);
        mixer.addInputSource(&previewTransport, false);
        sourcePlayer.setSource(&mixer);
        deviceManager.addAudioCallback(&sourcePlayer);
    }

    ~Impl() {
        *alive = false;
        if (genThread) {
            cancelFlag.store(true);
            genThread->stopThread(4000);
        }
        deviceManager.removeAudioCallback(&sourcePlayer);
        sourcePlayer.setSource(nullptr);
        mainTransport.setSource(nullptr);
        previewTransport.setSource(nullptr);
        mixer.removeAllInputs();
    }

    static juce::File dataDir() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("RiddimTrapGenerator");
    }

    //--------------------------------------------------------------------------
    static void fillBuffer(juce::AudioBuffer<float>& dst, const rtg::StereoBuffer& src) {
        const int n = (int) src.size();
        dst.setSize(2, juce::jmax(1, n), false, true, false);
        dst.clear();
        if (n > 0) {
            dst.copyFrom(0, 0, src.l.data(), n);
            dst.copyFrom(1, 0, src.r.data(), n);
        }
    }

    void onGenerationFinished(std::optional<rtg::GenerationResult> r) {
        genThread.reset();
        if (r.has_value()) {
            result = std::move(r);
            mainTransport.stop();
            mainTransport.setSource(nullptr);
            fillBuffer(mainBuffer, result->master);
            mainSource = std::make_unique<juce::MemoryAudioSource>(mainBuffer, false, false);
            mainTransport.setSource(mainSource.get(), 0, nullptr, result->sampleRate);
            mainTransport.setPosition(0.0);
        }
        owner.sendChangeMessage();
    }

    //--------------------------------------------------------------------------
    void startAuditionPreview(rtg::RatedSound sound) {
        std::weak_ptr<bool> alive = this->alive;
        Impl* self = this;
        juce::Thread::launch([alive, self, sound = std::move(sound)]() {
            auto buf = rtg::synth::renderPreview(sound.recipe, /*rootMidi*/ 41,
                                                 /*seconds*/ 1.5, /*sampleRate*/ 48000.0);
            juce::MessageManager::callAsync([alive, self, buf = std::move(buf)]() mutable {
                if (alive.expired()) return;
                self->applyPreview(std::move(buf));
            });
        });
    }

    void applyPreview(rtg::StereoBuffer buf) {
        previewTransport.stop();
        previewTransport.setSource(nullptr);
        fillBuffer(previewBuffer, buf);
        previewSource = std::make_unique<juce::MemoryAudioSource>(previewBuffer, false, false);
        previewTransport.setSource(previewSource.get(), 0, nullptr, 48000.0);
        previewTransport.setPosition(0.0);
        previewTransport.start();
    }
};

//==============================================================================
GenerationController::GenerationController()
    : impl_(std::make_unique<Impl>(*this)) {}

GenerationController::~GenerationController() = default;

//==============================================================================
void GenerationController::startGeneration(const rtg::Params& params) {
    if (impl_->running.load() || impl_->genThread) return;
    impl_->cancelFlag.store(false);
    impl_->progress.store(0.0f);
    {
        std::lock_guard<std::mutex> lk(impl_->stageMutex);
        impl_->stage = "Planning";
    }
    impl_->running.store(true);
    impl_->genThread = std::make_unique<Impl::GenThread>(*impl_);
    impl_->genThread->params = params;
    impl_->genThread->startThread();
}

void GenerationController::cancelGeneration() {
    impl_->cancelFlag.store(true);
}

bool GenerationController::isGenerating() const {
    return impl_->running.load();
}

float GenerationController::progress() const {
    return impl_->progress.load();
}

juce::String GenerationController::progressStage() const {
    std::lock_guard<std::mutex> lk(impl_->stageMutex);
    return juce::String(impl_->stage);
}

const std::optional<rtg::GenerationResult>& GenerationController::result() const {
    return impl_->result;
}

//==============================================================================
void GenerationController::togglePlayback() {
    if (impl_->mainSource == nullptr) return;
    if (impl_->mainTransport.isPlaying()) {
        impl_->mainTransport.stop();
    } else {
        if (impl_->mainTransport.getCurrentPosition() >= impl_->mainTransport.getLengthInSeconds() - 1.0e-3)
            impl_->mainTransport.setPosition(0.0);
        impl_->mainTransport.start();
    }
    sendChangeMessage();
}

void GenerationController::stopPlayback() {
    impl_->mainTransport.stop();
    impl_->mainTransport.setPosition(0.0);
    sendChangeMessage();
}

bool GenerationController::isPlaying() const {
    return impl_->mainTransport.isPlaying();
}

double GenerationController::playheadSeconds() const {
    return impl_->mainTransport.getCurrentPosition();
}

void GenerationController::seekSeconds(double t) {
    impl_->mainTransport.setPosition(juce::jmax(0.0, t));
}

//==============================================================================
bool GenerationController::exportWav(const juce::File& destination) const {
    if (!impl_->result.has_value()) return false;
    const auto& master = impl_->result->master;
    const int n = (int) master.size();
    if (n <= 0) return false;

    juce::AudioBuffer<float> buffer;
    Impl::fillBuffer(buffer, master);

    destination.deleteFile();
    auto stream = destination.createOutputStream();
    if (stream == nullptr) return false;

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), impl_->result->sampleRate, 2, 24, {}, 0));
    if (writer == nullptr) return false;
    stream.release(); // writer now owns the stream
    const bool ok = writer->writeFromAudioSampleBuffer(buffer, 0, n);
    writer.reset();   // flush
    return ok;
}

//==============================================================================
rtg::SoundLibrary& GenerationController::library() {
    return impl_->library;
}

juce::AudioDeviceManager& GenerationController::deviceManager() {
    return impl_->deviceManager;
}

juce::File GenerationController::dataDirectory() const {
    return Impl::dataDir();
}

void GenerationController::auditionSound(const rtg::RatedSound& sound) {
    impl_->startAuditionPreview(sound);
}

} // namespace rtg::app
