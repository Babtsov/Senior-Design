#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define FILE_NAME "20khz.csv"
#define TOLERANCE 6

FILE * input_file;
bool parsed_valid_bool(char * str) {
    return strcmp(str, "0\r\n") == 0   // Windows end line
        || strcmp(str, "1\r\n") == 0   // Windows end line
        || strcmp(str, "0\n") == 0     // Unix end line
        || strcmp(str, "1\n") == 0;    // Unix end line
}

int get_next_sample(void) {
    size_t nbytes = 40;
    char * my_string = (char *) calloc(nbytes + 1, sizeof(char));
    char * data_ptr;
    while (true) {
        ssize_t bytes_read = getline(&my_string, &nbytes, input_file);
        if (bytes_read == -1) {
            printf("\nEnd of file\n");
            exit(0);
        }
        data_ptr = strstr(my_string, ",");
        if (data_ptr && parsed_valid_bool(data_ptr + 1)) break;
    }
    int c = *(data_ptr + 1) - '0';
    free(my_string);
    assert(c == 0 || c == 1);
    return c;
}
int current;
int detect_change(void) {
    int count, sample;
    for (count = 1; (sample = get_next_sample()) == current; count++);  // Keep counting until there is a change detected
    current = sample;                                                   // modify current to reflect this change
    return count;                                                       // return the # of consecutive logic values
}


int get_first_manchester(void) {
    current = get_next_sample();
    while (true) {
        int count = detect_change();
        if (count > TOLERANCE) break;
    }
    return current;
}

int get_next_manchester(void) {
    int count = detect_change();
    if ( count <= TOLERANCE) {
        int next_count = detect_change();
        assert(next_count <= TOLERANCE);
        return current;
    } else {
        return current ^ 1; // return the opposite of "current"
    }
}
char formatHex(int i) {
    if ( 0 <= i && i <= 9){
        return i + '0';
    } else if (10 <= i && i <= 15) {
        return (i - 10) + 'A';
    } else {
        assert(false);
    }
}

struct {
    int buff[10];
    int current_sample;
} RFID;

void decodeRFID(void) {
    
}

int main(int argc, const char * argv[]) {
    input_file = fopen(FILE_NAME, "r");
    if (!input_file) {
        printf("Invalid file\n");
        return 1;
    }

    int decoded_bit = get_first_manchester();
    int one_count = decoded_bit;
    while (one_count < 9) { // wait until we get 9 consecutive 1's
        one_count = (get_next_manchester() == 1) ? one_count + 1 : 0;
    }
    int rfid[10] = {0};
    int col_parity[4] = {0};
    for (int i = 0; i < 10; i++) { // scan all 10 rfid characters
        int rfid_char = 0, row_parity = 0;
        for (int j = 3; j >= 0; j--) { //build 4-bit hex number bit by bit
            decoded_bit = get_next_manchester();
            rfid_char += decoded_bit << j;
            row_parity += decoded_bit;
            col_parity[j] += decoded_bit;
        }
        row_parity += get_next_manchester();
        assert((row_parity & 1) == 0); // assert row parity is even
        rfid[i] = rfid_char;
    }
    for (int i = 3; i >= 0; i--) { // now scan all the column parities
        col_parity[i] += get_next_manchester();
        assert((col_parity[i] & 1) == 0); // assert they are all even
    }
    int stop_bit = get_next_manchester();
    assert(stop_bit == 0);
    
    for (int i = 0; i < 10; i++) {
        printf("%c",formatHex(rfid[i]));
    }
    printf("\n");
    return 0;
}
