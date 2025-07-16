#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "fec.h"  

#define FX25_FLAG 0x7E
#define CORRELATION_TAG_SIZE 8
#define MAX_FRAME_SIZE 512

static const uint8_t CORR_TAG[8] = {
    0xCC, 0x8F, 0x8A, 0xE4, 0x85, 0xE2, 0x98, 0x01   
};

#define N 255
#define K 223 
#define ROOTS 32

typedef struct {
    void* rs_handle; // libfec Reed-Solomon handle
} fx25_config_t;

fx25_config_t* fx25_init() {
    fx25_config_t* config = malloc(sizeof(fx25_config_t));
    if (!config) return NULL;

    config->rs_handle = init_rs_char(8, 0x187, 112, 11, ROOTS, 0);
    if (!config->rs_handle) {
        free(config);
        return NULL;
    }

    return config;
}

void fx25_cleanup(fx25_config_t* config) {
    if (config) {
        if (config->rs_handle) {
            free_rs_char(config->rs_handle);
        }
        free(config);
    }
}

int parse_hex(const char* line, uint8_t* output, int max_len) {
    int byte_count = 0;  
    char* ptr = (char*)line;  

    while (*ptr && byte_count < max_len) {
        if (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r') {
            ptr++;
            continue;
        }

        if (!((*ptr >= '0' && *ptr <= '9') || 
              (*ptr >= 'A' && *ptr <= 'F') || 
              (*ptr >= 'a' && *ptr <= 'f'))) {
            ptr++;
            continue;
        }
        
        // Parse hex byte
        char hex_byte[3] = {0};
        hex_byte[0] = *ptr++;
        if (*ptr && ((*ptr >= '0' && *ptr <= '9') || 
                    (*ptr >= 'A' && *ptr <= 'F') || 
                    (*ptr >= 'a' && *ptr <= 'f'))) {
            hex_byte[1] = *ptr++;
        }
        
        output[byte_count++] = (uint8_t)strtol(hex_byte, NULL, 16);
    }

    return byte_count;
}

int read_ax25(const char* filename, uint8_t packets[][MAX_FRAME_SIZE], int* packet_lengths, int max_packets) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Cannot open %s\n", filename);
        return 0;
    }
    
    char line[1024];
    int packet_count = 0;
    uint8_t temp_buffer[MAX_FRAME_SIZE];
    int temp_len = 0;
    int in_packet = 0;

    while (fgets(line, sizeof(line), file) && packet_count < max_packets) {
        
        if (strstr(line, "Packet") && strstr(line, "bytes")) {
            // If we were building a packet, save it
            if (in_packet && temp_len > 0) {
                memcpy(packets[packet_count], temp_buffer, temp_len);
                packet_lengths[packet_count] = temp_len;
                packet_count++;
                temp_len = 0;
            }
            in_packet = 1;
            continue;
        }
        
        if (in_packet) {
            int bytes_parsed = parse_hex(line, temp_buffer + temp_len, 
                                       MAX_FRAME_SIZE - temp_len);
            temp_len += bytes_parsed;
            
            // Check for empty line (end of packet)
            if (strlen(line) <= 1 && temp_len > 0) {
                memcpy(packets[packet_count], temp_buffer, temp_len);
                packet_lengths[packet_count] = temp_len;
                packet_count++;
                temp_len = 0;
                in_packet = 0;
            }
        }
    }
    
    // Handle last packet if file doesn't end with empty line
    if (in_packet && temp_len > 0) {
        memcpy(packets[packet_count], temp_buffer, temp_len);
        packet_lengths[packet_count] = temp_len;
        packet_count++;
    }

    fclose(file);
    return packet_count;
}

int generate_fx25(fx25_config_t* config, const uint8_t* ax25_packet, int ax25_len, uint8_t* fx25_frame) {
    if (ax25_len > K) {
        printf("Error: AX.25 packet too large (%d bytes, max %d)\n", ax25_len, K);
        return 0;
    }
    
    int position = 0;

    // Acorr tag
    memcpy(fx25_frame + position, CORR_TAG, CORRELATION_TAG_SIZE);
    position += CORRELATION_TAG_SIZE;

    // rs data block
    uint8_t rs_block[N];
    memset(rs_block, 0, N); // Zero padding

    // Copy AX.25 packet to RS block
    memcpy(rs_block, ax25_packet, ax25_len);

    // add rs parity bits to the data
    encode_rs_char(config->rs_handle, rs_block, rs_block + K);

    // copy to output
    memcpy(fx25_frame + position, rs_block, N);
    position += N;

    return position;
}

void write_fx25_hex(FILE* output, const uint8_t* frame, int length, int packet_num) {
    fprintf(output, "FX.25 Packet %d (%d bytes):\n", packet_num, length);

    fprintf(output, "Correlation Tag: ");
    for (int i = 0; i < CORRELATION_TAG_SIZE; i++) {
        fprintf(output, "%02X ", frame[i]);
    }
    fprintf(output, "\n");
    
    // RS codeword 
    fprintf(output, "RS Codeword:\n");
    for (int i = CORRELATION_TAG_SIZE; i < length; i++) {
        fprintf(output, "%02X ", frame[i]);
        if ((i - CORRELATION_TAG_SIZE + 1) % 16 == 0) {
            fprintf(output, "\n");
        }
    }
    if ((length - CORRELATION_TAG_SIZE) % 16 != 0) {
        fprintf(output, "\n");
    }
    fprintf(output, "\n");
}

int main(int argc, char* argv[]) {
    const char* input_file = "packets.txt";
    const char* output_file = "fx25_packets.txt";
            
    fx25_config_t* config = fx25_init();
    if (!config) {
        printf("Error: Failed to initialize FX.25 configuration\n");
        return 1;
    }
    
    uint8_t ax25_packets[100][MAX_FRAME_SIZE];
    int packet_lengths[100];
    int packet_count = read_ax25(input_file, ax25_packets, packet_lengths, 100);

    if (packet_count <= 0) {
        printf("Error: No AX.25 packets found in %s\n", input_file);
        fx25_cleanup(config);
        return 1;
    }

    printf("Read %d AX.25 packets\n", packet_count);
    
    if (packet_count > 0) {
        printf("First packet length: %d bytes\n", packet_lengths[0]);
        printf("First few bytes: ");
        for (int i = 0; i < 8 && i < packet_lengths[0]; i++) {
            printf("%02X ", ax25_packets[0][i]);
        }
        printf("\n");
    }
    
    FILE* output = fopen(output_file, "w");
    if (!output) {
        printf("Error: Cannot create %s\n", output_file);
        fx25_cleanup(config);
        return 1;
    }
    
    int fx25_count = 0;
    for (int i = 0; i < packet_count; i++) {
        uint8_t fx25_frame[512];
        
        int fx25_len = generate_fx25(config, ax25_packets[i], packet_lengths[i], fx25_frame);
        
        if (fx25_len > 0) {
            write_fx25_hex(output, fx25_frame, fx25_len, fx25_count);
            fx25_count++;
        } else {
            printf("Warning: Failed to encode packet %d (length: %d bytes)\n", i, packet_lengths[i]);
        }
    }
    
    fclose(output);
    fx25_cleanup(config);
    
    printf("Successfully created %d FX.25 frames\n", fx25_count);
    printf("Results written to %s\n", output_file);
    
    return 0;
}