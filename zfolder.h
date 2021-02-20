/*  zfolder.h - v0.1 - Alessandro Bason 2021

    This is a small single header file library to easily compress and 
    decompress folders using zstandard

    To use this library, do this in one C or C++ file:
        #define Z_FOLDER_IMPLEMENTATION
        #include "zfolder.h"
    
  COMPILE TIME OPTIONS:

    #define MAX_FILES [n]
        maximum number of files in a zst file (default: 2048)

    #define MAX_PATH_LEN [n]
        maximum path length (not more than 255) (default: 128)

  USAGE:
    
    // == COMPRESSION ==========================
    zfolder dir;
    zf_init(&dir);
    zf_add_file(&dir, "hello_world.txt");
    zf_add_dir(&dir, "nested/folder_name", true); // recursive: true
    zf_compress(&dir, "file.zst", ZMAX_COMP);
    zf_destroy(&dir);

    // == DECOMPRESSION ========================
    zfolder dir;
    zf_init(&dir);
    zf_decompress(&dir, "file.zst");
    zf_decompress_todir(&dec, "output_dir", true); // overwrite: true
    zf_destroy(&dec);

  LICENSE:
    MIT License

    Copyright (c) 2021 snarmph

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#ifndef INCLUDE_Z_FOLDER_H
#define INCLUDE_Z_FOLDER_H

#include <stdint.h>
#include <stdbool.h>

#ifndef Z_MAX_FILES
#define Z_MAX_FILES 2048
#endif

#ifndef Z_MAX_PATH_LEN
#define Z_MAX_PATH_LEN 128
#endif

/*
FORMAT:
    nfiles (4 bytes) -> number of files encoded
    files header: (there are nfiles file headers)
        plen (1 bytes) -> length of path string
        flen (4 bytes) -> length of this specific file
        path (plen bytes) -> pathname (string DOES NOT END WITH NULL) 
    dlen (4 bytes) -> length of unencoded data
    data -> just a stream of data until the end of the file
*/

enum {
    ZMIN_COMP = -5,
    ZDECENT_COMP = 8,
    ZGOOD_ENOUGH_COMP = 18,
    ZMAX_COMP = 20
} zcompression;

typedef struct {
    char     path[Z_MAX_PATH_LEN];
    uint8_t  plen; // path length
    uint32_t flen; // file length
} zfile;

typedef struct {
    zfile    files[Z_MAX_FILES];
    uint32_t nfiles; // number of files
    uint8_t *data;
    uint32_t dlen;   // data length
} zfolder;

// initialize zfolder object
void zf_init(zfolder *dir);
// add a file to the zfolder
void zf_add_file(zfolder *dir, char path[Z_MAX_PATH_LEN]);
// add an entire directory to the zfolder
void zf_add_dir(zfolder *dir, const char *path, bool recursive);
// compress the zfolder
void zf_compress(zfolder *dir, const char *path, int compression_level);
// decompress the file
void zf_decompress(zfolder *dir, const char *fname);
// decompress the zfolder to the (output) directory
void zf_decompress_todir(zfolder *dir, const char *output, bool overwrite);
// get file, returns the data
uint8_t *zf_get_file(zfolder *dir, uint32_t index);
// destroy the zfolder object
void zf_destroy(zfolder *dir);

#endif // INCLUDE_Z_FOLDER_H

/* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
  +                                        +
  -             IMPLEMENTATION             -
  +                                        +
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- */

#ifdef Z_FOLDER_IMPLEMENTATION

#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <sys/stat.h>

#include <zstd.h> // zstandard compression

// == DEFINES ===================================================

#define crash(msg) do { fprintf(stderr, "[CRASH] " msg "\n"); exit(1); } while(0)
#define crashfmt(msg, ...) do { fprintf(stderr, "[CRASH] " msg "\n", __VA_ARGS__); exit(1); } while(0);

#define ncopy_to_buf(buf, data, n) do { memcpy((buf), &(data), n); (buf) += (n); } while(0);
#define copy_to_buf(buf, data) ncopy_to_buf(buf, (data), sizeof(data))

#define nread_from_buf(buf, data, n) do { memcpy(&(data), (buf), (n)); (buf) += (n); } while(0);
#define read_from_buf(buf, data) nread_from_buf(buf, data, sizeof(data))

#define next_path()             \
            size_t sz = (path_tmp-1) - path;  \
            char cur_path[sz+1];         \
            strncpy(cur_path, path, sz); \
            cur_path[sz] = '\0';

// == STATIC FUNCTIONS ==========================================

static uint32_t _zf_read_file(const char *path, zfolder *dir);
static uint32_t _read_whole_file(const char *fname, uint8_t **data);
static void _write_whole_file(const char *path, uint8_t *data, size_t dlen);
static bool _strcmp(const char *s1, const char *s2);
static void _concat_path(char *dst, const char *dir, const char *path, size_t path_length);
static uint8_t _split_path(const char **path);
static void _create_necessary_dirs(const char *path);

// == FUNCTIONS =================================================

void zf_init(zfolder *dir) {
    memset(dir, 0, sizeof(zfolder));
}

void zf_add_file(zfolder *dir, char path[Z_MAX_PATH_LEN]) {
    zfile *current = &dir->files[dir->nfiles++];
    strncpy(current->path, path, Z_MAX_PATH_LEN);
    current->plen = strnlen(current->path, Z_MAX_PATH_LEN);
    current->flen = _zf_read_file(path, dir);
}

void zf_add_dir(zfolder *_dir, const char *path, bool recursive) {
    DIR *d = opendir(path);
    if(!d)
        crashfmt("couldn't open directory -> %s", path);

    size_t plen = strlen(path); // path lenght
    struct dirent *dir;
    while((dir = readdir(d)) != NULL) {
        if(dir->d_type == DT_DIR && recursive) {
            // "." is the current directory, ".." is the previous directory
            if(_strcmp(dir->d_name, ".") || _strcmp(dir->d_name, "..")) 
                continue;

            // get final path lenght (path/dir)
            size_t dlen = strlen(dir->d_name) + plen + 1;
            if(dlen > Z_MAX_PATH_LEN)
                crashfmt("path is too long -> %s/%s", path, dir->d_name);

            char temp_fname[dlen+1];
            _concat_path(temp_fname, dir->d_name, path, plen);
            zf_add_dir(_dir, temp_fname, true);
        }
        else if(dir->d_type == DT_REG) {
            // get final path lenght (path/dir)
            size_t dlen = strlen(dir->d_name) + plen + 1;
            if(dlen > Z_MAX_PATH_LEN)
                crashfmt("path is too long -> %s/%s", path, dir->d_name);
            
            char temp_fname[dlen+1];
            _concat_path(temp_fname, dir->d_name, path, plen);
            zf_add_file(_dir, temp_fname);
        }
    }
    closedir(d);
}

void zf_compress(zfolder *dir, const char *path, int compression_level) {
    // maximum possible length (doesnt consider the length of every path
    // it justs assumes the maximum possible length)
    size_t max_len = 0;
    max_len += sizeof(dir->nfiles);
    max_len += dir->nfiles * sizeof(zfile);
    max_len += sizeof(dir->dlen);
    max_len += dir->dlen;

    uint8_t *to_compress = malloc(max_len);
    uint8_t *cur = to_compress;

    printf("number of files: %u\n", dir->nfiles);
    
    copy_to_buf(cur, dir->nfiles);
    for (uint32_t i = 0; i < dir->nfiles; ++i) {
        copy_to_buf(cur, dir->files[i].plen);
        copy_to_buf(cur, dir->files[i].flen);
        ncopy_to_buf(cur, dir->files[i].path, dir->files[i].plen);
    }
    copy_to_buf(cur, dir->dlen);
    ncopy_to_buf(cur, *dir->data, dir->dlen);

    size_t src_len = cur - (uint8_t*)to_compress;
    size_t dst_len = ZSTD_compressBound(src_len);
    uint8_t *dst = malloc(dst_len);

    size_t res = ZSTD_compress(dst, dst_len, to_compress, src_len, compression_level);
    if(ZSTD_isError(res))
        crash("couldn't compress data");
    
    _write_whole_file(path, dst, res);

    free(to_compress);
    free(dst);

    size_t srckb = src_len / 1024;
    size_t dstkb = res / 1024;

    printf("original size:   %zu b -- %zu kb\n", src_len, srckb);
    printf("compressed size: %zu b -- %zu kb\n", res, dstkb);
}

void zf_decompress(zfolder *dir, const char *fname) {
    uint8_t *compressed;
    // compressed lenght
    uint32_t clen = _read_whole_file(fname, &compressed);
    
    size_t res = ZSTD_getFrameContentSize(compressed, clen);
    
    if(res == ZSTD_CONTENTSIZE_UNKNOWN || res == ZSTD_CONTENTSIZE_ERROR) 
        crash("couldn't retrieve size from file");
    
    size_t dst_len = res;
    uint8_t *dst = malloc(dst_len);
    res = ZSTD_decompress(dst, dst_len, compressed, clen);
    free(compressed);
    if(ZSTD_isError(res))
        crash("couldn't decompress data");

    uint8_t *buf = dst;

    read_from_buf(buf, dir->nfiles);
    for (uint32_t i = 0; i < dir->nfiles; ++i) {
        read_from_buf(buf, dir->files[i].plen);
        read_from_buf(buf, dir->files[i].flen);
        nread_from_buf(buf, dir->files[i].path, dir->files[i].plen);
    }
    read_from_buf(buf, dir->dlen);
    dir->data = malloc(dir->dlen);
    nread_from_buf(buf, *dir->data, dir->dlen);
    
    free(dst);
}

void zf_decompress_todir(zfolder *dir, const char *output, bool overwrite) {
    struct stat st = {0};
    if(stat(output, &st) != -1 && !overwrite)
        crashfmt("folder %s already exists", output);
    mkdir(output, 0777);

    size_t pathlen = strlen(output);

    for (uint32_t i = 0; i < dir->nfiles; ++i) {
        uint8_t *data = zf_get_file(dir, i);
        size_t len = dir->files[i].flen;

        // make sure that the path finishes with \0
        dir->files[i].path[dir->files[i].plen] = '\0';
        
        size_t path_len = dir->files[i].plen + pathlen + 1;
        char temp_path[path_len];
        memset(temp_path, '\0', path_len);
        _concat_path(temp_path, dir->files[i].path, output, pathlen);
        
        _create_necessary_dirs(temp_path);
        
        _write_whole_file(temp_path, data, len);
    }
}

uint8_t *zf_get_file(zfolder *dir, uint32_t index) {
    uint32_t offset = 0;
    for (uint32_t i = 0; i < index; ++i)
        offset += dir->files[i].flen;
    
    return dir->data + offset;
}


void zf_destroy(zfolder *dir) {
    free(dir->data);
}

// == IMPLEMENTATION ============================================

static uint32_t _zf_read_file(const char *path, zfolder *dir) {
    FILE *f = fopen(path, "rb");
    if(!f) 
        crashfmt("couldn't open file -> %s", path);
    // get file length
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if(len < 0)
        crash("lenght of file is negative");
    fseek(f, 0, SEEK_SET);

    // allocate enough space to read the new data
    dir->data = realloc(dir->data, dir->dlen + len);
    // read data at the end of the buffer
    fread((dir->data + dir->dlen), len, 1, f);    
    dir->dlen += len;

    fclose(f);
    return len;
}

static uint32_t _read_whole_file(const char *fname, uint8_t **data) {
    FILE *f = fopen(fname, "rb");
    if(!f) 
        crashfmt("couldn't open file -> %s", fname);
    // get file length
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if(len < 0)
        crash("lenght of file is negative");
    fseek(f, 0, SEEK_SET);

    // allocate enough space to read the new data
    *data = malloc(len);
    // read data at the end of the buffer
    fread(*data, len, 1, f);

    fclose(f);
    return len;
}

static void _write_whole_file(const char *path, uint8_t *data, size_t dlen) {
    FILE *f = fopen(path, "wb");
    if(!f)
        crashfmt("couldn't open file -> %s", path);
    fwrite(data, dlen, 1, f);

    fclose(f);
}

static bool _strcmp(const char *s1, const char *s2) {
    // check that all the characters are the same
    for (; *s1 && *s2 ; ++s1, ++s2)
        if (*s1 != *s2)
            return false;
    // check that it didn't finish early
    if (*s1 != '\0' || *s2 != '\0')
        return false;
    return true;
}

static void _concat_path(char *dst, const char *dir, const char *path, size_t path_length) {
    strcpy(dst, path);
    dst[path_length] = '/';
    strcpy(dst + path_length + 1, dir);
}

static uint8_t _split_path(const char **path) {
    const char *c = *path;
    for (uint8_t len = 0; *c; c++, len++) {
        if(*c == '/') {
            *path += len+1;
            return len;
        }
    }
    return 0;
}

static void _create_necessary_dirs(const char *path) {
    // TODO check if the path exists before actually
    // trying every combination

    const char *tmp_path = path;

    struct stat st = {0};
    uint8_t len = 0;
    while((len = _split_path(&tmp_path))) {
        size_t sz = (tmp_path - 1) - path;
        char buf[sz+1];
        strncpy(buf, path, sz);
        buf[sz] = '\0';

        if(stat(buf, &st) == -1)
            mkdir(buf, 0777);
    }
}
#endif // Z_FOLDER_IMPLEMENTATION