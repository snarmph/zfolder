# zfolder v0.1
Single header library to compress file/libraries using zstandard

### Dependencies
- zstd [ github.com/facebook/zstd ]
- (only on windows) dirent [ github.com/tronkko/dirent ]

### Example usage
Compression
```c
// remember that you need <zstd.h> and <dirent.h> in your include path
#define Z_FOLDER_IMPLEMENTATION
#include "zfolder.h"

int main() {
    zfolder comp;
    zf_init(&comp);
    zf_add_file(&comp, "example.txt");
    zf_add_dir(&comp, "zstd", true); // recursive: true
    zf_compress(&comp, "output.zst", ZMAX_COMP);
    zf_destroy(&comp);
}
```

Decompression
```c
#define Z_FOLDER_IMPLEMENTATION
#include "zfolder.h"

int main() {
    zfolder dec;
    zf_init(&dec);
    // decompress zst file into the structure
    zf_decompress(&dec, "output.zst");
    // decompress structure to an output directory
    zf_decompress_todir(&dec, "output_dir", true); // overwrite: true
    zf_destroy(&dec);
}
```