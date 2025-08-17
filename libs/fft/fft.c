#include "fft.h"

void _fft(double complex *x, unsigned int N, double complex *result) {
	if (N == 1) {
		result[0] = (double complex)x[0];
		return;
	}

	double complex *sub_even =
	    (double complex *)malloc(sizeof(double complex) * (N / 2));
	double complex *sub_odd =
	    (double complex *)malloc(sizeof(double complex) * (N / 2));

	for (unsigned int i = 0; i < N / 2; i++) {
		sub_even[i] = x[2 * i];
		sub_odd[i] = x[2 * i + 1];
	}

	double complex *even =
	    (double complex *)malloc(sizeof(double complex) * (N / 2));
	double complex *odd =
	    (double complex *)malloc(sizeof(double complex) * (N / 2));
	_fft(sub_even, N / 2, even);
	_fft(sub_odd, N / 2, odd);

	for (unsigned int k = 0; k < N / 2; k++) {
		double complex twiddle = cexp(-2 * I * PI * k / N) * odd[k];
		result[k] = even[k] + twiddle;
		result[k + N / 2] = even[k] - twiddle;
	}

	free(sub_even);
	free(sub_odd);
	free(even);
	free(odd);
}

double complex *fft(double complex *x, unsigned int N) {
	if (N > 262144 || N <= 0 || ((N - 1) & N) != 0) return NULL;

	double complex *result =
	    (double complex *)malloc(sizeof(double complex) * N);
	_fft(x, N, result);

	return result;
}
