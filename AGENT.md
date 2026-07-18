# Agent Configuration for Zerus Game Engine

This file contains important information for AI agents working on the Zerus Game Engine project.

## Project Overview

Zerus is a cross-platform game engine written in pure C23. The project emphasizes:
- Memory safety and security
- Modern C development practices
- Cross-platform compatibility
- **Header-only development** - Most functionality implemented in headers
- Comprehensive testing and tooling

## Build Commands

### Configuration
```bash
# Debug build (default)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### Build
```bash
# Build project
cmake --build build -j4

# Clean build
cmake --build build --target clean
```

### Testing
```bash
# Run all tests
ctest --test-dir build --verbose

# Run specific test
./build/tests/test_engine
```

### Required Pre-Commit Verification

Before committing or pushing, inspect `.github/workflows/test.yml` and run the same configure,
build, and test commands locally in a clean build directory. Do not rely only on the existing
orb build: its compiler or dependency versions may differ from GitHub Actions. If the exact CI
toolchain is unavailable, align the environment first or explicitly report that CI parity was not
verified before committing.

### Code Quality
```bash
# Format all code
find src include tests -name "*.c" -o -name "*.h" -exec clang-format -i {} +

# Run linting (automatic with clangd in VS Code)
clang-tidy src/*.c src/**/*.c -- -Iinclude
```

## Development Workflow

### VS Code Tasks
- `Ctrl+Shift+P` → "Tasks: Run Task" → "Build" (or `Ctrl+Shift+B`)
- `Ctrl+Shift+P` → "Tasks: Run Task" → "Test"
- `Ctrl+Shift+P` → "Tasks: Run Task" → "Clean"
- `Ctrl+Shift+P` → "Tasks: Run Task" → "Format Code"

### Debugging
- `F5` - Run and debug main executable
- `Ctrl+F5` - Run without debugging
- Debug configurations available:
  - "Debug Engine" - Standard debugging
  - "Debug Engine (with AddressSanitizer)" - Memory debugging
  - "Debug Tests" - Debug unit tests

## Code Standards

### Naming Conventions
- **Functions**: `snake_case` (e.g., `engine_init`, `render_frame`)
- **Variables**: `snake_case` (e.g., `frame_count`, `is_running`)
- **Types**: `snake_case` with `_t` suffix (e.g., `engine_state_t`, `vector3_t`)
- **Constants/Macros**: `UPPER_CASE` (e.g., `MAX_ENTITIES`, `ENGINE_VERSION`)
- **File names**: `snake_case.c` and `snake_case.h`

### Code Style
- Indentation: 4 spaces (no tabs)
- Line length: 100 characters max
- Braces: Custom style (see `.clang-format`)
- Auto-formatting enabled on save

### Memory Safety
- All allocations must be paired with deallocations
- Use AddressSanitizer during development
- Prefer stack allocation when possible
- Check for null pointers before dereferencing
- Use `const` qualifiers where appropriate

### Error Handling
- Functions that can fail should return `bool` (success/failure)
- Use `fprintf(stderr, ...)` for error messages
- Critical errors should call `exit(EXIT_FAILURE)`
- Log errors with context information

## Project Structure

```
zerus/
├── src/                    # Implementation files
│   ├── main.c             # Application entry point
│   └── engine/            # Engine subsystems
│       └── core.c         # Core engine functionality
├── include/               # Public headers
│   └── engine/           # Engine APIs
│       └── core.h        # Core engine header
├── tests/                # Unit tests
│   ├── CMakeLists.txt    # Test build config
│   └── test_core.c       # Core engine tests
├── .vscode/              # VS Code configuration
├── docs/                 # Documentation
├── assets/               # Game assets
└── examples/             # Example programs
```

### Adding New Features (Header-Only Style)

1. **API Header**: Define public APIs in `include/engine/module.h`
2. **Implementation Header**: Implement in `include/engine/module_impl.h`
3. **Tests**: Add tests in `tests/` that include the `_impl.h` header
4. **Documentation**: Update relevant docs

Example structure:
```c
// include/engine/renderer.h - Public API
#pragma once
bool renderer_init(void);
void renderer_draw(void);

// include/engine/renderer_impl.h - Implementation
#pragma once
#include "renderer.h"
#ifndef RENDERER_IMPLEMENTATION
#define RENDERER_IMPLEMENTATION
bool renderer_init(void) { /* implementation */ }
void renderer_draw(void) { /* implementation */ }
#endif
```

### Adding New Files

1. **Header APIs**: Define in `include/engine/`
2. **Implementation**: Keep in header files using `#ifndef` guards
3. **Include Guards**: Always use `#pragma once`
4. **Main File**: Only `src/main.c` includes `_impl.h` headers
5. **Tests**: Include `_impl.h` headers directly

## Compiler Features

### C23 Features Available
- `auto` keyword for type inference
- Improved `_Generic` selections
- Enhanced `typeof` operator
- Binary literals (`0b101010`)
- Digit separators (`1'000'000`)
- `_Decimal128` type
- Enhanced bit manipulation

### Enabled Compiler Flags
- All warnings enabled and treated as errors
- Memory safety checks
- Stack protection
- Format string security
- Undefined behavior detection

## Dependencies

### Required Tools
- CMake 3.20+
- GCC/Clang with C23 support
- clang-format
- clang-tidy
- GDB (for debugging)

### Required Libraries
- GLFW3 (Graphics Library Framework) - window management and input
- GLM (OpenGL Mathematics) - header-only math library for graphics
- libshaderc - shader compilation library for GLSL to SPIR-V
- pkg-config (for dependency management)

### Installation (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install libglfw3-dev libglm-dev libshaderc-dev pkg-config
```

### Installation (macOS)
```bash
brew install glfw glm shaderc pkg-config
```

### Installation (Arch Linux)
```bash
sudo pacman -S glfw glm shaderc pkg-config
```

### Recommended VS Code Extensions
- C/C++ Extension Pack
- CMake Tools
- clangd (language server)
- Git Graph
- GitLens

## Common Issues

### Build Issues
- Ensure compiler supports C23
- Check CMake version >= 3.20
- Verify all tools are in PATH

### VS Code Issues
- Reload window after configuration changes
- Check that compile_commands.json exists in build/
- Ensure clangd extension is enabled

### Memory Issues
- Run with AddressSanitizer for debugging
- Use valgrind on Linux for additional checking
- Check for uninitialized variables

## Performance Considerations

- Profile before optimizing
- Use Release builds for performance testing
- Consider cache locality for game loops
- Minimize dynamic allocations in hot paths
- Use compiler intrinsics when needed

## Future Considerations

As the engine grows, consider adding:
- Asset loading system
- Rendering backend (OpenGL/Vulkan)
- Audio system
- Input handling
- Scripting integration
- Multi-threading support
- Platform-specific optimizations
