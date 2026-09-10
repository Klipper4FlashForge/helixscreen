# 1.0 Announcement Rollout

Posting the HelixScreen 1.0 announcement to the 3D printing communities. Written 2026-09-09,
the day `v1.0.0` shipped, to be executed in a later session.

**Delete this file when the rollout is done** (plan lifecycle: `docs/CLAUDE.md`).

## State at the time of writing

`v1.0.0` is tagged from `release/1.0`, the release pipeline went fully green across all nine
shipping platforms, 35 assets are published, the stable R2 channel is live so existing 0.99
installs are being offered the update, and the docs deploy fired.

Not done: nothing has been posted anywhere.

## Artifacts, both OUTSIDE the repo

| Path | What |
|------|------|
| `~/Code/Printing/helixscreen-1.0-announcement-draft.md` | The announcement. ~2670 words, 166 lines. |
| `~/Code/Printing/helixscreen-1.0-screenshots/` | 11 PNGs, all 800x480. |

The draft was fact-checked against the `v1.0.0` tag on 2026-09-09. Its numbers are measured, not
estimated: 204 tagged releases, 12,687 commits, 1,281 closed issues, 30,508 asset downloads,
13,295 test cases, 573K lines of C++, 351 XML layouts, 18 themes, 38 dashboard widgets, 93
printers in the detection database, 9 languages at 2,868 translated strings each, 8 AMS backends.
Do not re-round these without re-measuring.

## Screenshot inventory

All captured 2026-09-09. Resolution is deliberately **800x480**: the default home layout only
lays out correctly at `medium`, and larger breakpoints leave a gap and clip the Print Library
card (see #1551).

| File | Provenance | Verdict |
|------|-----------|---------|
| `01-home-voron.png` | real Voron 2.4 over `--moonraker` | use |
| `03-multi-material-afc.png` | real Box Turtle / AFC rig | use, strongest image in the set |
| `04-bed-mesh-ad5m.png` | real AD5M, full-bed 17x17, 0.457mm | use |
| `04-bed-mesh-k2.png` | real K2 Plus, 1.605mm | ugly bed, but right for a Creality-board post |
| `04-bed-mesh-voron.png` | real Voron, 0.045mm but a 3x3 adaptive patch | renders as a small patch, weak |
| `05-print-history.png` | real Voron, 829 prints / 1016h / 8.7km | use. Preston is fine with the 43% success rate showing |
| `05-print-history-mock.png` | mock | unused alternative |
| `06-zoffset-voron.png` | real Voron | use, shows the guided paper-test steps |
| `07-console-voron.png` | real Voron, AFC cut/park log | use, authentic |
| `08-wizard-language.png` | real Voron, wizard step 1 | use, but recaption toward languages |
| `09-network.png` | mock, real MAC redacted | 10 synthetic SSIDs. Optional, WiFi shows "Not connected" |

## Blocking work, in order

1. **Wire the real screenshots into the draft.** It still carries eight bracketed placeholder
   captions in its Screenshots section. The files exist; the insertion does not.
2. **Settle the two captions with no source.**
   - *"Exclude Object Map: tap to exclude during prints"* needs an active print. Mock has
     `HELIX_MOCK_EXCLUDE_OBJECTS`; on real hardware it needs a print started, which requires
     Preston's per-command confirmation. Either capture from mock, or drop the slot.
   - *"Label Printing: QR codes on Brother/Niimbot printers"* only reaches a setup form via the
     `label-printer` recipe, not a printed label. Recaption to match, or drop.
3. **Capture a Snapmaker U1 and an Elegoo CC1 shot** if the vendor-board posts are going out.
   Both were reachable on 2026-09-09. Note the CC1 serves Moonraker on **port 80**, not 7125.
4. **Confirm exact subreddit names** for the Snapmaker U1, Flashforge and Elegoo communities.
   Do not guess these.
5. **Check each target sub's self-promotion rules** before posting. Unverified as of writing.
   Several vendor subs restrict project posts or route them to a weekly thread.

## Board plan

Decided with Preston on 2026-09-09.

**Tier 1, post first:** `r/klippers` and `r/VORONDesign`. His history is there (the Feb 2026 beta
post drew 123 upvotes / 117 comments on r/VORONDesign) and the draft is already written for that
audience.

**Tier 2, staggered after:** the native-platform boards, Creality / Flashforge / Snapmaker U1 /
Elegoo. For these the lead has to change. The strongest argument for those owners is *it runs on
your printer's own board, no Pi required*, which in the current draft is buried around item 11 of
the What's-changed list. Rewrite the opening so that leads, and swap the hero screenshot to their
hardware.

**QIDI:** marginal, worth a late try, no loss if skipped. **Anycubic:** skip, negligible user base.

| Board | Lead hook | Hero screenshot |
|-------|-----------|-----------------|
| klippers / VORONDesign | AFC and the configurable dashboard | `01-home-voron.png`, `03-multi-material-afc.png` |
| Creality | Native on K1/K1C/K2, no Pi | `04-bed-mesh-k2.png` |
| Flashforge | Native on AD5M/Pro via Forge-X or Klipper Mod | `04-bed-mesh-ad5m.png` |
| Snapmaker U1 | Four-head toolchanger, RFID spool recognition | needs capture |
| Elegoo | Native on COSMOS, factory white-balance calibration | needs capture |

## Traps

- **Do not post the same text to many subs in one day.** Reddit's own duplicate-content filter
  will remove posts or flag the account regardless of what mods think. Spread over roughly a week
  and vary the text meaningfully, not cosmetically.
- **Screenshots need a PII pass before publication.** Two captures on 2026-09-09 contained real
  identifiers. One was deleted outright (the wizard Network step, which showed an SSID, two MACs
  and two LAN IPs from a non-`--test` run against a real printer).
- **A mock-mode capture of the Network panel still shows the capturing machine's real Ethernet
  MAC**, by design: `ethernet_backend_mock.cpp` assigns `info.mac_address = real_mac_`. The WiFi
  mock is synthetic (`de:ad:be:ef:ca:fe`) and the IP is synthesized. Only the Ethernet MAC needs
  redacting. `09-network.png` is already redacted; the raw version was deleted.
- **Let the mock WiFi scan finish before capturing.** It initializes 11 networks asynchronously;
  capture too early and the panel reads "Available networks (0)".
- **The AD5X Forge-X PR is not merged.** Do not write as though it is.
- **Do not lead with sound.** Explicit instruction. It sits mid-list in the draft, unlabelled as a
  headline, and should stay there.

## Open decisions for Preston

- Whether the exclude-object slot gets a mock capture, a started print, or gets dropped.
- Whether the label-printing slot gets recaptioned or dropped.
- Exact subreddit names for the three vendor communities.
- Whether the vendor-board variants are worth the rewrite effort, or whether Tier 1 only is enough.

## Done when

The Tier 1 posts are up with real screenshots, the vendor variants are either posted or explicitly
dropped, and this file is deleted.
