#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cmath>
#include <iostream>

int main()
{
    static_assert(sizeof(void*) == 8, "Ironstack's initial Windows target is x64.");
    constexpr std::array<double, 3> values { 0.0, 0.5, 1.0 };
    for (const auto value : values)
        if (!std::isfinite(value))
            return 1;

    // Link a real SDK function: finding the compiler alone does not prove that
    // the Windows headers and import libraries are installed correctly.
    const auto processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (processors == 0)
        return 2;

    std::cout << "C++17/Win32 probe passed; pointer bits=" << sizeof(void*) * 8
              << "; processors=" << processors << '\n';
    return 0;
}
