#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

FILE * fp;
char get_next_logic(void) {
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
    char c = my_string[0];
    free(my_string);
    return c;
}

int detect_change(char * current) {
    int count = 1;
    while(true) {
        char c = get_next_logic();
        if (c != *current) {
//            printf(" Count:%d\n", count);
//            printf("%c",c);
            *current = c;
            break;
        }
//        printf("%c",c);
        count++;
    }
    return count;
}

#define TOLERANCE 6
int main(int argc, const char * argv[]) {
    char buff[1000] = {0};
    fp = fopen("20khz.dat", "r");
    if (!fp) {
        printf("Invalid file\n");
        return 1;
    }
    char current = get_next_logic();
    printf("%c",current);
    while (true) {
        int count = detect_change(&current);
        if (count > TOLERANCE) break;
    }
    buff[0] = current;
    for (int i = 1; i < 1000; i++) {
        int count = detect_change(&current);
        if ( count <= TOLERANCE) {
            int next_count = detect_change(&current);
            assert(next_count <= TOLERANCE);
            buff[i] = current;
        } else {
            if (current == '1') {
                buff[i] = '0';
            } else if ( current == '0') {
                buff[i] = '1';
            } else {
                assert(0);
            }
        }
        printf("%c",buff[i]);
        
    }
    return 0;
}
