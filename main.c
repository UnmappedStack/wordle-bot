#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>

#define HERE(n) (printf("HERE (%d)\n", n))
#define WORD_LENGTH 5
#define NUM_GUESSES 6

#define RST "\x1b[0m"
#define RED "\x1b[0;31m"
#define GRN "\x1b[0;32m"
#define YLW "\x1b[0;33m"


typedef enum {
    INCORRECT, CORRECT, LOCATION,
} CharAccuracy;

typedef struct {
    CharAccuracy accs[WORD_LENGTH];
} WordAccuracy;

// Keep one of these for every character to store information known so far about where it could be etc
typedef struct {
    uint8_t allowed_spots;   // bitmap of indices of the word it is legal for it to be in (0=not allowed, 1=allowed)
    uint8_t confirmed_spots; // bitmap of indices where it is confirmed to be in that location for sure
    int min_in_word;         // number of times it is guaranteed to be in the word
    int max_in_word;
} CharInfo;

// returns true for success and vice versa, and fills *accuracy buffer
bool get_accuracy(char *word, WordAccuracy *buf) {
    printf(" > %s\n", word);
    static char char_accs[WORD_LENGTH + 1] = {0};
    for (int c = 0; read(STDIN_FILENO, &char_accs[c], 1) > 0 && c < WORD_LENGTH; c++) {
        if (char_accs[c] != '\n') continue;
        printf("%d characters required\n", WORD_LENGTH);
        return false;
    }
    WordAccuracy ret = {0};
    for (int i = 0; i < WORD_LENGTH; i++) {
        switch (char_accs[i]) {
            case '0':
                ret.accs[i] = INCORRECT;
                break;
            case '1':
                ret.accs[i] = LOCATION;
                break;
            case '2':
                ret.accs[i] = CORRECT;
                break;
            default:
                printf("invalid input (%d)\n", char_accs[i]);
                return false;
        }
    }
    *buf = ret;
    return true;
}

static char *get_words(size_t *words_len_buf) {
    FILE *f = fopen("words.txt", "r");
    if (!f) {
read_err:
        fprintf(stderr, "Failed to read from or open words.txt\n");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char*) malloc(len + 1);
    if (!fread(buf, len, 1, f)) {
        fclose(f);
        goto read_err;
    }
    fclose(f);
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n') buf[i] = 0;
    }
    *words_len_buf = len+1;
    return buf;
}

int count_char_in_str(char *s, char c) {
    int ret = 0;
    for (; *s; s++) {
        if (*s == c) ret++;
    }
    return ret;
}

static bool word_allowed(char *guessed, CharInfo *char_info) {
    // Check that no characters were placed in a spot they can't be
    for (size_t i = 0; i < WORD_LENGTH; i++) {
        size_t idx = guessed[i] - 'a';
        uint8_t legal = char_info[idx].allowed_spots & (1 << i);
        if (!legal) return false;
    }
    // Check:
    //  - that all chars were used minimum times they've been seen before and below the max (check 1)
    //  - that all confirmed characters were used in their correct position (check 2)
    for (size_t i = 0; i < 26; i++) {
        // check 1
        int times_found  = count_char_in_str(guessed, i + 'a');
        int times_needed = char_info[i].min_in_word;
        int times_max    = char_info[i].max_in_word;
        if (times_found < times_needed || (times_found > times_max && times_max > -1)) return false;
        // check 2
        for (int j = 0; j < WORD_LENGTH; j++) {
            bool required = char_info[i].confirmed_spots & (1 << j);
            bool is_there = guessed[j] == (i + 'a');
            if (required && !is_there) return false;
        }
    }
    return true;
}

// updates new words list
static char *guess(char *words, size_t words_len, CharInfo *char_info) {
    char *words_start = words;
    char *guessed;
    do {
        if (words >= &words_start[words_len]) {
            printf("ran out of words in dictionary to try\n");
            exit(-1);
        }
        guessed = words;
        words += strlen(words) + 1;
    } while (!word_allowed(guessed, char_info));
    // TODO:
    //  - Instead of using the first legal word, it should get all words and select the one with the most even split
    //  - Probably not necessary, but for efficiency, illegal words should be completely removed from the temp dict to not be checked
    //      in future guesses
    WordAccuracy accuracy = {0};
    if (!get_accuracy(guessed, &accuracy)) return NULL;
    bool correct = true;

    // this is stupid and kinda inefficient but whatever
    for (size_t c = 0; c < WORD_LENGTH; c++) {
        char_info[guessed[c]-'a'].max_in_word = count_char_in_str(guessed, guessed[c]);
        char_info[guessed[c]-'a'].min_in_word = 0;
    }

    for (size_t c = 0; c < WORD_LENGTH; c++) {
        size_t idx = guessed[c] - 'a';
        switch (accuracy.accs[c]) {
        case CORRECT:
            printf(GRN);
            char_info[idx].confirmed_spots |= 1 << c;
            char_info[guessed[c]-'a'].min_in_word++;
            break;
        case INCORRECT:
            printf(RED);
            correct = false;
            char_info[idx].allowed_spots &= ~(1 << c);
            char_info[idx].max_in_word--;
            break;
        case LOCATION:
            printf(YLW);
            correct = false;
            char_info[idx].allowed_spots &= ~(1 << c);
            char_info[guessed[c]-'a'].min_in_word++;
            break;
        }
        printf("%c" RST, guessed[c]);
    }


    for (size_t c = 0; c < WORD_LENGTH; c++) {
        if (char_info[guessed[c]-'a'].max_in_word == count_char_in_str(guessed, guessed[c]))
            char_info[guessed[c]-'a'].max_in_word = -1;
    }
    putchar('\n');
    if (correct) {
        printf("it did it, lesgo\n");
        return NULL;
    }
    return words;
}

int main(void) {
    char *words;
    size_t words_len;
    if ((words=get_words(&words_len)) == NULL) return -1;
    char *start_words = words;
    CharInfo char_info[26] = {0};
    for (size_t i = 0; i < 26; i++) {
        char_info[i].allowed_spots = 0b11111;
        char_info[i].max_in_word   = -1;
    }
    printf("For each word given, enter the accuracy:\n"
           " - Use 0 for incorrect\n"
           " - Use 1 for incorrect location\n"
           " - Use 2 for correct\n"
           "Please enter it as a continuous string of numbers for each letter, eg. \"12010\", then press enter\n");
    for (size_t i = 0; i < NUM_GUESSES; i++) {
        words = guess(words, words_len, char_info);
        if (words == NULL) break;
    }
    if (words != NULL) printf("it failed :(\n");
    free(start_words);
    return 0;
}
