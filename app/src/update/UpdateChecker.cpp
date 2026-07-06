// Auto-update subsystem implementation. See UpdateChecker.h for the contract.
//
// Design:
//  - One reusable background worker (juce::Thread subclass) runs either a
//    "check" job or a "download" job depending on which was requested. Only one
//    job runs at a time; the state machine guards against re-entry.
//  - All ChangeBroadcaster notifications are marshalled onto the message thread
//    via juce::MessageManager::callAsync, so listeners never touch worker data
//    directly. Download progress broadcasts are throttled to ~5 Hz.
//  - Shared strings are guarded by a CriticalSection; the state and progress are
//    lock-free atomics.
#include "UpdateChecker.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #include <process.h>   // _getpid
#endif

// ---- Compile-time config fallbacks (so this TU also builds standalone) ------
#ifndef RTG_VERSION
 #define RTG_VERSION "0.0.0"
#endif
#ifndef RTG_GITHUB_OWNER
 #define RTG_GITHUB_OWNER "owner"
#endif
#ifndef RTG_GITHUB_REPO
 #define RTG_GITHUB_REPO "repo"
#endif
#ifndef RTG_UPDATE_ASSET
 #define RTG_UPDATE_ASSET "RiddimTrapGenerator-win64.exe"
#endif

namespace rtg::app {

using State = UpdateChecker::State;

//==============================================================================
struct UpdateChecker::Impl : private juce::Thread
{
    explicit Impl (UpdateChecker& ownerToUse)
        : juce::Thread ("RTG_UpdateWorker"), owner (ownerToUse) {}

    ~Impl() override
    {
        // Signal the "alive" flag first so any pending callAsync/timer lambda
        // becomes a no-op, then stop the worker with a generous timeout.
        *aliveFlag = false;
        stopThread (15000);
    }

    //-- Job kinds the single worker can run ----------------------------------
    enum class Job { None, Check, Download };

    UpdateChecker& owner;

    std::atomic<State> currentState { State::Idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<Job>   pendingJob { Job::None };

    // Guarded string state.
    juce::CriticalSection lock;
    juce::String latestVer, notes, error, assetUrl;

    // Shared-lifetime flag; captured by value in async lambdas so they can bail
    // out safely if this object has been destroyed.
    std::shared_ptr<bool> aliveFlag { std::make_shared<bool> (true) };

    //-------------------------------------------------------------------------
    void startJob (Job job)
    {
        // Reject re-entry while a network job is active.
        auto s = currentState.load();
        if (s == State::Checking || s == State::Downloading)
            return;

        if (job == Job::Download)
        {
            // Download requires a resolved asset from a prior successful check.
            if (s != State::UpdateAvailable || getAssetUrl().isEmpty())
                return;
        }

        // Ensure any previous worker run has fully stopped before restarting.
        if (isThreadRunning())
            stopThread (15000);

        pendingJob = job;
        setState (job == Job::Download ? State::Downloading : State::Checking);
        if (job == Job::Download)
            setProgress (0.0f);

        startThread();
    }

    //-- Thread entry ----------------------------------------------------------
    void run() override
    {
        switch (pendingJob.load())
        {
            case Job::Check:    runCheck();    break;
            case Job::Download: runDownload(); break;
            case Job::None:     default:       break;
        }
    }

    //=========================================================================
    // CHECK job
    //=========================================================================
    void runCheck()
    {
        const juce::String api =
            "https://api.github.com/repos/" RTG_GITHUB_OWNER "/" RTG_GITHUB_REPO
            "/releases/latest";

        int statusCode = 0;
        juce::URL url (api);
        auto stream = url.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withExtraHeaders ("User-Agent: RiddimTrapGenerator-Updater\r\n"
                                   "Accept: application/vnd.github+json")
                .withConnectionTimeoutMs (10000)
                .withStatusCode (&statusCode)
                .withNumRedirectsToFollow (5));

        if (threadShouldExit())
            return;

        if (stream == nullptr)
        {
            fail ("Could not reach the update server. Check your connection.");
            return;
        }

        if (statusCode == 404)
        {
            fail ("No releases found");
            return;
        }

        if (statusCode != 0 && (statusCode < 200 || statusCode >= 300))
        {
            fail ("Update server returned status " + juce::String (statusCode));
            return;
        }

        const juce::String body = stream->readEntireStreamAsString();
        if (threadShouldExit())
            return;

        auto json = juce::JSON::parse (body);
        if (! json.isObject())
        {
            fail ("Could not read release information.");
            return;
        }

        auto* obj = json.getDynamicObject();
        if (obj == nullptr)
        {
            fail ("Could not read release information.");
            return;
        }

        const juce::var tagVar = obj->getProperty ("tag_name");
        if (tagVar.isVoid() || tagVar.toString().isEmpty())
        {
            fail ("No releases found");
            return;
        }

        juce::String tag = tagVar.toString().trim();
        if (tag.startsWithChar ('v') || tag.startsWithChar ('V'))
            tag = tag.substring (1);

        const juce::String relNotes = obj->getProperty ("body").toString();
        const juce::String downloadUrl = findAssetUrl (obj->getProperty ("assets"));

        {
            const juce::ScopedLock sl (lock);
            latestVer = tag;
            notes     = relNotes;
            assetUrl  = downloadUrl;
            error     = {};
        }

        if (isNewerVersion (tag, currentVersion()) && downloadUrl.isNotEmpty())
            setState (State::UpdateAvailable);
        else
            setState (State::UpToDate);
    }

    // Pick the update asset URL from the assets array. Prefer the exact
    // configured name; else fall back to the first ".exe" asset.
    static juce::String findAssetUrl (const juce::var& assets)
    {
        auto* arr = assets.getArray();
        if (arr == nullptr)
            return {};

        juce::String firstExe;
        for (const auto& a : *arr)
        {
            auto* ao = a.getDynamicObject();
            if (ao == nullptr)
                continue;

            const juce::String name = ao->getProperty ("name").toString();
            const juce::String dlUrl = ao->getProperty ("browser_download_url").toString();
            if (dlUrl.isEmpty())
                continue;

            if (name == RTG_UPDATE_ASSET)
                return dlUrl;   // exact match wins immediately

            if (firstExe.isEmpty() && name.endsWithIgnoreCase (".exe"))
                firstExe = dlUrl;
        }
        return firstExe;        // may be empty → treated as no-asset
    }

    //=========================================================================
    // DOWNLOAD job
    //=========================================================================
    void runDownload()
    {
        const juce::String dlUrl = getAssetUrl();
        if (dlUrl.isEmpty())
        {
            fail ("No downloadable update asset was found.");
            return;
        }

        auto dest = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("RTG_Update_v" + getLatestVersion() + ".exe");
        dest.deleteFile();

        int statusCode = 0;
        juce::URL url (dlUrl);
        auto stream = url.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withExtraHeaders ("User-Agent: RiddimTrapGenerator-Updater")
                .withConnectionTimeoutMs (10000)
                .withStatusCode (&statusCode)
                .withNumRedirectsToFollow (5));   // follow redirects to CDN

        if (threadShouldExit())
            return;

        if (stream == nullptr || (statusCode != 0 && (statusCode < 200 || statusCode >= 300)))
        {
            fail ("Could not start the download.");
            return;
        }

        std::unique_ptr<juce::FileOutputStream> out (dest.createOutputStream());
        if (out == nullptr || out->failedToOpen())
        {
            fail ("Could not write the update file to disk.");
            return;
        }

        const juce::int64 total = stream->getTotalLength();   // -1 if unknown
        juce::int64 received = 0;
        juce::HeapBlock<char> buffer (65536);
        auto lastBroadcast = juce::Time::getMillisecondCounter();

        for (;;)
        {
            if (threadShouldExit())
            {
                out.reset();
                dest.deleteFile();
                return;   // silent abort (destruction / cancel)
            }

            const int got = stream->read (buffer.getData(), 65536);
            if (got <= 0)
                break;

            if (! out->write (buffer.getData(), (size_t) got))
            {
                out.reset();
                dest.deleteFile();
                fail ("Writing the update file failed (disk full?).");
                return;
            }

            received += got;

            // Indeterminate length → report a steady 0.5; else true fraction.
            const float frac = (total > 0)
                ? juce::jlimit (0.0f, 1.0f, (float) ((double) received / (double) total))
                : 0.5f;

            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastBroadcast >= 200)   // throttle to ~5 Hz
            {
                lastBroadcast = now;
                setProgress (frac);
            }
        }

        out->flush();
        const bool writeOk = ! out->failedToOpen();
        out.reset();

        if (! writeOk)
        {
            dest.deleteFile();
            fail ("The update file could not be finalised.");
            return;
        }

        // PE sanity: > 1 MB and begins with the "MZ" DOS header.
        if (! looksLikeValidExe (dest))
        {
            dest.deleteFile();
            fail ("The downloaded update failed verification.");
            return;
        }

        {
            const juce::ScopedLock sl (lock);
            downloadedFile = dest;
            error = {};
        }
        setProgress (1.0f);
        setState (State::ReadyToInstall);
    }

    static bool looksLikeValidExe (const juce::File& f)
    {
        if (! f.existsAsFile() || f.getSize() <= (juce::int64) (1024 * 1024))
            return false;

        juce::FileInputStream in (f);
        if (in.failedToOpen())
            return false;

        char magic[2] = { 0, 0 };
        return in.read (magic, 2) == 2 && magic[0] == 'M' && magic[1] == 'Z';
    }

    //=========================================================================
    // INSTALL
    //=========================================================================
    void doRestartAndInstall()
    {
        if (currentState.load() != State::ReadyToInstall)
            return;

        juce::File exe;
        {
            const juce::ScopedLock sl (lock);
            exe = downloadedFile;
        }
        if (! exe.existsAsFile())
        {
            fail ("The downloaded update is missing.");
            return;
        }

       #if JUCE_WINDOWS
        const juce::File current =
            juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const juce::String currentPath = current.getFullPathName();
        const juce::String exeName     = current.getFileName();
        const juce::String downloaded  = exe.getFullPathName();
        const int ourPid               = _getpid();

        // Batch swap: wait until this process exits (poll its PID for up to 60s),
        // overwrite the running exe with the downloaded one, relaunch, self-delete.
        // Note the doubled-up quoting: every path is wrapped in "..." because the
        // install directory (e.g. "Program Files") can contain spaces.
        juce::String bat;
        bat << "@echo off\r\n"
            << "set RETRIES=0\r\n"
            << ":wait\r\n"
            << "timeout /t 1 /nobreak >nul\r\n"
            << "tasklist /FI \"PID eq " << ourPid << "\" 2>nul | findstr /i \""
            << exeName << "\" >nul && ( set /a RETRIES+=1 & if %RETRIES% LSS 60 goto wait )\r\n"
            << "copy /y \"" << downloaded << "\" \"" << currentPath << "\" >nul\r\n"
            << "if errorlevel 1 ( exit /b 1 )\r\n"
            << "start \"\" \"" << currentPath << "\"\r\n"
            << "del \"%~f0\"\r\n";

        auto batFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("rtg_update.bat");
        batFile.deleteFile();
        if (! batFile.replaceWithText (bat))
        {
            fail ("Could not write the updater script.");
            return;
        }

        // Launch the updater detached; it keeps running after we quit.
        if (! batFile.startAsProcess())
        {
            fail ("Could not launch the updater.");
            return;
        }

        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
       #else
        // Dev builds on Linux/macOS: no in-place swap; just show the file.
        exe.revealToUser();
        // Remain in ReadyToInstall.
       #endif
    }

    //=========================================================================
    // State / broadcast helpers (all marshalled to the message thread)
    //=========================================================================
    void setState (State s)
    {
        currentState = s;
        broadcastAsync();
    }

    void setProgress (float p)
    {
        progress = p;
        broadcastAsync();
    }

    void fail (const juce::String& message)
    {
        {
            const juce::ScopedLock sl (lock);
            error = message;
        }
        setState (State::Error);
    }

    void broadcastAsync()
    {
        auto flag = aliveFlag;
        auto* self = &owner;
        juce::MessageManager::callAsync ([flag, self]
        {
            if (*flag)
                self->sendChangeMessage();
        });
    }

    //-- Thread-safe accessors -------------------------------------------------
    juce::String getLatestVersion() const { const juce::ScopedLock sl (lock); return latestVer; }
    juce::String getReleaseNotes()  const { const juce::ScopedLock sl (lock); return notes; }
    juce::String getError()         const { const juce::ScopedLock sl (lock); return error; }
    juce::String getAssetUrl()      const { const juce::ScopedLock sl (lock); return assetUrl; }

    static juce::String currentVersion() { return RTG_VERSION; }

    juce::File downloadedFile;
};

//==============================================================================
// UpdateChecker public surface
//==============================================================================
UpdateChecker::UpdateChecker()
    : impl_ (std::make_unique<Impl> (*this))
{
    // Auto-check ~3 s after launch. Guard the deferred call with the shared
    // alive flag so it is a no-op if we are destroyed first.
    auto flag = impl_->aliveFlag;
    auto* self = this;
    juce::Timer::callAfterDelay (3000, [flag, self]
    {
        if (*flag)
            self->checkForUpdates();
    });
}

UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::checkForUpdates()   { impl_->startJob (Impl::Job::Check); }
void UpdateChecker::downloadUpdate()    { impl_->startJob (Impl::Job::Download); }
void UpdateChecker::restartAndInstall() { impl_->doRestartAndInstall(); }

UpdateChecker::State UpdateChecker::state() const   { return impl_->currentState.load(); }
float UpdateChecker::downloadProgress() const       { return impl_->progress.load(); }
juce::String UpdateChecker::currentVersion() const  { return Impl::currentVersion(); }
juce::String UpdateChecker::latestVersion() const   { return impl_->getLatestVersion(); }
juce::String UpdateChecker::releaseNotes() const    { return impl_->getReleaseNotes(); }
juce::String UpdateChecker::errorMessage() const    { return impl_->getError(); }

//==============================================================================
// Semver comparison: compare up to 4 numeric fields, ignoring any pre-release
// suffix after '-'. Missing fields count as 0. Strictly-greater → true.
bool UpdateChecker::isNewerVersion (const juce::String& latest, const juce::String& current)
{
    auto parse = [] (juce::String v, int out[4])
    {
        for (int i = 0; i < 4; ++i)
            out[i] = 0;

        v = v.trim();
        if (v.startsWithChar ('v') || v.startsWithChar ('V'))
            v = v.substring (1);

        // Drop pre-release / build suffix ("1.2.0-beta" → "1.2.0").
        const int dash = v.indexOfChar ('-');
        if (dash >= 0)
            v = v.substring (0, dash);

        juce::StringArray parts;
        parts.addTokens (v, ".", "");
        for (int i = 0; i < juce::jmin (4, parts.size()); ++i)
            out[i] = parts[i].getIntValue();   // trailing non-digits ignored
    };

    int l[4], c[4];
    parse (latest, l);
    parse (current, c);

    for (int i = 0; i < 4; ++i)
    {
        if (l[i] > c[i]) return true;
        if (l[i] < c[i]) return false;
    }
    return false;   // equal → not newer
}

} // namespace rtg::app
