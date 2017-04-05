#include <cstdint>
#include <cstdio>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: bin2hex <input_file> <output_array_name>\n");
        return - 1;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        printf("Could not open file for reading: %s\n", argv[1]);
        return -1;
    }

    const int line_length = 16;
    long long size = 0;

    printf("unsigned char %s[] = {\n\t", argv[2]);

    int byte = fgetc(f);
    while (byte != EOF) {
        printf("0x%.2X", byte);

        byte = fgetc(f);
        if (byte != EOF) printf(", ");

        size++;
        if (size % line_length == 0) printf("\n\t");
    }

    printf("\n};\n");
    printf("long long %s_size = %lld;", argv[2], size);

    if (!feof(f)) {
        printf("Could not read entire file: %s", argv[1]);
    }

    fclose(f);
    return 0;
}
