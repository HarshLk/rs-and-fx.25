#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    
    FILE *input = fopen(argv[1], "rb");
    FILE *output = fopen(argv[2], "wb");
    int flip_pos = atoi(argv[3]);
    
    int byte_count = 0;
    int c;
    
    while ((c = fgetc(input)) != EOF) {
        if (byte_count == flip_pos) {
            c ^= 1;  // Flip 1 bit
        }
        fputc(c, output);
        byte_count++;
    }
    
    fclose(input);
    fclose(output);
    return 0;
}