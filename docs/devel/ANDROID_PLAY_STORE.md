# Android Play Store — Publishing & Automation

How the CI pipeline ships `helix-screen` to Google Play, and the one-time manual setup required before the automated upload can work.

## Status snapshot — 2026-04-23

**Done:**
- In-repo automation (CI builds signed AAB, generates whatsnew from CHANGELOG, publishes to internal track when service-account secret is set).
- Upload keystore generated and backed up at `~/.android-keystore/helixscreen-upload.jks`; four keystore-related GitHub secrets set (`ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD`).
- Store assets committed: 8 phone screenshots, 1024×500 feature graphic, 512×512 store icon, title / short / full descriptions. Fastlane metadata at `android/fastlane/metadata/android/en-US/`.
- Privacy policy published at `https://helixscreen.org/legal/privacy/` (platform-neutral; applies to Android as-is).
- Google Play Developer account org verification **cleared** (356C LLC).

**Warning — v0.99.43 AAB is debug-signed.** Secrets were added at 16:39 UTC on 2026-04-23; v0.99.43 built at 23:15 UTC on 2026-04-22, before the secrets were present, so its keystore step fell back to the Android Debug keystore. Play Store will reject it. **First manual upload must use v0.99.44 or later** — those are the first releases signed with the real upload keystore.

**Blocked on first manual upload (user):**
- Create the HelixScreen app record in Play Console.
- Upload listing text + store icon + feature graphic + 8 screenshots via Play Console UI.
- First manual AAB upload to the internal track (Google requires this before the Publishing API accepts uploads). Grab the AAB from whichever GitHub release is most recent at that point — CI already signs it with the upload keystore.
- Enroll in Play App Signing (prompted during the first manual upload).
- Create the Google Cloud service account, enable the Google Play Android Developer API, grant Release Manager scoped to the HelixScreen app, download the JSON key, paste it as the `PLAY_SERVICE_ACCOUNT_JSON` GitHub secret.

**Regular releases are unblocked.** On every release tag:
- `build-android` produces a properly-signed AAB (attached to the GitHub release for download).
- `publish-android` runs but skips cleanly when `PLAY_SERVICE_ACCOUNT_JSON` is unset — it logs a notice and every subsequent step is gated on the secret being present, so the job exits green.
- Nightly CI does not build Android at all, so nothing to worry about there.
- Once the service-account secret is added, the next release tag automatically starts pushing to the Play Store internal track. No workflow change needed.

## Overview

On every release tag (`v*`), `.github/workflows/release.yml` runs:

1. **`build-android`** — builds APKs (`assembleRelease`) and an AAB (`bundleRelease`), signed with the upload keystore held in GitHub secrets. Verifies the signature on the built bytes before anything is uploaded (see "What happens when the keystore is missing"). Generates a Play Store "What's new" text from `CHANGELOG.md` and uploads three artifacts: `release-android` (APKs), `release-android-aab` (AAB), `release-android-whatsnew`.
2. **`publish-android`** — uploads the AAB to the Play Store's **internal** track with status **draft**, using the `r0adkll/upload-google-play@v1` action. Inert when `PLAY_SERVICE_ACCOUNT_JSON` is unset.
3. **`release`** — attaches all artifacts to the GitHub release. It `needs` **both** `build-platforms` and `build-android`, and asserts the three APKs and the AAB are present by name before creating the release. Without the `build-android` dependency the release could be created while Android was still building (90 min budget against the matrix's 82) and, because the artifact globs run with `fail_on_unmatched_files: false`, it would publish with no Android downloads and still report success.

Once the upload lands on the internal track, you promote it to open testing or production from the Play Console UI. The automation stops at `draft` on purpose so every release gets a final human review before it goes live.

## Source of truth

| Path | Holds |
|------|-------|
| `android/fastlane/metadata/android/en-US/title.txt` | App title (≤30 chars) |
| `android/fastlane/metadata/android/en-US/short_description.txt` | Short description (≤80 chars) |
| `android/fastlane/metadata/android/en-US/full_description.txt` | Full description (≤4000 chars) |
| `android/fastlane/metadata/android/en-US/images/icon.png` | Store icon (512×512, symlinked to `docs/store/android/icon-512.png`) |
| `android/fastlane/metadata/android/en-US/images/featureGraphic.png` | Feature graphic (1024×500, symlinked to `docs/store/android/`) |
| `android/fastlane/metadata/android/en-US/images/phoneScreenshots/*.png` | Phone screenshots (symlinked to `docs/store/android/`) |
| `android/fastlane/metadata/android/en-US/changelogs/<versionCode>.txt` | "What's new" per release. **Generated, not maintained** — `scripts/generate-whatsnew.sh` writes it from `CHANGELOG.md` at release time and `release.yml` reads it straight back. Nothing is committed here; the source of truth is the `CHANGELOG.md` section for the version. (One stale file, `9943.txt`, was committed under the retired `major*10000` packing and has been removed — it would have read as the naming convention.) |

The listing text (title / descriptions / screenshots) is **not** synced by the workflow — `r0adkll/upload-google-play` only handles the AAB + whatsnew. Updates to those files are version-controlled here so the Play Console listing can be kept in sync by hand, with this tree as the canonical record. If we ever need full metadata sync, switch to `fastlane supply` (see "Future work").

## One-time manual prerequisites

Before the first run of `publish-android` can succeed, the following must be done by hand. Tick each item as it completes.

- [x] **Google Play Developer account** — registered 2026-04-23 as `356C LLC`; org verification cleared same day.
- [ ] **Create the app in Play Console** — name "HelixScreen", default language English (US), free, app type = App.
- [ ] **Complete Play Console declarations** — target audience, data safety, content rating (expected: Everyone), ads (none), government/COVID/financial questionnaires.
- [ ] **Upload listing assets manually** — title, short/full description, store icon, feature graphic, and 8 screenshots from `android/fastlane/metadata/android/en-US/`. Use the text in the `.txt` files verbatim so Play Console matches the repo.
- [x] **Privacy policy URL** — `https://helixscreen.org/legal/privacy/` is live. Paste that URL into Play Console → App content → Privacy policy. The Play Console "Data Safety" questionnaire is separate from the privacy policy URL — fill it out based on what Section 4 of the policy describes (opt-in telemetry; no PII; encrypted in transit; users can request deletion of local queue).
- [ ] **First manual AAB upload** — Google requires one manual AAB upload before the Publishing API will accept uploads. **Use v0.99.44 or later** — v0.99.43 and earlier are debug-signed and will be rejected. Download `helixscreen-android-v<VERSION>.aab` from the GitHub release and upload it to the internal track via the Play Console UI. This also triggers the **Play App Signing** enrollment prompt.

  **Decision: accept the Google-generated app signing key.** Google holds the app signing key, our upload keystore stays an upload key only, and a lost or compromised upload key can be reset by Google instead of orphaning the app. The consequence is worth stating plainly because nothing else in this doc did:

  - **`org.helixscreen.app` then has two signatures.** Play-served installs carry Google's app signing key; the APKs we attach to GitHub releases carry our upload key. Android identifies an app by package name *plus* signing certificate, so the two channels are mutually exclusive on a device: a user who installed from GitHub cannot take a Play update, and vice versa, without uninstalling first — which wipes local config (printer list, wizard state, themes).
  - **Both keys must be registered against the package name** for Android developer verification. Multiple signing keys per package are supported, so this is a registration detail, not a conflict — but registering only one of them leaves the other channel's installs unverifiable.
  - **Escape hatch, if the split becomes a real support burden:** the Play Developer API's `generatedapks.download` returns the Play-signed universal APK for a given versionCode. Publishing *that* to GitHub releases instead of our own `assembleRelease` output would converge both channels on Google's key and make installs interchangeable. Not worth the extra API plumbing until someone actually hits the problem.
- [ ] **Generate a service account for the API** — in Google Cloud Console (linked to the Play Developer account):
  1. Create a new service account named e.g. `helixscreen-ci`.
  2. Enable the "Google Play Android Developer API" for the project.
  3. Create a JSON key for the service account and download it.
- [ ] **Grant the service account access to the app** — Play Console → Users and permissions → Invite new users → add the service account email → grant **"Release manager"** role scoped to the HelixScreen app (production + testing tracks). Do **not** grant account-level admin.
- [x] **Generate the upload keystore** — done 2026-04-23. Backed up at `~/.android-keystore/helixscreen-upload.jks`. Passwords stored in password manager.
- [x] **Add the four keystore GitHub secrets** — `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` all set 2026-04-23 (verified via `gh secret list`).
- [ ] **Add the `PLAY_SERVICE_ACCOUNT_JSON` secret** — paste the full JSON contents (plain text, not base64) once the service account is created. Blocked on the Play Console app existing.

After all prerequisites are ticked off, the next release tag will trigger an automated upload to the internal track. Until then, **regular releases continue normally** — the `build-android` job produces properly-signed AABs (thanks to the keystore secrets being in place), and the `publish-android` job detects the missing service-account secret and skips every step via its guarded `if:` conditions. No build fails; the AAB is just not pushed to Google until we're ready.

### What happens when the keystore is missing

This used to say the missing-keystore case "fails noisily in `publish-android` instead of silently shipping a broken release." That was never true. `publish-android` runs *after* `release`, so the GitHub artifacts are already published by the time it could object; it is also inert without `PLAY_SERVICE_ACCOUNT_JSON`, so it objected to nothing. What actually happened is that v0.99.43 shipped a debug-signed AAB and nobody found out until someone tried to upload it.

A release build now refuses to produce an artifact it cannot sign properly, at three points:

1. **`release.yml` → "Materialize upload keystore"** exits 1 when `ANDROID_KEYSTORE_BASE64` is unset. A release tag has no business producing unsigned artifacts, and failing here names the missing secret.
2. **Gradle** fails any release-signing task (`assembleRelease`, `bundleRelease`, `packageRelease`, …) when no keystore is configured, listing the four env vars it needs. The check hangs off `gradle.taskGraph.whenReady` rather than firing at configuration time — configuration is shared with the debug variant, and `.github/workflows/build.yml` runs `assembleDebug` on every PR, which must keep working. `assembleDebug` and `lintRelease` are deliberately unaffected.
3. **`release.yml` → "Verify release artifacts are signed with the upload key"** runs after `bundleRelease` and before anything is renamed or uploaded. It reads the signature actually on the bytes — `apksigner verify --print-certs` for the APKs, `keytool -printcert -jarfile` for the AAB, since apksigner cannot read a bundle — and fails on the debug keystore's `CN=Android Debug`.

   **The two tools are not interchangeable, in either direction.** `minSdk` is 28, so apksigner signs the APKs with v2/v3 only and skips the v1 JAR signature entirely; `keytool -printcert -jarfile` on one of our APKs answers `Not a signed jar file`. The AAB goes the other way — AGP jar-signs bundles, so keytool reads it fine and apksigner cannot. Worse, keytool prints that refusal and **exits 0**, so a check that merely looks for the absence of `CN=Android Debug` would pass on an artifact it never actually read. The step therefore requires *positive* evidence: a SHA-256 fingerprint with hex after it must be present, or the artifact fails. Absence of the debug DN is not proof of a good signature. It also prints each artifact's SHA-256 signer fingerprint, which is what Play Console and Android developer verification want for package-name registration; read it off any release run rather than recomputing it from the keystore.

**Local escape hatch:** `./gradlew assembleRelease -PallowDebugSigning` builds a debug-signed release variant for a local smoke test. It exists so a developer without the keystore can still build; anything produced with it must never leave the machine.

## Android developer verification

Google is phasing in a requirement that the developer behind a package name be a verified identity. Where we stand:

- **`org.helixscreen.app` has never been published**, so this is a **new package-name registration** in Play Console — enter the package name plus the signing certificate's public key and you are done. There is no APK ownership challenge; that step only applies to *existing* package names, where Google has to confirm the person registering actually controls the app already.
- **Register both signing keys.** With Play App Signing accepted (see above), Play-served installs carry Google's app signing key and GitHub-release APKs carry our upload key. Multiple keys per package name are supported — register both, or one channel's installs are unverifiable. The two fingerprints come from different places, and only one of them is ours to print:

  | Key | SHA-256 fingerprint from |
  |-----|--------------------------|
  | Our upload key (GitHub-release APKs) | The "Verify release artifacts are signed with the upload key" step on any release run |
  | Google's app signing key (Play-served installs) | Play Console → Test and release → App integrity → App signing. Does not exist until Play App Signing enrollment completes, so it cannot be registered before the first manual upload |
- **Enforcement starts 2026-09-30**, and only for **Brazil, Indonesia, Singapore, and Thailand**, and only on *participating* app stores. Our sideload channel (direct APK download from a GitHub release) is not affected by that wave. Global enforcement is **2027**.
- **Register early anyway.** It costs nothing now, and it parks the package name — `org.helixscreen.app` is not reserved by anything today.

## Promoting a release

The automation stops at **internal / draft**. Promote manually:

1. Play Console → Release → Testing → **Internal testing** — click the draft release, review, and roll it out to testers (or discard and let the next tag upload a fresh one).
2. Once smoke-tested: **Promote release** → **Open testing** → submit for review (~1–3 days for review on a new app; faster on updates).
3. Once open testing is stable: **Promote release** → **Production**.

The internal track only needs an email list (add yourself + any trusted testers) and does not require review. Use it as the first sanity check after every release.

## Changing automation behavior

To change the target track or status, edit the `publish-android` job in `.github/workflows/release.yml`:

| Setting | Value now | Alternatives |
|---------|-----------|--------------|
| `track` | `internal` | `alpha`, `beta`, `production` |
| `status` | `draft` | `inProgress`, `halted`, `completed` |
| `changesNotSentForReview` | `true` | `false` — sends the release for review automatically; can't be used on `production` without supplying release notes |

## Local dry runs

Regenerate the whatsnew file locally to inspect what CI will upload:

```bash
scripts/generate-whatsnew.sh /tmp/whatsnew-preview.txt
cat /tmp/whatsnew-preview.txt
wc -m /tmp/whatsnew-preview.txt   # must be ≤500 chars for Play Store
```

The script reads the section for `VERSION.txt`'s current version from `CHANGELOG.md`, strips markdown, and truncates on a sentence boundary with an ellipsis if it would exceed 500 characters.

To test the full upload path without touching production, point the action at a sandbox app: create a second Play Console app (e.g. `org.helixscreen.app.dev`), invite the same service account, and override `packageName` in a temporary workflow branch. Not currently set up — only add this if we need to debug the uploader itself.

## Gotchas

- **First upload must be manual.** The Publishing API refuses the very first upload for any new app. This is a Google constraint, not a workflow limitation.
- **versionCode must strictly increase**, and there is exactly one definition of it: **`scripts/android-version-code.sh`**. It packs `VERSION.txt` as `major*1000000 + minor*1000 + patch`, so `0.99.113` → `99113` and `1.0.0` → `1000000`. Re-releasing the same `VERSION.txt` to the Play Store will be rejected. The lanes are 1000 wide because the field below must never overflow into the one above: under the earlier `major*10000 + minor*100 + patch` packing, `0.99.113` produced `10013` and `1.0.0` produced `10000`, so 1.0 would have been rejected as a downgrade both by the Play Store and by every sideloaded install.

  Three consumers call that script — `android/app/build.gradle` (the versionCode itself), `scripts/generate-whatsnew.sh` (names `changelogs/<code>.txt`), `.github/workflows/release.yml` (reads that same file back). They used to each write the arithmetic out, and they diverged the moment the lanes were widened: release.yml kept the old packing, so it looked for `changelogs/10014.txt` while the script had written `changelogs/99114.txt`. The `if [ -f "$SRC" ]` fell through to a `::warning::`, `if-no-files-found: ignore` skipped the upload, and the whatsnew artifact silently did not exist — invisible only because `publish-android` is inert without the service-account secret. `tests/shell/test_android_version_code.bats` gates against a second copy reappearing. Query the current value with `cd android && ./gradlew -q printVersionCode` or `scripts/android-version-code.sh`.
- **Pre-release tags (`v1.0.0-beta`) still upload.** The `publish-android` job does not check for pre-release tags. If we later want to gate pre-releases out of the Play Store, add an `if: !contains(github.ref_name, '-')` at the job level.
- **Store icon vs launcher icon.** `android/app/src/main/res/mipmap-*/ic_launcher.png` (max 192×192) is what users see on their home screen. Play Console separately requires a **512×512** store icon for the listing, generated from `assets/images/helix-icon.png` onto the launcher's #2D2D2B background and committed at `docs/store/android/icon-512.png`. Upload it manually the first time; after that it persists on the listing until you change it.
- **Screenshot dimensions.** Current screenshots in `docs/store/android/` are 960×540. Play Store accepts anything ≥320 on each side, so these pass — but re-shooting at 1920×1080 would look crisper on large-screen previews. Not blocking.
- **Secret hygiene.** `PLAY_SERVICE_ACCOUNT_JSON` is the key to pushing arbitrary code to production. Rotate if exposed. The service account should hold only "Release manager" scoped to this app — not account-level admin.

## Future work

- **Full metadata sync via `fastlane supply`** — would let the workflow also update the store listing text/screenshots on every release. Adds a fastlane install step in CI. Skipped for now since listing copy changes rarely.
- **Pre-release gating** — skip `publish-android` on tags containing `-` (e.g., `v1.0.0-beta`).
- **Per-platform release notes** — currently the same whatsnew is used for Android and all other platforms. Splitting is low-value until we have Android-specific changes that don't apply elsewhere.
- **Automatic promotion internal → open testing** — possible via a scheduled workflow that checks internal crash rates. Premature until we have tester volume.

## Related docs

- `docs/plans/2026-04-07-android-play-store-design.md` — original design spec
- `docs/plans/2026-04-07-android-play-store-plan.md` — implementation plan (code fixes)
- `docs/user/PRIVACY_POLICY.md` — privacy policy text (to be published at helixscreen.org/privacy)
- `scripts/generate-upload-keystore.sh` — one-time upload keystore generation
- `scripts/generate-whatsnew.sh` — per-release whatsnew extraction from CHANGELOG.md
