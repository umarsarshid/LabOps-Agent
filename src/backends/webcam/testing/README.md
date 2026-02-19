# src/backends/webcam/testing

Webcam backend testing support files.

## Why this folder exists

Upcoming webcam commits need platform-independent tests for capability reporting, parameter handling, and frame-loop behavior. This folder is reserved for small fakes/fixtures so unit tests do not depend on physical webcams.

## Current state

- Placeholder folder for future mock providers and fixture helpers.

## Connection to the project

Keeps webcam test infrastructure close to backend code while preserving clean separation from production platform implementations.
