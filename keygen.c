#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <keylength>\n", argv[0]);
        exit(1);
    }

    int keylength = atoi(argv[1]);
    if (keylength <= 0) {
        fprintf(stderr, "Key length must be a positive integer\n");
        exit(1);
    }

    srand(time(NULL));

    for (int i = 0; i < keylength; ++i) {
        // Generate random character
        char random_char = 'A' + (rand() % 27);
        if (random_char == '[') // '[' corresponds to '\n' in ASCII
            random_char = ' '; // Replace '[' with ' ' (space character)
        printf("%c", random_char);
    }
    printf("\n");

    return 0;
}
