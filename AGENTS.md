# SKELLY UI AGENT SOURCE-OF-TRUTH 🩻

Any request for the "current UI", "show my UI", or a UI screenshot **must** refer to this repo's actual current JUCE `AudioProcessorEditor` C++ and generate screenshots from that compiled editor, locally if possible.

- **Never** generate illustrative UI art, present a WAV audio rendering as a UI snapshot, or call an old prebuilt PNG the current interface after source changes.
- Standard **four actual scenes**: default, real control/menu, active, one *supported* alternate theme/state. For a full responsive audit use `--full`.
- Shared builder/registry: `skellyruno/skellcore-vst-template/tools/quick4_projects.py`.
- Source policy/rollout: `skellyruno/skellcore-vst-template/docs/PLUGIN_QUICK4_ROLLOUT.md`.
- Local migration status here: **NOT YET VERIFIED for local Quick4**. If no working renderer is available, implement and test an opt-in native JUCE screenshot executable using this plugin's actual editor; disclose missing dependencies/permissions. No bogus placeholder.
- Windows VST3 release compilation, FL Studio compatibility, audio DSP, latency and CPU regression tests remain independently mandatory. Local PNG captures do NOT validate these.
- Do not run GitHub Actions screenshot workflows for routine UI changes; GitHub may be used to obtain source or independently validate Windows releases.
