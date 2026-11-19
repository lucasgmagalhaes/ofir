/* The C side of textstats: reads the files named on the command line and
   prints their statistics through the library built from textstats.of. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "textstats.h"

static char *read_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc((size_t)*len + 1);
    if (data && fread(data, 1, (size_t)*len, f) != (size_t)*len) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

int main(int argc, char **argv) {
    int status = 0;
    for (int i = 1; i < argc; i++) {
        long len = 0;
        char *data = read_file(argv[i], &len);
        if (!data) {
            fprintf(stderr, "textstats: cannot read %s\n", argv[i]);
            status = 1;
            continue;
        }
        const char *name = strrchr(argv[i], '/');
        name = name ? name + 1 : argv[i];
        struct Stats st;
        textstats_count((const uint8_t *)data, len, &st);
        textstats_report((const uint8_t *)name, (int64_t)strlen(name), &st);
        ofir_stdout_flush();
        printf("  longest line %" PRId64 ", crc32 %08" PRIx32 ", adler32 %08" PRIx32 "\n",
               st.longest, st.crc32, st.adler32);
        printf("  \"the\" occurs %" PRId64 " times\n",
               textstats_occurrences((const uint8_t *)data, len, (const uint8_t *)"the", 3));
        fflush(stdout);
        free(data);
    }
    return status;
}
