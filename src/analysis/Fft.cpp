#include "Fft.h"

#include <cmath>

namespace vibecheck
{
namespace
{
constexpr double pi = 3.14159265358979323846;
}

std::size_t largestPowerOfTwoAtMost (std::size_t n)
{
    if (n == 0)
        return 0;

    std::size_t result = 1;

    while ((result << 1) <= n)
        result <<= 1;

    return result;
}

void fft (std::vector<Complex>& data, bool inverse)
{
    const auto n = data.size();

    if (n < 2)
        return;

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
            std::swap (data[i], data[j]);
    }

    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const auto angle = 2.0 * pi / (double) length * (inverse ? 1.0 : -1.0);
        const Complex step { std::cos (angle), std::sin (angle) };

        for (std::size_t start = 0; start < n; start += length)
        {
            Complex factor { 1.0, 0.0 };

            for (std::size_t offset = 0; offset < length / 2; ++offset)
            {
                const auto even = data[start + offset];
                const auto odd  = data[start + offset + length / 2] * factor;

                data[start + offset]                = even + odd;
                data[start + offset + length / 2]   = even - odd;

                factor *= step;
            }
        }
    }

    if (inverse)
        for (auto& value : data)
            value /= (double) n;
}

WindowGains applyWindow (std::vector<double>& samples, Window window)
{
    const auto n = samples.size();

    if (n == 0 || window == Window::rectangular)
        return {};

    double sum = 0.0;
    double sumOfSquares = 0.0;

    for (std::size_t i = 0; i < n; ++i)
    {
        const auto position = (double) i / (double) n;
        double value = 1.0;

        if (window == Window::hann)
        {
            value = 0.5 - 0.5 * std::cos (2.0 * pi * position);
        }
        else
        {
            // Blackman-Harris, 4 term: sidelobes below -92 dB, so harmonics are not buried
            // under leakage from the fundamental.
            value = 0.35875
                    - 0.48829 * std::cos (2.0 * pi * position)
                    + 0.14128 * std::cos (4.0 * pi * position)
                    - 0.01168 * std::cos (6.0 * pi * position);
        }

        samples[i] *= value;
        sum += value;
        sumOfSquares += value * value;
    }

    return { sum / (double) n, std::sqrt (sumOfSquares / (double) n) };
}
} // namespace vibecheck
