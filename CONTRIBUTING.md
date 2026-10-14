# Contributing

StoryForge C++ uses C++20, CMake, and Conventional Commits. Keep the core free
of UI, database, and provider-specific dependencies.

1. Configure with `cmake -S . -B build -DSTORYFORGE_BUILD_TESTS=ON`.
2. Build with `cmake --build build`.
3. Run tests with `ctest --test-dir build --output-on-failure`.
4. Validate the example with `build/storyforge validate examples/clockwork-academy`.

Changes to the package format must update the schema, format documentation,
loader tests, validation tests, and example package in the same pull request.
Never commit credentials or copyrighted story settings.
