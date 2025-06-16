# NS-3 Development Guide

## Build Commands
- Configure: `./ns3 configure --enable-examples --enable-tests`
- Build: `./ns3 build`
- Run simulation: `./ns3 run scratch/blockchain-simulator`
- Run specific example: `./ns3 run example-name`
- Run single test: `./test.py -s test-suite-name`
- Run all tests: `./test.py`

## Code Style
- Follow Allman-style braces with 4-space indentation
- Code formatting: `./utils/check-style-clang-format.py --fix .`
- Naming: CamelCase for classes/methods, camelBack for variables
- Prefixes: `m_` for member vars, `g_` for globals
- Header includes order: class header, same module, other modules, external
- Comments: Doxygen-style for public API, use `///` or `/**` style
- Use clang-tidy for static analysis: `./ns3 configure --enable-clang-tidy`

## Best Practices
- Header guards with uppercase filename: `MY_CLASS_H`
- Use smart pointers (Ptr<>) and check with `if (ptr)` not `if (ptr != nullptr)`
- Explicitly mark overridden functions with `override`
- Use Time with integer values: `MilliSeconds(100)` not `Seconds(0.1)`
- Always initialize variables at declaration
- Use `const` and reference types appropriately
- Keep code under 100 characters per line