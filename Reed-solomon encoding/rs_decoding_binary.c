#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Reed-Solomon parameters (CCSDS standard)
#define N 255           // Codeword length
#define K 223           // Information symbols  
#define T 16            // Error correction capability
#define PARITY 32       // Parity symbols (2*T)
#define PRIM_POLY 0x11D // x^8 + x^7 + x^2 + x + 1
#define ALPHA 0x02      // Primitive element

// Galois field lookup tables
uint8_t gf_exp[512];
uint8_t gf_log[256];

void init_galois_field(void) {
    uint16_t temp = 1;
    
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)temp;
        gf_log[temp] = i;
        temp <<= 1;
        if (temp & 0x100) temp ^= PRIM_POLY;
    }
    
    // Extend table for convenience
    for (int i = 255; i < 512; i++) {
        gf_exp[i] = gf_exp[i - 255];
    }
    gf_log[0] = 255; // Special case for log(0)
}

uint8_t gf_mult(uint8_t a, uint8_t b) {
    return (a == 0 || b == 0) ? 0 : gf_exp[gf_log[a] + gf_log[b]];
}

uint8_t gf_div(uint8_t a, uint8_t b) {
    if (b == 0) return 0; 
    return (a == 0) ? 0 : gf_exp[gf_log[a] + 255 - gf_log[b]];
}

uint8_t gf_pow(uint8_t base, int exp) {
    return (base == 0) ? ((exp == 0) ? 1 : 0) : gf_exp[(gf_log[base] * exp) % 255];
}

void compute_syndromes(uint8_t *received, uint8_t *syndromes) {
    for (int i = 0; i < PARITY; i++) {
        syndromes[i] = 0;
        uint8_t alpha_i = gf_pow(ALPHA, i);
        for (int j = 0; j < N; j++) {
            syndromes[i] ^= gf_mult(received[j], gf_pow(alpha_i, j));
        }
    }
}

int berlekamp_massey(uint8_t *syndromes, uint8_t *lambda, uint8_t *omega) {
    uint8_t prev_lambda[PARITY + 1] = {0};
    uint8_t temp[PARITY + 1];
    
    lambda[0] = 1;
    prev_lambda[0] = 1;
    int deg_lambda = 0, deg_prev = 0;
    
    for (int k = 0; k < PARITY; k++) {
        uint8_t disc = syndromes[k];
        for (int i = 1; i <= deg_lambda; i++) {
            disc ^= gf_mult(lambda[i], syndromes[k - i]);
        }
        
        if (disc != 0) {
            memcpy(temp, lambda, sizeof(temp));
            
            for (int i = 0; i <= deg_prev; i++) {
                if (prev_lambda[i] != 0) {
                    int pos = i + (k - deg_prev);
                    if (pos <= PARITY) {
                        lambda[pos] ^= gf_mult(disc, prev_lambda[i]);
                    }
                }
            }
            
            int new_deg = deg_prev + (k - deg_prev);
            if (new_deg > deg_lambda) {
                deg_lambda = new_deg;
                memcpy(prev_lambda, temp, sizeof(prev_lambda));
                deg_prev = k - deg_lambda;
                
                // Scale previous lambda
                uint8_t inv_disc = gf_exp[255 - gf_log[disc]];
                for (int i = 0; i <= PARITY; i++) {
                    prev_lambda[i] = gf_mult(prev_lambda[i], inv_disc);
                }
            }
        }
    }
    
    // Compute error evaluator polynomial
    memset(omega, 0, PARITY);
    for (int i = 0; i < PARITY; i++) {
        for (int j = 0; j <= deg_lambda && j <= i; j++) {
            omega[i] ^= gf_mult(syndromes[i - j], lambda[j]);
        }
    }
    
    return deg_lambda;
}

int find_and_correct_errors(uint8_t *lambda, uint8_t *omega, int deg_lambda, uint8_t *corrected) {
    int error_count = 0;
    
    for (int i = 0; i < N; i++) {
        uint8_t sum = 0;
        
        // Evaluate error locator polynomial at alpha^(-i)
        for (int j = 0; j <= deg_lambda; j++) {
            if (lambda[j] != 0) {
                sum ^= gf_mult(lambda[j], gf_pow(ALPHA, (255 - i * j) % 255));
            }
        }
        
        if (sum == 0) {  
            error_count++;
            
            // Check bounds to prevent overflow
            if (error_count > T) {
                return -1; 
            }
            
            uint8_t alpha_inv_i = gf_pow(ALPHA, (255 - i) % 255);
            
            // Compute error evaluator value
            uint8_t omega_val = 0;
            for (int j = 0; j < PARITY; j++) {
                if (omega[j] != 0) {
                    omega_val ^= gf_mult(omega[j], gf_pow(alpha_inv_i, j));
                }
            }
            
            // Compute error locator derivative
            uint8_t lambda_prime = 0;
            for (int j = 1; j <= deg_lambda; j += 2) {
                if (lambda[j] != 0) {
                    lambda_prime ^= gf_mult(lambda[j], gf_pow(alpha_inv_i, j - 1));
                }
            }
            
            // Apply correction if derivative is non-zero
            if (lambda_prime != 0) {
                corrected[i] ^= gf_div(omega_val, lambda_prime);
            }
        }
    }
    
    // Validate error count consistency
    return (error_count == deg_lambda) ? error_count : -1;
}

int rs_decode_block(uint8_t *received, uint8_t *corrected) {
    uint8_t syndromes[PARITY];
    uint8_t lambda[PARITY + 1] = {0};
    uint8_t omega[PARITY] = {0};
    
    memcpy(corrected, received, N);
    compute_syndromes(received, syndromes);
    
    int has_errors = 0;
    for (int i = 0; i < PARITY; i++) {
        if (syndromes[i] != 0) {
            has_errors = 1;
            break;
        }
    }
    
    if (!has_errors) return 0;
    
    // Find error locator and evaluator polynomials
    int deg_lambda = berlekamp_massey(syndromes, lambda, omega);
    if (deg_lambda == 0) return 0;
    
    // Find and correct errors
    return find_and_correct_errors(lambda, omega, deg_lambda, corrected);
}

int decode_file(const char *input_file, const char *output_file) {
    FILE *input_fp = fopen(input_file, "rb");
    if (!input_fp) {
        printf("Error: Cannot open input file\n");
        return -1;
    }
    
    FILE *output_fp = fopen(output_file, "wb");
    if (!output_fp) {
        printf("Error: Cannot create output file\n");
        fclose(input_fp);
        return -1;
    }
    
    // Get file size for proper handling
    fseek(input_fp, 0, SEEK_END);
    long file_size = ftell(input_fp);
    fseek(input_fp, 0, SEEK_SET);
    long total_blocks = file_size / N;
    
    uint8_t received_block[N], corrected_block[N];
    size_t bytes_read;
    int block_count = 0, corrected_blocks = 0, failed_blocks = 0;
    
    printf("Processing %ld blocks...\n", total_blocks);
    
    while ((bytes_read = fread(received_block, 1, N, input_fp)) > 0) {
        // Pad incomplete block with zeros
        if (bytes_read < N) {
            memset(received_block + bytes_read, 0, N - bytes_read);
        }
        
        int result = rs_decode_block(received_block, corrected_block);
        
        if (result == -1) {
            failed_blocks++;
            // Use original data when correction fails
            memcpy(corrected_block, received_block, N);
        } else if (result > 0) {
            corrected_blocks++;
        }
        
        // Determine output size
        size_t write_size = K;
        
        // Handle last block padding removal
        if (++block_count == total_blocks) {
            while (write_size > 0 && corrected_block[write_size - 1] == 0) {
                write_size--;
            }
        }
        
        if (fwrite(corrected_block, 1, write_size, output_fp) != write_size) {
            printf("Error: Write failed at block %d\n", block_count);
            break;
        }
    }
    
    fclose(input_fp);
    fclose(output_fp);
    
    printf("Decoding complete: %d blocks processed, %d corrected, %d failed\n", 
           block_count, corrected_blocks, failed_blocks);
    
    return 0;
}

int main(int argc, char *argv[]) {
    
    printf("Reed-Solomon Decoder (N=%d, K=%d, T=%d)\n", N, K, T);
    
    init_galois_field();
    int result = decode_file(argv[1], argv[2]);
    
    if (result == 0) {
        printf("All blocks decoded successfully\n");
    } else if (result == 1) {
        printf("Decoding completed with some uncorrectable blocks\n");
    } else {
        printf("Decoding failed\n");
    }
    
    return result;
}