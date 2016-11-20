#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

FILE * fp;
int get_next_sample(void) {
    size_t nbytes = 1;
    char * my_string = (char *) malloc (nbytes + 1);
    ssize_t bytes_read = getline (&my_string, &nbytes, fp);
    if (bytes_read == -1) {
        printf("\nEnd of file\n");
        exit(0);
    } else if (bytes_read != 2) {
        printf("invalid file\n");
        exit(1);
    }
    int c = my_string[0] - '0';
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

#define TOLERANCE 6
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

int main(int argc, const char * argv[]) {
    fp = fopen("20khz.dat", "r");
    if (!fp) {
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
