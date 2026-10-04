# Development

Status: Documentation foundation. There is no build system, public header, test executable, package, or runnable example yet.

## Work available now

Read the [architecture](../design/architecture.md), [UI model](../design/ui-model.md), and [current milestone](../roadmap/current.md). Documentation changes can be reviewed directly as Markdown. Check links and consistency before submitting changes.

There are no verified configure/build/test commands to run. Do not install a Vulkan SDK, Slang, or font libraries merely to edit these documents. Tool versions will be selected when the owning implementation modules are introduced.

## Foundation build requirements

The foundation milestone should introduce a minimal CMake project with a core-only build and an executable tree/serialization smoke. Select and document the C++ standard, compiler requirements, dependency acquisition, test runner, and output locations with that change.

Keep core-only setup independent of graphics SDKs. Optional backend/text modules should fail with actionable missing-dependency diagnostics only when requested. Public headers must not require engine/editor/platform headers for ordinary UI model use.

## Workflow to document after implementation

Add verified commands for:

1. Configuring a clean core-only build.
2. Building and running the relevant deterministic tests.
3. Enabling one concrete backend and compiling its shaders.
4. Running a minimal standalone host/menu example.
5. Installing and consuming exported targets, when packaging is implemented.

For each command, record supported shell/generator/configuration, required tools, expected output, and known limits. Verify commands on a clean build rather than inheriting reference-project presets or executable names.

## Examples to grow with milestones

| Proposed example | Purpose |
| --- | --- |
| `hello-ui` | Small document, inspect/serialize path, later primitive drawing |
| `flex-layout` | Numeric layout and visual geometry demonstration |
| `gamepad-menu` | Focus, keyboard/gamepad navigation, activation/cancel |
| `inventory` | Components, keyed lists, image assets, state/reload |

These names are planned directories, not runnable samples. Each example should prove one boundary and identify placeholder behavior. See [testing](testing.md) for verification scope and [dependencies](../reference/dependencies.md) for adoption policy.
