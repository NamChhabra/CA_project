#include <iostream>
#include <vector>
#include <chrono>

// Matrix size (N x N)
// 512 * 512 * 4 bytes (int) = 1,048,576 bytes (1MB).
// This is larger than 32KB L1, making cache effects very visible.
#define N 256
#define TILE_SIZE 16 // Adjust this to see how different tile sizes affect AMAT

int A[N][N], B[N][N], C[N][N];

void initialize() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A[i][j] = i + j;
            B[i][j] = i - j;
            C[i][j] = 0;
        }
    }
}

// NAIVE VERSION: High Cache Misses due to poor stride in Matrix B
void multiply_naive() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            for (int k = 0; k < N; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

// TILED VERSION: High Cache Hits because tiles stay in L1/L2
void multiply_tiled() {
    for (int ih = 0; ih < N; ih += TILE_SIZE) {
        for (int jh = 0; jh < N; jh += TILE_SIZE) {
            for (int kh = 0; kh < N; kh += TILE_SIZE) {
                
                // Internal tile multiplication
                for (int il = ih; il < ih + TILE_SIZE; il++) {
                    for (int jl = jh; jl < jh + TILE_SIZE; jl++) {
                        for (int kl = kh; kl < kh + TILE_SIZE; kl++) {
                            C[il][jl] += A[il][kl] * B[kl][jl];
                        }
                    }
                }

            }
        }
    }
}

int main() {
    initialize();
    multiply_tiled();

    return 0;
}
