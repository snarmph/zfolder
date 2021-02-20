# zfolder 0.1
Single header library to compress file/libraries using zstandard

### Example usage
Compression
```c
// remember that you need <zstd.h> in your include path
#define Z_FOLDER_IMPLEMENTATION
#include "zfolder.h"

int main() {
    zfolder comp;
    zf_init(&comp);
    zf_add_dir(&comp, "zstd", true);
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
    zf_decompress(&dec, "output.zst");
    zf_decompress_todir(&dec, "output_dir", true);
    zf_destroy(&dec);
}
```