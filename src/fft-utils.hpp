#pragma once

#include <vector>
#include <complex>
#include <cmath>

// Simple FFT implementation
// Based on standard Cooley-Tukey algorithm
// Note: Size must be power of 2

class SimpleFFT {
public:
	static void Compute(const std::vector<float> &input, std::vector<float> &output_magnitudes)
	{
		size_t n = input.size();
		if (n == 0)
			return;

		// Check if power of 2 (omitted for brevity, assume caller ensures)

		std::vector<std::complex<float>> data(n);
		for (size_t i = 0; i < n; i++) {
			data[i] = std::complex<float>(input[i], 0.0f);
		}

		fft(data);

		output_magnitudes.resize(n / 2);
		for (size_t i = 0; i < n / 2; i++) {
			output_magnitudes[i] = std::abs(data[i]);
		}
	}

private:
	static void fft(std::vector<std::complex<float>> &x)
	{
		size_t n = x.size();
		if (n <= 1)
			return;

		std::vector<std::complex<float>> even(n / 2);
		std::vector<std::complex<float>> odd(n / 2);

		for (size_t i = 0; i < n / 2; i++) {
			even[i] = x[2 * i];
			odd[i] = x[2 * i + 1];
		}

		fft(even);
		fft(odd);

		for (size_t k = 0; k < n / 2; k++) {
			std::complex<float> t = std::polar(1.0f, -2.0f * (float)M_PI * k / n) * odd[k];
			x[k] = even[k] + t;
			x[k + n / 2] = even[k] - t;
		}
	}
};
