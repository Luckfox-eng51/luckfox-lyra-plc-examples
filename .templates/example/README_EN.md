# Example Name

[简体中文](README.md) | **English**

> This is a template. Replace all placeholders, add the implementation, and
> verify it on the target board before publishing. Keep both READMEs in sync.

## Functionality

- Function: TODO: describe inputs, outputs, and behavior.
- Product and hardware revision: TODO.
- System and version: TODO: image, kernel, or runtime version.
- Verification: TODO: unverified, or verified environment and date.

## Hardware Wiring

| Board interface or pin | Peripheral interface or pin | Electrical requirements and notes |
| --- | --- | --- |
| TODO | TODO | TODO: voltage, power, grounding, termination, etc. |

TODO: list peripherals, wiring, and power-on conditions.
State that no wiring is required when the example needs no peripherals.

## Dependencies

TODO: list host and target dependencies, versions, and complete installation commands.

## Build

TODO: describe native or cross compilation, toolchain, commands, and output paths.
For interpreted languages or platform flows, state that no compilation is needed
and provide dependency setup or project import steps.

The template provides a Makefile entry point. Implement the applicable `build`,
`run`, `check`, and `clean` targets before use; they currently report an error.

## Run

TODO: provide the working directory, complete commands, required permissions,
parameter meanings, stop procedure, and device state on exit.
Make device paths, serial parameters, device IDs, register addresses, and network
addresses configurable.

Keep example configuration in `config/`. If systemd is needed, place service
files in `systemd/` and provide install, start, stop, and uninstall commands.
Remove unused directories.

Keep installation, deployment, and test scripts in the example's `scripts/`
directory, creating it when needed.

## Expected Results

TODO: provide verified output, peripheral behavior, or data and clear success criteria.

## Common Errors

| Error or symptom | Cause | Resolution |
| --- | --- | --- |
| TODO: actual error or symptom | TODO | TODO: actionable diagnosis or fix |
