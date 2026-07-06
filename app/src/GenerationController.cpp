#include "GenerationController.h"

#include <atomic>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>

#include "rtg/decision/plan.h"
#include "rtg/library/preference_model.h"
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

    // --- A/B Preference Trainer ---------------------------------------------
    juce::AudioBuffer<float> trainBufferA, trainBufferB;
    std::unique_ptr<juce::MemoryAudioSource> trainSourceA, trainSourceB;
    rtg::Features trainFeatA{}, trainFeatB{};
    std::string trainRoleA = "Growl";
    rtg::Genre trainGenre = rtg::Genre::Riddim;
    std::atomic<bool> trainReady { false };
    std::atomic<bool> trainRendering { false };
    std::atomic<int> lastTrainPlayed { 0 };      // 0 = A, 1 = B
    std::atomic<int> voteCount { 0 };

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
        dataDir().getChildFile("training").createDirectory();
        library.load();
        voteCount.store((int) rtg::PreferenceModel::loadVotes(votesPathStr()).size());

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

    //--- A/B Preference Trainer ------------------------------------------------
    std::string dataDirStr() const { return dataDir().getFullPathName().toStdString(); }
    std::string votesPathStr() const { return rtg::PreferenceModel::votesPath(dataDirStr()); }
    std::string weightsPathStr() const { return rtg::PreferenceModel::weightsPath(dataDirStr()); }

    void startTrainingPair(rtg::Genre g) {
        if (trainRendering.exchange(true)) return;   // one render at a time
        trainReady.store(false);
        trainGenre = g;
        owner.sendChangeMessage();                    // reflect spinner immediately

        // Two candidates. Riddim: two Growl recipes (different seeds).
        // Trap: a Bass808 and a Screech candidate.
        rtg::Role roleA = (g == rtg::Genre::Riddim) ? rtg::Role::Growl : rtg::Role::Bass808;
        rtg::Role roleB = (g == rtg::Genre::Riddim) ? rtg::Role::Growl : rtg::Role::Screech;
        const uint64_t base = (uint64_t) juce::Random::getSystemRandom().nextInt64() | 1ull;

        std::weak_ptr<bool> alive = this->alive;
        Impl* self = this;
        juce::Thread::launch([alive, self, g, roleA, roleB, base]() {
            rtg::Rng rngA(base ^ 0xA11CE5ull);
            rtg::Rng rngB(base ^ 0xB0B0B0ull);
            rtg::Recipe recA = rtg::synth::makeRecipe(roleA, 0.78f, 0.55f, 0.45f, rngA);
            rtg::Recipe recB = rtg::synth::makeRecipe(roleB, 0.78f, 0.55f, 0.45f, rngB);
            rtg::StereoBuffer bufA = rtg::synth::renderPreview(recA, /*midi*/ 41, 2.5, 48000.0);
            rtg::StereoBuffer bufB = rtg::synth::renderPreview(recB, /*midi*/ 41, 2.5, 48000.0);
            rtg::Features fA = rtg::synth::analyze(bufA, 48000.0);
            rtg::Features fB = rtg::synth::analyze(bufB, 48000.0);
            std::string roleName = rtg::roleName(roleA);
            juce::MessageManager::callAsync(
                [alive, self, bufA = std::move(bufA), bufB = std::move(bufB),
                 fA, fB, g, roleName]() mutable {
                    if (alive.expired()) return;
                    self->applyTrainingPair(std::move(bufA), std::move(bufB), fA, fB, g, roleName);
                });
        });
    }

    void applyTrainingPair(rtg::StereoBuffer a, rtg::StereoBuffer b,
                           rtg::Features fA, rtg::Features fB,
                           rtg::Genre g, std::string roleName) {
        fillBuffer(trainBufferA, a);
        fillBuffer(trainBufferB, b);
        trainSourceA = std::make_unique<juce::MemoryAudioSource>(trainBufferA, false, false);
        trainSourceB = std::make_unique<juce::MemoryAudioSource>(trainBufferB, false, false);
        trainFeatA = fA; trainFeatB = fB;
        trainGenre = g; trainRoleA = std::move(roleName);
        trainRendering.store(false);
        trainReady.store(true);
        owner.sendChangeMessage();
    }

    void playTrain(int which) {
        auto* src = (which == 1) ? trainSourceB.get() : trainSourceA.get();
        if (src == nullptr) return;
        lastTrainPlayed.store(which);
        previewTransport.stop();
        previewTransport.setSource(nullptr);
        previewTransport.setSource(src, 0, nullptr, 48000.0);
        previewTransport.setPosition(0.0);
        previewTransport.start();
    }

    void voteTrain(int choice) {
        if (!trainReady.load()) return;
        rtg::Vote v;
        v.v = rtg::PreferenceModel::kSchemaV;
        v.featv = rtg::PreferenceModel::kFeatV;
        v.install = rtg::PreferenceModel::installId(dataDirStr());
        v.ts = (long long) std::time(nullptr);
        v.genre = (trainGenre == rtg::Genre::Riddim) ? "riddim" : "trap";
        v.role = trainRoleA;
        v.featA = rtg::PreferenceModel::extract(trainFeatA);
        v.featB = rtg::PreferenceModel::extract(trainFeatB);
        v.choice = (choice == 1) ? 1 : 0;
        if (rtg::PreferenceModel::appendVote(votesPathStr(), v))
            voteCount.fetch_add(1);
        // Continuous flow: immediately render the next pair.
        startTrainingPair(trainGenre);
    }

    int trainNow() {
        auto votes = rtg::PreferenceModel::loadVotes(votesPathStr());
        rtg::PreferenceModel model;
        model.train(votes, /*epochs*/ 300, /*lr*/ 0.2f, /*l2*/ 1e-4f);
        model.save(weightsPathStr());
        return model.trainedOn();
    }

    int modelTrainedOn() const {
        rtg::PreferenceModel m;
        if (!m.load(weightsPathStr())) return 0;
        return m.trainedOn();
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

//==============================================================================
void GenerationController::startTrainingPair(rtg::Genre genre) {
    impl_->startTrainingPair(genre);
}

bool GenerationController::trainPairReady() const {
    return impl_->trainReady.load();
}

bool GenerationController::isTrainRendering() const {
    return impl_->trainRendering.load();
}

void GenerationController::playTrainA() { impl_->playTrain(0); }
void GenerationController::playTrainB() { impl_->playTrain(1); }

void GenerationController::replayLastTrain() {
    impl_->playTrain(impl_->lastTrainPlayed.load());
}

void GenerationController::voteTrain(int choice) {
    impl_->voteTrain(choice);
}

int GenerationController::trainNow() {
    return impl_->trainNow();
}

int GenerationController::voteCount() const {
    return impl_->voteCount.load();
}

int GenerationController::modelTrainedOn() const {
    return impl_->modelTrainedOn();
}

} // namespace rtg::app
