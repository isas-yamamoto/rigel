/** @file
 *  @brief C wrapper for the Rigel C++ library.
 *
 *  rigel.h is a C++ class (std::mutex/std::list members, virtual methods,
 *  no extern "C") and can't be linked from a C translation unit. This
 *  wraps it behind an opaque handle and a plain function API so C code
 *  can use it too. Same library, same rigel.meta format - a directory
 *  written via this API can be read via rigel::Rigel and vice versa.
 */
#ifndef RIGEL_RIGEL_C_H_
#define RIGEL_RIGEL_C_H_

#include <sys/types.h> /* ssize_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors rigel::BLOCK_SIZE/MAX_FILE_COUNT (see rigel.h); keep in sync. */
#define RIGEL_C_BLOCK_SIZE 1024
#define RIGEL_C_MAX_FILE_COUNT 131072

typedef struct RigelHandle RigelHandle;

/* Allocates a new, uninitialized handle. Call rigel_c_init() or
 * rigel_c_init_from_meta() before using it. Must be released with
 * rigel_c_destroy(). */
RigelHandle* rigel_c_create(void);
void rigel_c_destroy(RigelHandle* h);

/* Initializes with dirname/key/block_size/max_file_count/index_offset
 * given directly. */
void rigel_c_init(RigelHandle* h, const char* dirname, const char* key,
                   int block_size, int max_file_count, int index_offset);

/* Initializes by reading metadata from dirname/rigel.meta (written by
 * rigel_c_write_meta() or rigel::Rigel::WriteMeta()). Returns 1 on
 * success, 0 if the metadata is missing or malformed. */
int rigel_c_init_from_meta(RigelHandle* h, const char* dirname);

/* Writes key/block_size/max_file_count/index_offset as metadata under
 * dirname. Returns 1 on success, 0 on failure. */
int rigel_c_write_meta(const char* dirname, const char* key,
                        int block_size, int max_file_count, int index_offset);

/* Returns the number of bytes written on success, -1 on failure. */
ssize_t rigel_c_write(RigelHandle* h, int index, const unsigned char* data, size_t size);

/* Returns the number of bytes read on success, -1 on failure (including
 * a normal, expected "never written this index" miss). */
ssize_t rigel_c_read(RigelHandle* h, int index, unsigned char* data, size_t size);

/* Clears index back to never-written. Returns 1 on success (including a
 * no-op on an index that was never written), 0 on failure. */
int rigel_c_delete(RigelHandle* h, int index);

int rigel_c_scan_init(RigelHandle* h, int start);
/* Returns the next written index, or -1 when scanning is exhausted. */
int rigel_c_scan_next(RigelHandle* h);

/* Details of the most recent failure. Empty string if none, or if the
 * last failure was a normal miss (see rigel_c_read()'s doc above). */
const char* rigel_c_last_error(const RigelHandle* h);

int rigel_c_block_size(const RigelHandle* h);
const char* rigel_c_key(const RigelHandle* h);
int rigel_c_max_file_count(const RigelHandle* h);
int rigel_c_index_offset(const RigelHandle* h);

/* Snapshot of key/geometry/usage info, as printed by `rigel stat`. Mirrors
 * rigel::Rigel::Stat. */
typedef struct RigelCStat {
  int block_size;
  int max_file_count;
  unsigned long long max_file_size;
  int index_offset;
  long long record_count;
  int min_index; /* -1 if record_count == 0 */
  int max_index; /* -1 if record_count == 0 */
  int shard_count;
  unsigned long long shard_bytes;
  unsigned long long index_bytes;
} RigelCStat;

/* Fills *out with a full stat snapshot in one native call (no per-record
 * round trip through the caller's language). Returns 1 on success, 0 on
 * failure (see rigel_c_last_error()). */
int rigel_c_stat(RigelHandle* h, RigelCStat* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RIGEL_RIGEL_C_H_ */
