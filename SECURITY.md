# Security policy

## Supported versions

Tessera is pre-1.0. Security fixes land on `main` and in the next release; there are no maintenance branches.

| Version | Supported |
| --- | --- |
| `main` | Best effort |
| Latest release | Yes |
| Older releases | No; upgrade to the latest release |

## Reporting a vulnerability

Report suspected vulnerabilities privately rather than in a public issue. Use GitHub's [private vulnerability reporting](https://github.com/animu-sphere/tessera/security/advisories/new) for this repository.

Include, where possible:

- the affected release or commit;
- operating system, compiler, and build configuration, plus GPU/driver and Vulkan SDK when the optional backend is involved;
- a description of the issue and its impact;
- reproduction steps or a minimal input, such as a JSON v1 document;
- any suggested remediation.

Do not include credentials or private data in a report.

## What to expect

Tessera is maintained on a best-effort basis. We aim to acknowledge a report within a few business days, assess its severity, keep the reporter informed while a fix is developed, and coordinate disclosure once a fix is available.

## Scope

In scope are the core library, including JSON v1 loading and its documented [input bounds](formats/tessera-ui/README.md#rejection-and-bounds); the optional Vulkan backend; the example hosts; and the CI/release automation.

Report vulnerabilities in Vulkan, Slang, CMake, compilers, GPU drivers, or other third-party dependencies to their upstream maintainers. A private heads-up is welcome if Tessera's use of a dependency materially worsens the issue.
