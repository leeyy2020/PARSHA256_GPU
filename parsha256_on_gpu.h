//
// Created by neville on 03.11.20.
//

#ifndef SHAONGPU_PARSHA256_ON_GPU_H
#define SHAONGPU_PARSHA256_ON_GPU_H

#define gpuErrchk(ans) { gpuAssert((ans), __FILE__, __LINE__); }

inline void gpuAssert(cudaError_t code, const char *file, int line, bool abort = true) {
    if (code != cudaSuccess) {
        fprintf(stderr, "GPUassert: %s %s %d\n", cudaGetErrorString(code), file, line);
        if (abort) exit(code);
    }
}


#include <cassert>
#include <string>
#include "parsha256_padding.h"
#include "parsha256_sha256.h"
#include "helper.cuh"
#include "parsha256_kernel_firstRound.cuh"
#include "parsha256_kernel_middleRounds.cuh"
#include "parsha256_kernel_decreasingRounds.cuh"
#include "parsha256_kernel_lastRound.cuh"
#include "parsha256_kernel_singleInvocation.cuh"
#include <sstream>
#include "cuda_profiler_api.h"


std::string parsha256_on_gpu(const std::string in, const bool benchmark = false) {


    uint64_t added_zeros_bits = 0; // How many 0 bits to addd

    const int n = 768; // Input per Node in bits
    const int m = 256; // Output per Node in bits


    int L = in.size() * sizeof(char) * 8;
    int T = 13; // Height of available processor tree
    int t = 0; // Effective height processor tree
    const int l = 0; // IV length

    uint64_t q;
    uint64_t r;
    uint64_t b;


    if (L <= delta(0)) {


        std::vector<int> padded = parsha256_padding(in, n - l - L); // IV padding0


        int *dev_ptr;
        gpuErrchk(cudaMalloc(&dev_ptr, 24 * sizeof(uint32_t)));
        gpuErrchk(cudaMemcpy(dev_ptr, padded.data(), 24 * sizeof(uint32_t), cudaMemcpyHostToDevice));

        parsha256_kernel_gpu_singleInvocation<<<1, 1>>>(dev_ptr, dev_ptr);
        gpuErrchk(cudaPeekAtLastError());
        gpuErrchk(cudaDeviceSynchronize());
        if (benchmark) {
            for (int i = 0; i < 10; i++) {
                parsha256_kernel_gpu_singleInvocation<<<1, 1>>>(dev_ptr, dev_ptr);
                gpuErrchk(cudaPeekAtLastError());
                gpuErrchk(cudaDeviceSynchronize());

            }
            cudaProfilerStart();
            for (int i = 0; i < 100; i++) {
                parsha256_kernel_gpu_singleInvocation<<<1, 1>>>(dev_ptr, dev_ptr);
            }
            cudaProfilerStop();
        }

        gpuErrchk(cudaMemcpy(padded.data(), dev_ptr, 8 * sizeof(int), cudaMemcpyDeviceToHost));




//        parsha256_sha256(padded.data(), padded.data() + 8, padded.data() + 16, padded.data());



        // Write intermediate result to input buffer
        const int diff = (n - m) / 32; // How many numbers to pad

        for (int i = 0; i < diff - 1; i++) {
            padded[8 + i] = 0;
        }
//        padded[m / 32 - 1] = _byteswap_ulong(L);
        padded[m / 32 - 1] = L;


        gpuErrchk(cudaMemcpy(dev_ptr, padded.data(), 24 * sizeof(uint32_t), cudaMemcpyHostToDevice));


//        for (int &i : padded) {
//            i = _byteswap_uint32(i);
//        }

        parsha256_kernel_gpu_singleInvocation<<<1, 1>>>(dev_ptr, dev_ptr);
        gpuErrchk(cudaPeekAtLastError());
        gpuErrchk(cudaDeviceSynchronize());
        if (benchmark) {
            for (int i = 0; i < 10; i++) {
                parsha256_kernel_gpu_singleInvocation<<<1, 1>>>(dev_ptr, dev_ptr);
                gpuErrchk(cudaPeekAtLastError());
                gpuErrchk(cudaDeviceSynchronize());
            }
            cudaProfilerStart();
            for (int i = 0; i < 100; i++) {
                parsha256_kernel_gpu_singleInvocation<<<1, 1>>>(dev_ptr, dev_ptr);
            }
            cudaProfilerStop();
        }

        gpuErrchk(cudaMemcpy(padded.data(), dev_ptr, 8 * sizeof(int), cudaMemcpyDeviceToHost));

//        parsha256_sha256(padded.data(), padded.data() + 8, padded.data() + 16, padded.data()); // Write intermediate result to input buffer

        std::string res_string = "";
        char buffer[50];
        for (int i = 0; i < 8; i++) {
            int curr = padded[i];
            sprintf(buffer, "%x", curr);
            res_string += buffer;

        }
        gpuErrchk(cudaFree(dev_ptr));

        return res_string;

    } else if (L < delta(1)) {

        added_zeros_bits += delta(1) - L;
        L = delta(1);
    }


    if (L >= delta(T)) {
        t = T;
    } else {
        for (int i = 1; i < T; i++) {
            if (delta(i) <= L && L < delta(i + 1)) {
                t = i;
                break;
            }
        }
    }

    if (L > delta(t)) {

        q = (L - delta(t)) / lambda(t);
        r = (L - delta(t)) % lambda(t);
        if (r == 0) {
            q--;
            r = lambda(t);
        }
    } else if (L == delta(t)) {
        q = 0;
        r = 0;
    }
    b = std::ceil(r / (double) (2 * n - 2 * m - l));

    const uint64_t R = q + t + 2; // Number of Parallel Rounds






    // 1. Padding
//    added_zeros_bits += b * (2 * n - 2 * m - l) - r; // How many zeros to padd in bits

    std::vector<int> padded;
//    std::vector<int> padded = parsha256_padding(in, added_zeros_bits);

    int middle_rounds;
// Message Padding fix
    for (uint64_t i = 0;; i++) {
        uint64_t message_length = std::pow(2, t) * n
                                  + i * (std::pow(2, t - 1) * (n - 2 * m) + std::pow(2, t - 1) * n)
                                  + (n - 2 * m) * (std::pow(2, t));

        if (message_length >= L) {

            uint64_t diff = message_length - L + added_zeros_bits;
            middle_rounds = i;
            padded = parsha256_padding(in, diff);
            break;
        }
    }


    // 2. Change byte ordering since SHA-256 uses big endian byte ordering
    // This is only necessary to get the same result as other implementations
//    for (int &i : padded) {
//        i = _byteswap_uint32(i);
//    }
//

    int threads = std::pow(2, t);
    int threads_per_threadsblock = std::min(64, threads);
    int thread_blocks = (threads_per_threadsblock + threads - 1) / threads_per_threadsblock;

    // Copy data to gpu memory
    int *dev_In;
    int *dev_buf1;
    int *dev_buf2;
    int *out;


    gpuErrchk(cudaMalloc(&dev_In, padded.size() * sizeof(uint32_t)));
    gpuErrchk(cudaMalloc(&dev_buf1, 8 * sizeof(int) * std::max(threads_per_threadsblock * thread_blocks, 24)));
    gpuErrchk(cudaMalloc(&dev_buf2, 8 * sizeof(int) * std::max(threads_per_threadsblock * thread_blocks, 24)));
    gpuErrchk(cudaMalloc(&out, 8 * sizeof(int)));

    int *dev_In_orignal = dev_In;
    int *dev_buf1_original = dev_buf1;
    int *dev_buf2_orignal = dev_buf2;
    int *out_orignal = out;


    gpuErrchk(cudaMemcpy(dev_In, padded.data(), padded.size() * sizeof(int), cudaMemcpyHostToDevice));

//    // Cal kernel





// First Round
    parsha256_kernel_gpu_firstRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1);
    gpuErrchk(cudaPeekAtLastError());
    gpuErrchk(cudaDeviceSynchronize());
    if (benchmark) {
        for (int i = 0; i < 10; i++) {
            parsha256_kernel_gpu_firstRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1);
            gpuErrchk(cudaPeekAtLastError());
            gpuErrchk(cudaDeviceSynchronize());
        }
        cudaProfilerStart();
        for (int i = 0; i < 100; i++) {
            parsha256_kernel_gpu_firstRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1);
        }
        cudaProfilerStop();
    }

    dev_In += threads * 24; // Consumed Message so far, every threads consumes 24 integers


    // Rounds 2 to p + 1
//    const int p = R - t - 1;

    for (int i = 0; i < middle_rounds; i++) {

        parsha256_kernel_gpu_middleRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1, dev_buf2);
        gpuErrchk(cudaPeekAtLastError());
        gpuErrchk(cudaDeviceSynchronize());
        if (benchmark) {
            for (int i = 0; i < 10; i++) {
                parsha256_kernel_gpu_middleRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1, dev_buf2);
                gpuErrchk(cudaPeekAtLastError());
                gpuErrchk(cudaDeviceSynchronize());
            }
            cudaProfilerStart();
            for (int i = 0; i < 100; i++) {
                parsha256_kernel_gpu_middleRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1, dev_buf2);
            }
            cudaProfilerStop();
        }


        dev_In += 8 * (threads / 2) + 24 * (threads / 2); // Consumed Message so far, half of the threads consume 8 ints (non leafs), other halfs consumes again 24 ints
        std::swap(dev_buf1, dev_buf2);
    }


    // Decreasing Rounds
    for (int i = 0; i < t; i++) {

        parsha256_kernel_gpu_decreasingRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1, dev_buf2);
        gpuErrchk(cudaPeekAtLastError());
        gpuErrchk(cudaDeviceSynchronize());
        if (benchmark) {
            for (int i = 0; i < 10; i++) {
                parsha256_kernel_gpu_decreasingRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1, dev_buf2);
                gpuErrchk(cudaPeekAtLastError());
                gpuErrchk(cudaDeviceSynchronize());
            }
            cudaProfilerStart();
            for (int i = 0; i < 100; i++) {
                parsha256_kernel_gpu_decreasingRound<<<thread_blocks, threads_per_threadsblock>>>(dev_In, dev_buf1, dev_buf2);
            }
            cudaProfilerStop();
        }
        std::swap(dev_buf1, dev_buf2);
        threads /= 2;
        threads_per_threadsblock = std::min(128, threads);
        dev_In += 8 * threads; // Half of the thread consume 8 ints the other half copy their stuff around
        thread_blocks = (threads_per_threadsblock + threads - 1) / threads_per_threadsblock;

    }

    std::swap(dev_buf1, dev_buf2);
//    assert(threads == 1)
    dev_In += 8;
    parsha256_kernel_gpu_lastRound<<<1, 1>>>(dev_In, dev_buf1, dev_buf2, out, b, L);
    gpuErrchk(cudaPeekAtLastError());
    gpuErrchk(cudaDeviceSynchronize());
    if (benchmark) {
        for (int i = 0; i < 10; i++) {
            parsha256_kernel_gpu_lastRound<<<1, 1>>>(dev_In, dev_buf1, dev_buf2, out, b, L);
            gpuErrchk(cudaPeekAtLastError());
            gpuErrchk(cudaDeviceSynchronize());
        }
        cudaProfilerStart();
        for (int i = 0; i < 100; i++) {
            parsha256_kernel_gpu_lastRound<<<1, 1>>>(dev_In, dev_buf1, dev_buf2, out, b, L);
        }
        cudaProfilerStop();
    }



//    // Copy result back
    std::vector<int> res_int(8);
    gpuErrchk(cudaMemcpy(res_int.data(), out, 8 * sizeof(int), cudaMemcpyDeviceToHost));


    // Convert Result to String
    std::string res_string = "";
    char buffer[50];
    for (int i = 0; i < res_int.size(); i++) {
        int curr = res_int[i];
        sprintf(buffer, "%x", curr);
        res_string += buffer;

    }


    gpuErrchk(cudaFree(dev_In_orignal));

    gpuErrchk(cudaFree(dev_buf1_original));

    gpuErrchk(cudaFree(dev_buf2_orignal));

    gpuErrchk(cudaFree(out_orignal));


    return res_string;
}

void parsha256_on_gpu_test() {

    std::cout << parsha256_on_gpu("abc") << std::endl;
    std::cout << parsha256_on_gpu("abcdefgh") << std::endl;
    std::cout << parsha256_on_gpu(std::string(10000, 'a')) << std::endl;

    parsha256_on_gpu("abc");
    parsha256_on_gpu("abcdefgh");
    parsha256_on_gpu(std::string(10000, 'a'));

}

void parsha256_on_gpu_bench(int i) {


    std::cout << std::pow(10, i) << ": " << parsha256_on_gpu(std::string(std::pow(10, i), 'a'), true) << std::endl << std::flush;


}


#endif //SHAONGPU_PARSHA256_ON_GPU_H
