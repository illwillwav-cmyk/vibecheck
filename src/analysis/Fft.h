#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace vibecheck
{
using Complex = std::complex<double>;

/** Iterative radix-2 Cooley-Tukey, in place, double precision throughout.

    JUCE's own FFT is single precision. Distortion measurement lives or dies on the noise floor
    of the transform, so the arithmetic here stays in double: a float FFT bottoms out around
    -140 dB, which is close enough to real harmonic levels to be worth avoiding. */
void fft (std::vector<Complex>& data, bool inverse = false);

/** Largest power of two less than or equal to n, or 0 when n is 0. */
std::size_t largestPowerOfTwoAtMost (std::size_t n);

enum class Window
{
    rectangular,    ///< No window. Correct for impulse responses, wrong for steady tones.
    hann,
    blackmanHarris  ///< Very low sidelobes, which is what harmonic measurement needs.
};

struct WindowGains
{
    /** Mean of the window. Scales a bin so a bin-centred tone reads as its own amplitude. */
    double coherent = 1.0;

    /** Root mean square of the window. Needed when a tone's energy is summed across several
        bins, which is the only way to measure a tone that does not sit exactly on a bin. */
    double rms = 1.0;

    /** Multiply a root-sum-of-squares across a main lobe by this to recover peak amplitude.
        Without it a Blackman-Harris measurement reads 3.01 dB high. */
    double lobeCorrection() const { return rms > 0.0 ? coherent / rms : 1.0; }
};

/** Applies the window in place and returns its gains. */
WindowGains applyWindow (std::vector<double>& samples, Window window);
} // namespace vibecheck
