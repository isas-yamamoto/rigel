/** @file
 *  @brief Implementation of the C wrapper declared in rigel_c.h.
 */
#include "rigel_c.h"
#include "rigel.h"

struct RigelHandle {
  rigel::Rigel impl;
};

extern "C" {

RigelHandle* rigel_c_create(void) {
  return new RigelHandle();
}

void rigel_c_destroy(RigelHandle* h) {
  delete h;
}

void rigel_c_init(RigelHandle* h, const char* dirname, const char* key,
                   int block_size, int max_file_count, int index_offset) {
  h->impl.Init(dirname, key, block_size, max_file_count, index_offset);
}

int rigel_c_init_from_meta(RigelHandle* h, const char* dirname) {
  return h->impl.Init(dirname) ? 1 : 0;
}

int rigel_c_write_meta(const char* dirname, const char* key,
                        int block_size, int max_file_count, int index_offset) {
  return rigel::Rigel::WriteMeta(dirname, key, block_size, max_file_count, index_offset) ? 1 : 0;
}

ssize_t rigel_c_write(RigelHandle* h, int index, const unsigned char* data, size_t size) {
  return h->impl.Write(index, data, size);
}

ssize_t rigel_c_read(RigelHandle* h, int index, unsigned char* data, size_t size) {
  return h->impl.Read(index, data, size);
}

int rigel_c_delete(RigelHandle* h, int index) {
  return h->impl.Delete(index) ? 1 : 0;
}

int rigel_c_scan_init(RigelHandle* h, int start) {
  return h->impl.ScanInit(start) ? 1 : 0;
}

int rigel_c_scan_next(RigelHandle* h) {
  return h->impl.ScanNext();
}

const char* rigel_c_last_error(const RigelHandle* h) {
  return h->impl.LastError();
}

int rigel_c_block_size(const RigelHandle* h) {
  return h->impl.BlockSize();
}

const char* rigel_c_key(const RigelHandle* h) {
  return h->impl.Key();
}

int rigel_c_max_file_count(const RigelHandle* h) {
  return h->impl.MaxFileCount();
}

int rigel_c_index_offset(const RigelHandle* h) {
  return h->impl.IndexOffset();
}

} // extern "C"
