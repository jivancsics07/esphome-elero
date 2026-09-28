# Local patches (Raffstore / venetian variant)

This branch is upstream `main` (including
[#66](https://github.com/manuschillerdev/esphome-elero/pull/66), ESPHome 2026.9
support) plus the gateway's local changes, ported from
[`deyanp/esphome-elero@local/schlotterer-variant`](https://github.com/deyanp/esphome-elero/tree/local/schlotterer-variant).
It is meant to be installed directly — no `git apply`, no frontend build, no
release download.

## Install

```yaml
esphome:
  min_version: 2026.9.0

external_components:
  - source: github://jivancsics07/esphome-elero@claude/clever-goldberg-qjzra4
    components: [elero, elero_nvs, elero_web]  # or elero_mqtt
    refresh: 1d
```

Then pick an output adapter as in the [README](README.md#2-choose-your-output-adapter):
`api:` + `elero_nvs:` (native API) or `mqtt:` + `elero_mqtt:`, plus `elero_web:`.
List the components you use in `components:`. `elero_nvs` compiles again since
[#66](https://github.com/manuschillerdev/esphome-elero/pull/66), which fixed
[#59](https://github.com/manuschillerdev/esphome-elero/issues/59).

`components/elero_web/elero_web_ui.h` is committed on this branch, so the build
does not need to fetch the pre-built web UI from a GitHub release. After changing
the frontend, rebuild it with `cd components/elero_web/frontend/app && pnpm build`
and commit the regenerated header.

## What is on top of upstream

| Change | Upstream | Status |
|--------|----------|--------|
| `logger::LogListener` → `add_log_callback` | [#58](https://github.com/manuschillerdev/esphome-elero/pull/58) | already in upstream via #66 — not carried |
| `ELERO_VERSION` pointed at a non-existent 0.9.0 release; UI download failure is now a hard error | [#57](https://github.com/manuschillerdev/esphome-elero/pull/57) | carried |
| ESP32-C6 compile target, CI matrix entry, device docs (ESPHome version bump dropped — #66 already pins 2026.9.0) | [#60](https://github.com/manuschillerdev/esphome-elero/pull/60) | carried |
| CC1101 health line demoted from `WARN` to `VERBOSE` | [#61](https://github.com/manuschillerdev/esphome-elero/pull/61) | carried |
| Direction-aware cover tilt: `command_cover_tilt(dev, open)`, MQTT tilt payload read instead of dropped | [#62](https://github.com/manuschillerdev/esphome-elero/pull/62) | carried |
| Packet-copy button works over plain HTTP (no `navigator.clipboard`) | [#64](https://github.com/manuschillerdev/esphome-elero/pull/64) | carried |
| Device names up to 47 bytes instead of 23 (UTF-8 safe, v3 → v4 NVS migration on boot) | — (not yet proposed) | carried |
| `elero_packet.h` command byte remap, `cover_sm.cpp` `TILT_UP`/`TILT_DOWN` branches, `elero_strings.cpp` names | never — see [#63](https://github.com/manuschillerdev/esphome-elero/issues/63) | stays local |

When an upstream PR merges, drop the matching commit on the next rebase onto
upstream `main`.

## Applying to upstream by hand

`patches/` holds the same commits as a `git format-patch` series against upstream
commit `951d6fb` (#66):

```bash
git clone https://github.com/manuschillerdev/esphome-elero
cd esphome-elero
git checkout 951d6fb
git am /path/to/patches/*.patch
```
