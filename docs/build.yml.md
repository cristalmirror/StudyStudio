# `.github/workflows/build.yml`

Status: draft for user approval; version 0.0.13.

## Purpose

Defines a GitHub Actions workflow that builds StudyStudio on every push and pull request targeting `main`. It runs on `ubuntu-latest` and reuses the repository's own Docker toolchain (see [Dockerfile](Dockerfile.md)) instead of installing GTK4/MinGW dependencies directly on the runner, so the CI build uses the same environment as a local build.

## Jobs and steps

| Step | Behavior |
| --- | --- |
| Checkout code | Clones the repository via `actions/checkout@v4`. |
| Build Docker toolchain image | Builds the `studystudio-builder` image from the repository `Dockerfile`. |
| Compile Linux + Windows binaries (release) | Runs `make all` inside the container, bind-mounting `src`, `include`, `build`, `Makefile`, `resources.xml`, and `interface.ui` — the same command documented in the [repository README](../README.md#compilation). |
| Compile Linux binary (debug) | Runs `make debug` inside the same container and volume layout, producing `build/studystudio-0.0.13_linux_debug` (see [Makefile](Makefile.md)). |
| Verify binaries were produced | Fails the job if the release Linux binary, release Windows binary, or debug Linux binary is missing from `build/`. |
| Upload binaries as artifacts | Publishes the three binaries via `actions/upload-artifact@v4`, downloadable from the workflow run for 90 days by default. |

## Triggers

Runs on `push` and `pull_request` events restricted to the `main` branch. A feature branch only triggers a run once a pull request targeting `main` is opened against it.

## Current limitations

- There is no Windows debug job. As of version 0.0.13 the `Makefile` does have a `win64-debug` target (see [Makefile](Makefile.md)), but this workflow has not been updated to build or upload it yet — only the release binaries and the Linux debug binary are compiled and verified here.
- No test step exists yet. This workflow only verifies that the release and debug builds compile and produce the expected binaries; it does not verify application behavior.
- Docker layer caching is not configured, so each run rebuilds the Fedora 40 image from scratch instead of reusing cached `dnf` layers.
- The `debug` step name has "Compile Linux binary (debug)" while the release step compiles both platforms; the naming is intentional since only Linux has a debug target, not an oversight.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). This workflow is orchestration, not application logic: it only invokes existing `Makefile` targets inside the existing `Dockerfile` image. Update it if those targets' names, output paths, or the number of platforms/variants change.
