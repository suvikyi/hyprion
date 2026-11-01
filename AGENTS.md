# Agent guidance

## Versioning and releases

Use Semantic Versioning 2.0.0 for Hyprion. Keep releases pre-1.0 until the user-facing configuration and effect behavior are considered stable.

The consumer-facing contract includes the Hyprpm plugin ID (`hyprion`), Lua settings under `plugin.hyprion.*`, documented defaults and effect behavior, and the published install identity.

### Before 1.0.0

Keep the major version at `0`:

- **Patch** (`0.y.Z`): backwards-compatible bug fixes and corrections.
- **Minor** (`0.Y.0`): new features or settings, and any breaking change to the consumer-facing contract. Reset the patch number to zero.

Prefer additive configuration changes. Because the project is pre-1.0, do not add compatibility aliases for removed or renamed settings by default; update examples and migration instructions when a breaking change is made.

### From 1.0.0 onward

- **Patch**: backwards-compatible fixes.
- **Minor**: backwards-compatible features and settings.
- **Major**: incompatible changes to the consumer-facing contract.

Use `1.0.0` when the configuration surface and documented behavior are ready to be treated as stable, not merely because a date or feature count was reached.

### Release procedure

- Keep `project(... VERSION ...)` in `CMakeLists.txt` and the version returned by `PLUGIN_DESCRIPTION_INFO` in `src/main.cpp` identical. These are the project's version metadata; update both in the same release change.
- `hyprpm.toml` has no independent plugin-version field. Do not introduce a second version value there.
- Bump for user-visible code or packaging changes; documentation-only edits and internal refactors with no user-visible change do not require a bump.
- Before release, build the plugin and verify the two version values match. Choose the bump from the highest-impact user-visible change in the release.
- Tag releases `vMAJOR.MINOR.PATCH` (for example, `v0.1.0`) and publish GitHub release notes against that tag. Do not move or rewrite a published version tag; correct a released mistake with a subsequent release.

## Local plugin verification

- Run every test that spawns windows, starts a nested compositor, or takes screenshots on **DP-3** in suvikyi's (soryu) environment. Use DP-3 for test surfaces and captures; leave DP-1 and DP-2 untouched.
- Verify renderer changes in an isolated nested compositor before touching the desktop session.
- Never overwrite a loaded shared library in place, even after a plugin unload: the dynamic loader may retain its mapping. Install to a temporary file on the same filesystem and rename it atomically.
- Use a fresh compositor session for updated native-hook code; do not hot-reload the desktop plugin to verify it.
