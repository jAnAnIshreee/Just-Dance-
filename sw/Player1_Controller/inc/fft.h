/**
 * @file fft.h
 * @author Jared McArthur
 * @brief Header file to abstract out STM32 complex FFT functions/assembly files
 * @version 0.1
 * @date 2022-11-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#pragma once

#include <stdint.h>

/** If the input data to an FFT is real-valued (as most real-world data is), you
 * can either use a real FFT or a complex FFT (set the imaginary portion of
 * the signal to 0).
 * https://www.ni.com/docs/en-US/bundle/labview/page/lvanlsconcepts/lvac_real_complex_fft.html
 */

/**
 * @brief Fast fourier transform with 64 elements. All the signal samples must
 * be 32-bit data containing the 16-bit real part followed by the 16-bit
 * imaginary part (in the little Endian order: imaginary_real). The output array
 * will be overwritten.
 * 
 * Can be used for real-valued data.
 * 
 * @param out   output buffer (64 elements)
 * @param in    input buffer (64 elements)
 */
void fft_formatted_64(uint32_t out[], uint32_t in[]);

/**
 * @brief Fast fourier transform with 64 elements. Pass in the real and the
 * imaginary arrays. The output and input arrays will be overwritten.
 * 
 * Slower than the formatted version.
 * 
 * @param out   output buffer (64 elements)
 * @param in    input buffer (64 elements)
 * @param real  real number input buffer (64 elements)
 * @param imag  imaginary number input buffer (64 elements)
 */
void fft_unformatted_64(uint32_t out[], uint32_t in[], uint16_t real[], uint16_t imag[]);

/**
 * @brief Fast fourier transform with 256 elements. All the signal samples must
 * be 32-bit data containing the 16-bit real part followed by the 16-bit
 * imaginary part (in the little Endian order: imaginary_real). The output array
 * will be overwritten.
 * 
 * Can be used for real-valued data.
 * 
 * @param out   output buffer (256 elements)
 * @param in    input buffer (256 elements)
 */
void fft_formatted_256(uint32_t out[], uint32_t in[]);

/**
 * @brief Fast fourier transform with 64 elements. Pass in the real and the
 * imaginary arrays. The output and input arrays will be overwritten.
 * 
 * Slower than the formatted version.
 * 
 * @param out   output buffer (256 elements)
 * @param in    input buffer (256 elements)
 * @param real  real number input buffer (256 elements)
 * @param imag  imaginary number input buffer (256 elements)
 */
void fft_unformatted_256(uint32_t out[], uint32_t in[], uint16_t real[], uint16_t imag[]);

/**
 * @brief Fast fourier transform with 1024 elements. All the signal samples must
 * be 32-bit data containing the 16-bit real part followed by the 16-bit
 * imaginary part (in the little Endian order: imaginary_real). The output array
 * will be overwritten.
 * 
 * Can be used for real-valued data.
 * 
 * @param out   output buffer (1024 elements)
 * @param in    input buffer (1024 elements)
 */
void fft_formatted_1024(uint32_t out[], uint32_t in[]);

/**
 * @brief Fast fourier transform with 1024 elements. Pass in the real and the
 * imaginary arrays. The output and input arrays will be overwritten.
 * 
 * Slower than the formatted version.
 * 
 * @param out   output buffer (1024 elements)
 * @param in    input buffer (1024 elements)
 * @param real  real number input buffer (1024 elements)
 * @param imag  imaginary number input buffer (1024 elements)
 */
void fft_unformatted_1024(uint32_t out[], uint32_t in[], uint16_t real[], uint16_t imag[]);
