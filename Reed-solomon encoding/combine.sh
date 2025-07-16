
gcc rs_encoding_binary.c 
./a.out input.txt output.txt

# gcc bit_flip_error.c 
# ./a.out encoded_output.txt corrupted_output.txt 

gcc rs_decoding_binary.c 
./a.out output.txt final.txt
