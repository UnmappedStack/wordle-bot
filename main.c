#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stddef.h>

static const char *correct = "pasta"; // hardcoded for now

#define HERE(n) (printf("HERE (%d)\n", n))
#define WORD_LENGTH 5
#define NUM_GUESSES 6

#define RST "\e[0m"
#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YLW "\e[0;33m"


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
    int num_in_word;         // number of times it is guaranteed to be in the word
    int max_in_word;
} CharInfo;

// thanks stackoverflow, im still of the strong belief this should be in the libc
char* to_bin_str(int n) {
  int num_bits = sizeof(int) * 8;
  char *string = malloc(num_bits + 1);
  if (!string) {
    return NULL;
  }
  for (int i = num_bits - 1; i >= 0; i--) {
    string[i] = (n & 1) + '0';
    n >>= 1;
  }
  string[num_bits] = '\0';
  return string;
}

// returns a buffer of max strlen(s) elements which must be freed, terminated by -1
int *find_chars_in_str(const char *s, char c) {
    int len = strlen(s);
    int *ret = (int*) malloc((strlen(s)+1) * sizeof(int));
    int at = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] != c) continue;
        ret[at++] = i;
    }
    ret[at] = -1;
    return ret;
}

bool loc_in_locs(int *locs, int idx) {
    for (int loc = 0; locs[loc] != -1; loc++) {
        if (locs[loc] == idx) return true;
    }
    return false;
}

WordAccuracy get_accuracy(char *word) {
    printf(" > %s\n", word);
    char char_accs[WORD_LENGTH] = {0};
    scanf("%5s", char_accs);
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
                exit(-1);
                break;
        }
    }
    return ret;
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
        int times_needed = char_info[i].num_in_word;
        int times_max    = char_info[i].max_in_word;
        if (times_found < times_needed || (times_found > times_max && times_max > -1)) return false;
        // check 2 (this is "unnecessarily" very nested but its for readability)
        for (int j = 0; j < WORD_LENGTH; j++) { // for each bitmap val...
            // if it's required to be in a specific spot...
            if (char_info[i].confirmed_spots & (1 << j)) {
                // make sure its actually in that spot.
                if (guessed[j] != (i + 'a')) return false;
            }
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

    WordAccuracy accuracy = get_accuracy(guessed);
    bool correct = true;

    // this is stupid and kinda inefficient but whatever
    for (size_t c = 0; c < WORD_LENGTH; c++) {
        char_info[guessed[c]-'a'].max_in_word = count_char_in_str(guessed, guessed[c]);
        char_info[guessed[c]-'a'].num_in_word = 0;
    }

    for (size_t c = 0; c < WORD_LENGTH; c++) {
        size_t idx = guessed[c] - 'a';
        if (accuracy.accs[c] == CORRECT) {
            printf(GRN);
            char_info[idx].confirmed_spots |= 1 << c;
            char_info[guessed[c]-'a'].num_in_word++;
        } else if (accuracy.accs[c] == INCORRECT) {
            printf(RED);
            correct = false;
            char_info[idx].allowed_spots &= ~(1 << c);
            char_info[idx].max_in_word--;
        } else { // LOCATION
            printf(YLW);
            correct = false;
            char_info[idx].allowed_spots &= ~(1 << c);
            char_info[guessed[c]-'a'].num_in_word++;
        }
        printf("%c" RST, guessed[c]);
    }


    for (size_t c = 0; c < WORD_LENGTH; c++) {
        if (char_info[guessed[c]-'a'].max_in_word == count_char_in_str(guessed, guessed[c]))
            char_info[guessed[c]-'a'].max_in_word = -1;
    }

    puts("");
    if (correct) return NULL;
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
        if (words == NULL) {
            printf("It did it lesgo\n");
            break;
        }
    }
    if (words != NULL)
        printf("it failed :(\n");
    free(start_words);
    return 0;
}
