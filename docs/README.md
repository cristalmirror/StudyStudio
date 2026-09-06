# StudyStudio documentation

Status: draft for user approval. Baseline: source code reviewed at version 0.0.7.

StudyStudio is an early-stage desktop application written in C with GTK4. Its current UI adds numbered buttons and demonstrates construction of a subject object. Archive compression and decompression exist in the source but are not connected to the UI.

## File documentation

Each C source/header has its own document. UI resources and build configuration are also documented because they are part of the application workflow. Generated build outputs are covered by their generator rather than individually.

| Project file | Documentation |
| --- | --- |
| `src/main.c` | [Application entry point and UI callbacks](main.c.md) |
| `include/subject.h` | [Subject public interface](subject.h.md) |
| `src/subject.c` | [Subject implementation](subject.c.md) |
| `resources.c` | [Generated resource source](resources.c.md) |
| `interface.ui` | [GTK interface definition](interface.ui.md) |
| `resources.xml` | [Resource manifest](resources.xml.md) |
| `Makefile` | [Build rules](Makefile.md) |
| `Dockerfile` | [Build environment](Dockerfile.md) |

## Development direction: SOLID

**From this point onward, new development and refactoring must follow SOLID principles.** This is the project's agreed development direction, not a claim that version 0.0.7 already satisfies every principle. Apply these principles through C modules, opaque types, and small operation tables where substitution is useful; a different language or a class hierarchy is not required.

| Principle | Project guideline |
| --- | --- |
| Single responsibility | Separate subject data, application workflows, GTK presentation, archive storage, and operating-system process handling according to their reasons to change. |
| Open/closed | Provide extension points for actual variation, such as storage implementations, without repeatedly changing application workflows. Avoid speculative abstraction. |
| Liskov substitution | Implementations of the same interface must preserve documented inputs, outputs, ownership, error behavior, and lifecycle requirements. |
| Interface segregation | Expose only the operations each consumer needs. Keep process handles and compression internals out of the subject interface. |
| Dependency inversion | Application workflows should depend on small contracts. Wire concrete storage and platform implementations at application startup rather than hard-coding them inside workflows. |

The intended direction is for UI callbacks to invoke application operations, for those operations to use subject data and storage contracts, and for infrastructure modules to implement those contracts. These boundaries are planned; they are not all present today.

As relevant code changes, document ownership and error contracts, return recoverable failures to callers, and verify observable behavior at the affected boundary. Existing limitations should be addressed incrementally. Keep each file document synchronized with implementation changes and clearly distinguish current behavior from planned behavior.

## Review scope

These documents describe a static source review. No build, runtime execution, or cross-platform validation was performed as part of this documentation task. Known limitations below are observations from the source, not an exhaustive defect audit.
