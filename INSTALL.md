# Installing Riddim Trap Generator on Windows

RTG ships as a **single portable `.exe`** — no installer, no dependencies,
fully offline. Builds are produced by the GitHub **Release** workflow and
published on the repository's Releases page; the app then keeps itself up to
date with the built-in update checker.

## A. Publish a release (first time, or any time you want a new version)

1. Open the repository on GitHub → **Actions** tab.
2. In the left sidebar choose **Release**.
3. Click **Run workflow** (top right), pick the version bump
   (`patch` / `minor` / `major` — the first run automatically becomes
   **v1.0.0**), and press the green **Run workflow** button.
4. Wait for the run to go green (~15–25 min: it builds the app, renders two
   test tracks headlessly to verify the engine, then publishes).
5. The release appears under **Releases** with two assets:
   - `RiddimTrapGenerator-win64.exe` — the app (this is also what the
     auto-updater downloads)
   - `RiddimTrapGenerator-vX.Y.Z-win64.zip` — same exe, zipped

## B. Install on your computer

1. On the repo page, click **Releases** (right sidebar) → the latest release.
2. Download **`RiddimTrapGenerator-win64.exe`**.
3. Move it somewhere permanent you have write access to, e.g.
   `C:\Users\<you>\Apps\RiddimTrapGenerator\RiddimTrapGenerator-win64.exe`
   (a folder of its own is best — auto-update replaces the exe in place;
   avoid `C:\Program Files`, which blocks the self-updater).
4. Double-click to run.
5. **Windows SmartScreen** will warn the first time (the exe is not
   code-signed): click **More info → Run anyway**.
6. Optional: right-click the exe → *Pin to Start* / *Send to Desktop* for a
   shortcut.

First launch creates your data folder at
`%APPDATA%\RiddimTrapGenerator\` (evolving sound library + settings).
Deleting the exe never deletes your library.

### First track

Pick **RIDDIM** or **TRAP**, set BPM/length/knobs (or keep defaults), press
**GENERATE**, then ▶ to preview and **Export WAV…** to save the master.

## C. How auto-update works

- On every launch (and via **Settings → Check for updates**) the app queries
  this repository's **latest GitHub release** and compares its tag against
  the running version.
- If a newer version exists, an **update banner** appears with the new
  version and release notes. Click **Download** — the new exe downloads in
  the background with a progress readout.
- Click **Restart & install** — the app closes, swaps itself for the new
  version, and relaunches automatically. Done.
- Publishing an update = step A above: run the **Release** workflow again;
  it auto-tags the next version, and every installed copy will offer the
  update on its next launch.

## D. Troubleshooting

| Symptom | Fix |
|---|---|
| SmartScreen blocks launch | More info → Run anyway (unsigned build) |
| Update "Restart & install" doesn't swap | Ensure the exe's folder is user-writable (not Program Files); download manually from Releases as fallback |
| No sound | Settings → Audio device: pick your output device (WASAPI) |
| "No releases found" in update check | The Release workflow hasn't been run yet, or no network |
| Corporate proxy blocks api.github.com | Updates fail gracefully; the app itself is fully offline |
