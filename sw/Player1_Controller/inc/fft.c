/**
 * @file fft.c
 * @author Jared McArthur
 * @brief Implementation of the abstracted out FFT functions from the STM32 assembly files
 * @version 0.1
 * @date 2022-11-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "fft.h"

// Example usage of the assembly functions from http://www.bdtic.com/download/ST/UM0585.pdf
// #define N 64 /*Number of points*/
// uint32_t x[N],y[N]; /* input and output arrays */
// uint16_t real[N], imag[N]; /* real and imaginary arrays */
// /* Fill the input array */
// for(i=0; i<N; i++)
// x[i] = (((uint16_t)(real[i])) | ((uint32_t)(imag[i]<<16)));
// cr4_fft_64_stm32(y, x, N); /*computes the FFT of the x[N] samples*/

/**
 * Extern the relevant stm32 fft functions
 * Files that need to be included in the project;
 *  inc/cr4_fft_64_stm32.s
 *  inc/cr4_fft_256_stm32.s
 *  inc/cr4_fft_1024_stm32.s
 */

extern void cr4_fft_64_stm32(void* out, void* in, uint16_t n);
extern void cr4_fft_256_stm32(void* out, void* in, uint16_t n);
extern void cr4_fft_1024_stm32(void* out, void* in, uint16_t n);

void fft_formatted_64(uint32_t out[], uint32_t in[]) {
    cr4_fft_64_stm32(out, in, 64);
}

void fft_unformatted_64(uint32_t out[], uint32_t in[], uint16_t real[], uint16_t imag[]) {
    for (uint16_t i = 0; i < 64; ++i) {
        in[i] = ((uint16_t) (imag[i] << 16)) | ((uint16_t) real[i]);
    }
    cr4_fft_64_stm32(out, in, 64);
}

void fft_formatted_256(uint32_t out[], uint32_t in[]) {
    cr4_fft_256_stm32(out, in, 256);
}

void fft_unformatted_256(uint32_t out[], uint32_t in[], uint16_t real[], uint16_t imag[]) {
    for (uint16_t i = 0; i < 256; ++i) {
        in[i] = ((uint16_t) (imag[i] << 16)) | ((uint16_t) real[i]);
    }
    cr4_fft_256_stm32(out, in, 256);
}

void fft_formatted_1024(uint32_t out[], uint32_t in[]) {
    cr4_fft_1024_stm32(out, in, 1024);
}

void fft_unformatted_1024(uint32_t out[], uint32_t in[], uint16_t real[], uint16_t imag[]) {
    for (uint16_t i = 0; i < 1024; ++i) {
        in[i] = ((uint16_t) (imag[i] << 16)) | ((uint16_t) real[i]);
    }
    cr4_fft_1024_stm32(out, in, 1024);
}
