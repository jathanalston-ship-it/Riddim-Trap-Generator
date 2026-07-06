# A/B Preference Trainer — vote schema & aggregation design

The in-app **Preference Trainer** (Train page) plays the user pairs of short
bass-drop loops, records which one they preferred, and trains a tiny local
logistic model (`rtg::PreferenceModel`) on those votes. The trained model is
then blended into fresh-candidate selection in the generation pipeline so
generations drift toward the user's taste.

Everything here is **anonymous and audio-free**: a vote stores only the numeric
analysis feature vectors of the two candidates plus which one won. This is by
design so a future version can aggregate votes across the whole userbase.

---

## 1. Feature vector (`featv = 1`)

Votes and weights are versioned by a **feature-order version** `featv`. Any
change to the ordering, count, or normalization below is a new `featv` and old
data is ignored on load (mismatched `featv` ⇒ `PreferenceModel::load` returns
false, `loadVotes` still parses but the model must not mix feature versions).

`PreferenceModel::extract(const rtg::Features&)` produces an 8-dimensional
vector, in this exact order:

| idx | source            | transform                    |
|-----|-------------------|------------------------------|
| 0   | `brightness`      | as-is (0..1)                 |
| 1   | `aggression`      | as-is (0..1)                 |
| 2   | `darkness`        | as-is (0..1)                 |
| 3   | `movement`        | as-is (0..1)                 |
| 4   | `subRatio`        | as-is (0..1)                 |
| 5   | `transient`       | as-is (0..1)                 |
| 6   | `crestDb`         | `crestDb / 20`               |
| 7   | `centroidHz`      | `centroidHz / 8000`, clamped 0..1 |

The model has **9 weights**: 8 feature weights + 1 bias. The bias does not enter
the pairwise loss (it cancels in the A−B difference), so it stays near zero; it
exists only so `score()` is a well-formed absolute probability.

### Scoring & training

- `score(features) = sigmoid(w·x + bias)` ∈ 0..1.
- Training is **pairwise logistic regression** by SGD:
  `P(A beats B) = sigmoid(w·(xA − xB))`, label `y = 1` if A won (`choice == 0`),
  `y = 0` if B won (`choice == 1`). Update `w -= lr·((p−y)·(xA−xB) + L2·w)`.
- App defaults: `epochs = 300`, `lr = 0.2`, `L2 = 1e-4`.
- `trainedOn` is the number of votes the current weights were trained on.

---

## 2. On-disk layout

All trainer data lives under the per-user data directory
`<dataDir>` (the app uses `<userAppData>/RiddimTrapGenerator`):

```
<dataDir>/
  library/                      # existing .rtgsound assets (SoundLibrary)
  training/
    install_id                  # anonymous per-install uuid (hex-32), 1 line
    votes.jsonl                 # append-only, one vote per line
    preference_weights.json     # trained model weights
```

### `install_id`
A random 128-bit id rendered as 32 lowercase hex chars, generated on first use
and persisted. It is **not** tied to any account, email, or hardware — it exists
only to dedupe votes when many files are merged. `PreferenceModel::installId()`
creates it lazily.

### `votes.jsonl` (append-only JSONL, one object per line)

```json
{"v":1,"featv":1,"install":"<hex-32>","ts":1720051200,"genre":"riddim","role":"Growl","featA":[...8 floats...],"featB":[...8 floats...],"choice":0}
```

| field     | meaning                                               |
|-----------|-------------------------------------------------------|
| `v`       | vote schema version (currently 1)                     |
| `featv`   | feature-order version the vectors were extracted with |
| `install` | anonymous install uuid                                |
| `ts`      | unix seconds when the vote was cast                   |
| `genre`   | `"riddim"` or `"trap"`                                |
| `role`    | representative recipe role, e.g. `"Growl"`, `"Bass808"` |
| `featA`   | 8-float feature vector of candidate A                 |
| `featB`   | 8-float feature vector of candidate B                 |
| `choice`  | `0` = A preferred, `1` = B preferred                  |

`loadVotes(path)` is **tolerant**: blank lines and unparseable lines are
skipped, missing scalar fields fall back to defaults, and a line is only
accepted if both `featA` and `featB` parse to ≥ 8 floats.

### `preference_weights.json` (hand-rolled minimal JSON)

```json
{"v":1,"featv":1,"trainedOn":137,"w":[w0,w1,w2,w3,w4,w5,w6,w7,bias]}
```

`load` rejects the file if `featv` mismatches or `w` has fewer than 9 entries,
so a schema change is a clean no-op rather than a crash.

`PreferenceModel::available(dataDir)` is true when the weights file loads **and**
`trainedOn >= 20`.

---

## 3. Selection blending in the pipeline (how it activates)

`engine/generation/src/pipeline.cpp`, stage [3] "Choose sounds", fresh-candidate
synthesis (`synthesizeBest`). The pipeline only has a `SoundLibrary&`, so the
weights path is derived by convention:

```
prefsPath = library.directory() + "/../training/preference_weights.json"
```

i.e. the sibling `training/` directory next to the library directory (the app
stores the library at `<dataDir>/library` and trainer data at
`<dataDir>/training`, so this resolves to `<dataDir>/training/preference_weights.json`).

- If `PreferenceModel::load(prefsPath)` **fails** (missing / invalid / `featv`
  mismatch) → behavior is **byte-for-byte identical to before**: candidates are
  ranked purely by `synth::rate(role, features)`.
- If it **succeeds** → each fresh candidate's *selection metric* becomes:

  ```
  sel = 0.65 * synth::rate(role, features) + 0.35 * prefModel.score(features)
  ```

  The winner is chosen by `sel`, but the `RatedSound::score` stored for library
  admission remains the raw `rate()` value, so library quality gating is
  unchanged. Selection stays fully deterministic given the weights.

---

## 4. Future community aggregation (design only — not built here)

The vote format is deliberately anonymous, audio-free, and mergeable so votes
can be pooled across users later. The intended pipeline:

1. **Opt-in uploader.** A future version adds an explicit, opt-in setting that
   ships the user's `votes.jsonl` to a community endpoint. Uploads contain only
   the anonymous rows above — no audio, no account, no IP-linked identity beyond
   the random `install_id`. Nothing uploads without consent.
2. **Server-side merge.** The server concatenates every submitted `votes.jsonl`
   into a directory and runs the **same** `PreferenceModel::mergeVotes(dir)`
   used locally. Dedupe key is `(install, ts, featA[0], featB[0])`, so
   re-uploads and overlapping files collapse cleanly. Only rows whose `featv`
   matches the current feature version are trained on.
3. **Global training.** The server trains a `PreferenceModel` on the merged
   corpus (same pairwise-logistic training) producing global weights.
4. **Ship-back via the existing update channel.** The global
   `preference_weights.json` is published as a **release asset** on the existing
   GitHub-release update channel (same mechanism as
   `app/src/update/UpdateChecker.*`). The client downloads it as
   `community_weights.json` alongside the app update — **no new network code or
   endpoint is required in the client**; it reuses the update infrastructure.
5. **Local blend of personal + community.** When both a locally trained
   `preference_weights.json` and a downloaded community model exist, the
   effective weights are:

   ```
   final = 0.7 * personal + 0.3 * community
   ```

   (element-wise on the weight vectors, both at the same `featv`). Personal
   taste dominates; the community model fills in where the user has few votes.
   When only one exists, that one is used as-is.

None of §4 is implemented in this version — only the local trainer, the vote
schema, `mergeVotes`, and the selection blend are. The schema and merge function
are the stable contract the community layer will build on.
