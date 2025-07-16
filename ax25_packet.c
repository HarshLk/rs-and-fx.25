#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define AX25_FLAG 0x7E
#define AX_25_CONTROL 0x03
#define PID_NoL3 0xF0

#define MAX_FILE_SIZE 10240
#define MAX_PAYLOAD 256

typedef enum {
    BEACON_FRAME = 0,
    FRAME_DATA_HEADER,
    FRAME_DATA_FIRST,
    FRAME_DATA,
    FRAME_DATA_END,
    FRAME_MESSAGE,
} frame_type_t;

typedef struct {
    char source_call[8];
    char dest_call[8];
    uint8_t source;
    uint8_t dest;
} ax25_config_t;

void encode_address(const char* call, uint8_t ssid, uint8_t* out, int last) {
    int call_len = strlen(call);

    for (int i = 0; i < 6; i++) {
        if (i < call_len) {
            out[i] = call[i] << 1;
        } else {
            out[i] = ' ' << 1;
        }
    }
    out[6] = (ssid << 1) | (last ? 1 : 0);
}

uint16_t calculate_crc(uint8_t* data, int length) {
    uint16_t crc = 0xFFFF;

    for (int i = 0; i < length; i++) {
        crc ^= data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc ^ 0xFFFF;
}

int frame_header(frame_type_t type, uint16_t sequence, uint16_t total, uint8_t* buffer) {
    buffer[0] = (uint8_t) type;
    buffer[1] = (sequence >> 8) & 0xFF;
    buffer[2] = sequence & 0xFF;
    buffer[3] = (total >> 8) & 0xFF;
    buffer[4] = total & 0xFF;
    return 5;
}

int frame_gen(const ax25_config_t* config, frame_type_t type, uint16_t sequence, uint16_t total, const uint8_t* payload, int payload_len, uint8_t* frame_buffer) {
    int position = 0;
    
    // Opening flag
    frame_buffer[position++] = AX25_FLAG;
    encode_address(config->dest_call, config->dest, &frame_buffer[position], 0);
    position += 7;
    encode_address(config->source_call, config->source, &frame_buffer[position], 1);
    position += 7;

    frame_buffer[position++] = AX_25_CONTROL;
    frame_buffer[position++] = PID_NoL3;

    if (type != FRAME_MESSAGE) {
        position += frame_header(type, sequence, total, &frame_buffer[position]);
    }

    if (payload && payload_len > 0) {
        memcpy(&frame_buffer[position], payload, payload_len);
        position += payload_len;
    }
    
    // Frame Check Sequence (FCS)
    uint16_t fcs = calculate_crc(&frame_buffer[1], position - 1);
    frame_buffer[position++] = fcs & 0xFF;
    frame_buffer[position++] = (fcs >> 8) & 0xFF;

    // Closing flag
    frame_buffer[position++] = AX25_FLAG;

    return position;
}

void write_frame_hex(FILE* output, const uint8_t* frame, int length, int packet_num) {
    fprintf(output, "Packet %d (%d bytes):\n", packet_num, length);
    for (int i = 0; i < length; i++) {
        fprintf(output, "%02X ", frame[i]);
        if ((i + 1) % 16 == 0) {
            fprintf(output, "\n");
        }
    }
    if (length % 16 != 0) {
        fprintf(output, "\n");
    }
    fprintf(output, "\n");
}

int create_beacon_frame(const ax25_config_t* config, const char* message, uint8_t* frame_buffer) {
    return frame_gen(config, BEACON_FRAME, 0, 1, (uint8_t*)message, strlen(message), frame_buffer);
}

int create_message_frame(const ax25_config_t* config, const char* message, uint8_t* frame_buffer) {
    return frame_gen(config, FRAME_MESSAGE, 0, 1, (uint8_t*)message, strlen(message), frame_buffer);
}

int packetization(const ax25_config_t* config, const uint8_t* data, int data_length, FILE* output) {
    int total_packets = (data_length + MAX_PAYLOAD - 1) / MAX_PAYLOAD;
    printf("Packetizing %d bytes into %d frames\n", data_length, total_packets);
    
    for (int packet = 0; packet < total_packets; packet++) {
        uint8_t frame_buffer[512];
        int data_offset = packet * MAX_PAYLOAD;
        int chunk_size = (data_offset + MAX_PAYLOAD > data_length) ? 
                         (data_length - data_offset) : MAX_PAYLOAD;
        
        // Determine frame type based on position
        frame_type_t frame_type;
        if (total_packets == 1) {
            frame_type = FRAME_DATA_HEADER;
        } else if (packet == 0) {
            frame_type = FRAME_DATA_FIRST;
        } else if (packet == total_packets - 1) {
            frame_type = FRAME_DATA_END;
        } else {
            frame_type = FRAME_DATA;
        }
        
        // Generate frame
        int frame_length = frame_gen(config, frame_type, packet, total_packets,
                                   data + data_offset, chunk_size, frame_buffer);
        
        write_frame_hex(output, frame_buffer, frame_length, packet);
    }
    
    return total_packets;
}

int main() {
    ax25_config_t config = {
        .source_call = "N0CALL",
        .dest_call = "CQ",
        .source = 0,
        .dest = 0,
    };

    printf("Generating AX.25 Frames\n");

    FILE* input_file = fopen("input.txt", "r");
    if (!input_file) {
        printf("Error: Cannot open input.txt\n");
        return 1;
    }
    
    // Read file contents
    uint8_t data_buffer[MAX_FILE_SIZE];
    int data_length = fread(data_buffer, 1, MAX_FILE_SIZE, input_file);
    fclose(input_file);
    
    if (data_length <= 0) {
        printf("Error: No data read from input.txt\n");
        return 1;
    }
    
    printf("Read %d bytes from input.txt\n", data_length);
    
    // Open output file
    FILE* output_file = fopen("packets.txt", "w");
    if (!output_file) {
        printf("Error: Cannot create packets.txt\n");
        return 1;
    }

    int packets = packetization(&config, data_buffer, data_length, output_file);
    fclose(output_file);
    
    if (packets > 0) {
        printf("Successfully created %d packet frames\n", packets);
        printf("Results written to packets.txt\n");
    } else {
        printf("Error occurred during packetization\n");
        return 1;
    }
    
    return 0;
}