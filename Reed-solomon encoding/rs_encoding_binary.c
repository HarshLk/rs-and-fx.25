#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Reed-Solomon parameters according to CCSDS standard
#define N 255           // Total codeword length
#define K 223           // Information symbols  
#define T 16            // Error correction capability
#define PARITY 32       // Parity symbols (2*T)
#define GF_SIZE 256     // Galois field size (2^8)
#define PRIM_POLY 0x11D // Field generator polynomial: x^8 + x^7 + x^2 + x + 1
#define ALPHA 0x02      // Primitive element (alpha = 2)

// Global tables for Galois field operations
uint8_t gf_exp[512];    // Exponential table (extended for convenience)
uint8_t gf_log[256];    // Logarithm table
uint8_t generator[PARITY + 1]; // Generator polynomial coefficients

// Function prototypes
void init_galois_field(void);
uint8_t gf_mult(uint8_t a, uint8_t b);
uint8_t gf_div(uint8_t a, uint8_t b);
uint8_t gf_pow(uint8_t base, int exp);
void generate_polynomial(void);
void rs_encode_block(uint8_t *data, uint8_t *codeword);
int encode_file(const char *input_file, const char *output_file);
void print_polynomial(uint8_t *poly, int length, const char *name);

/**
 * Initialize Galois Field GF(2^8) lookup tables
 * Creates exponential and logarithm tables for efficient multiplication/division
 */
void init_galois_field(void) {
    int i;
    uint16_t temp = 1;
    
    // Initialize exponential table
    for (i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)temp;
        gf_log[temp] = i;
        
        // Multiply by primitive element (alpha = 2)
        temp <<= 1;
        if (temp & 0x100) {
            temp ^= PRIM_POLY; // Reduce modulo primitive polynomial
        }
    }
    
    // Extend exponential table for convenience (gf_exp[i] = gf_exp[i mod 255])
    for (i = 255; i < 512; i++) {
        gf_exp[i] = gf_exp[i - 255];
    }
    
    // Set log(0) to a special value (undefined, but we use 255)
    gf_log[0] = 255;
    
    printf("Galois Field GF(2^8) initialized successfully.\n");
}

/**
 * Galois Field multiplication
 * Uses lookup tables for efficiency: a * b = alpha^(log(a) + log(b))
 */
uint8_t gf_mult(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    return gf_exp[gf_log[a] + gf_log[b]];
}

/**
 * Galois Field division
 * Uses lookup tables: a / b = alpha^(log(a) - log(b))
 */
uint8_t gf_div(uint8_t a, uint8_t b) {
    if (a == 0) {
        return 0;
    }
    if (b == 0) {
        printf("Error: Division by zero in Galois Field\n");
        exit(1);
    }
    return gf_exp[gf_log[a] + 255 - gf_log[b]];
}

/**
 * Galois Field exponentiation
 * Computes base^exp in GF(2^8)
 */
uint8_t gf_pow(uint8_t base, int exp) {
    if (base == 0) {
        return (exp == 0) ? 1 : 0;
    }
    return gf_exp[(gf_log[base] * exp) % 255];
}

/**
 * Generate the Reed-Solomon generator polynomial
 * g(x) = (x - α^0)(x - α^1)...(x - α^(2t-1))
 * For CCSDS standard: g(x) = (x - α^0)(x - α^1)...(x - α^31)
 */
void generate_polynomial(void) {
    int i, j;
    
    // Initialize generator polynomial to 1
    memset(generator, 0, sizeof(generator));
    generator[0] = 1;
    
    // Multiply by (x - α^i) for i = 0 to PARITY-1
    for (i = 0; i < PARITY; i++) {
        uint8_t alpha_i = gf_pow(ALPHA, i);
        
        // Multiply current polynomial by (x - α^i)
        // Shift polynomial up by one degree
        for (j = i + 1; j > 0; j--) {
            generator[j] = generator[j-1] ^ gf_mult(generator[j], alpha_i);
        }
        generator[0] = gf_mult(generator[0], alpha_i);
    }
    
    printf("Generator polynomial created with degree %d.\n", PARITY);
    print_polynomial(generator, PARITY + 1, "Generator");
}

/**
 * Print polynomial coefficients in readable format
 */
void print_polynomial(uint8_t *poly, int length, const char *name) {
    int i;
    printf("%s polynomial: ", name);
    for (i = length - 1; i >= 0; i--) {
        if (poly[i] != 0) {
            printf("0x%02X", poly[i]);
            if (i > 0) printf("*x^%d + ", i);
        }
    }
    printf("\n");
}

/**
 * Encode a single Reed-Solomon block
 * Implements systematic encoding using polynomial division
 * Input: data[K] - information symbols
 * Output: codeword[N] - systematic codeword (data + parity)
 */
void rs_encode_block(uint8_t *data, uint8_t *codeword) {
    int i, j;
    uint8_t remainder[PARITY];
    uint8_t feedback;
    
    // Initialize remainder to zero
    memset(remainder, 0, PARITY);
    
    // Systematic encoding: copy data to first K positions
    memcpy(codeword, data, K);
    
    // Polynomial division to compute parity symbols
    // We're computing data(x) * x^PARITY mod g(x)
    for (i = 0; i < K; i++) {
        feedback = data[i] ^ remainder[PARITY - 1];
        
        // Shift remainder and add feedback term
        for (j = PARITY - 1; j > 0; j--) {
            remainder[j] = remainder[j - 1] ^ gf_mult(generator[j], feedback);
        }
        remainder[0] = gf_mult(generator[0], feedback);
    }
    
    // Copy parity symbols to codeword
    memcpy(codeword + K, remainder, PARITY);
}

/**
 * Encode entire file using Reed-Solomon coding
 * Reads input file, processes it in K-byte blocks, and writes encoded data
 */
int encode_file(const char *input_file, const char *output_file) {
    FILE *input_fp, *output_fp;
    uint8_t data_block[K];
    uint8_t codeword[N];
    size_t bytes_read;
    int block_count = 0;
    
    // Open input file
    input_fp = fopen(input_file, "rb");
    if (!input_fp) {
        printf("Error: Cannot open input file '%s'\n", input_file);
        return -1;
    }
    
    // Open output file
    output_fp = fopen(output_file, "wb");
    if (!output_fp) {
        printf("Error: Cannot create output file '%s'\n", output_file);
        fclose(input_fp);
        return -1;
    }
    
    printf("Encoding file '%s' to '%s'...\n", input_file, output_file);
    
    // Process file in K-byte blocks
    while ((bytes_read = fread(data_block, 1, K, input_fp)) > 0) {
        // Pad block with zeros if necessary
        if (bytes_read < K) {
            memset(data_block + bytes_read, 0, K - bytes_read);
            printf("Block %d: Padded %zu bytes with zeros\n", block_count + 1, K - bytes_read);
        }
        
        // Encode the block
        rs_encode_block(data_block, codeword);
        
        // Write encoded block to output file
        if (fwrite(codeword, 1, N, output_fp) != N) {
            printf("Error: Failed to write encoded block %d\n", block_count + 1);
            fclose(input_fp);
            fclose(output_fp);
            return -1;
        }
        
        block_count++;
        
        // Progress indicator
        if (block_count % 100 == 0) {
            printf("Processed %d blocks...\n", block_count);
        }
    }
    
    fclose(input_fp);
    fclose(output_fp);
    
    printf("Encoding completed successfully!\n");
    printf("Total blocks processed: %d\n", block_count);
    printf("Input file size: %d bytes\n", block_count * K - (K - (int)bytes_read));
    printf("Output file size: %d bytes\n", block_count * N);
    printf("Coding rate: %.3f\n", (float)K / N);
    printf("Redundancy: %d parity symbols per %d data symbols\n", PARITY, K);
    
    return 0;
}

/**
 * Main function
 */
int main(int argc, char *argv[]) {
    printf("Reed-Solomon Encoder (CCSDS 131.0-B-5 Standard)\n");
    printf("================================================\n");
    printf("Parameters: N=%d, K=%d, T=%d (can correct up to %d symbol errors)\n\n", 
           N, K, T, T);
    
    // Check command line arguments
    if (argc != 3) {
        printf("Usage: %s <input_file.txt> <output_file.txt>\n", argv[0]);
        printf("Example: %s data.txt encoded_data.txt\n", argv[0]);
        return 1;
    }
    
    // Initialize Galois Field
    printf("Initializing Galois Field GF(2^8)...\n");
    init_galois_field();
    
    // Generate Reed-Solomon generator polynomial
    printf("Generating Reed-Solomon generator polynomial...\n");
    generate_polynomial();
    
    // Encode the file
    printf("\nStarting file encoding...\n");
    if (encode_file(argv[1], argv[2]) != 0) {
        printf("Encoding failed!\n");
        return 1;
    }
    
    printf("\nReed-Solomon encoding completed successfully!\n");
    printf("Encoded file saved as: %s\n", argv[2]);
    
    return 0;
}