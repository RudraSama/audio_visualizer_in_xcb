#ifndef FFT_H
#define FFT_H

#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

void _fft(double complex *x, unsigned int N, double complex *result);
double complex *fft(double complex *x, unsigned int N);

#endif
