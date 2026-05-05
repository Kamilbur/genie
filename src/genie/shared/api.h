/**
 * Copyright 2018-2024 The Genie Authors.
 * @file
 * @copyright This file is part of Genie. See LICENSE and/or
 * https://github.com/MueFab/genie for more details.
 */

#ifndef SRC_GENIE_SHARED_API_H_
#define SRC_GENIE_SHARED_API_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GENIE_SHARED_SUCCESS 0
#define GENIE_SHARED_INVALID_PARAMETER 13
#define GENIE_SHARED_INVALID_BITSTREAM 14
#define GENIE_SHARED_ACCESS_UNIT_NOT_FOUND 7
#define GENIE_SHARED_UNLISTED_ERROR 15

/**
 * @brief Return a static error string for a Genie shared-library return code.
 */
const char* GenieSharedStrerror(uint8_t code);

/**
 * @brief Count MPEG-G access units in an .mgb file.
 *
 * @param input_file Path to an .mgb file.
 * @param output_count Receives the number of access units.
 * @return GENIE_SHARED_SUCCESS on success, otherwise a GENIE_SHARED_* code.
 */
uint8_t GenieGetAccessUnitCount(const char* input_file, uint64_t* output_count);

/**
 * @brief Decompress one MPEG-G access unit from an .mgb file.
 *
 * output_file must end in .fastq.
 *
 * @param input_file Path to an .mgb file.
 * @param access_unit_id MPEG-G AU id to decompress.
 * @param output_file Output path.
 * @param reference_file Optional .fasta/.fa/.mgb reference path, or NULL.
 * @param working_dir Optional working directory for temporary decoder files.
 * @param threads Decoder thread count. 0 means 1 thread.
 * @return GENIE_SHARED_SUCCESS on success, otherwise a GENIE_SHARED_* code.
 */
uint8_t GenieDecompressAccessUnit(const char* input_file,
                                  uint64_t access_unit_id,
                                  const char* output_file,
                                  const char* reference_file,
                                  const char* working_dir, uint64_t threads);

/**
 * @brief Decompress one MPEG-G access unit from an .mgb file into memory.
 *
 * The returned buffer contains FASTQ bytes and is null-terminated for convenient
 * C-string use. Use output_size for binary-safe length. Caller must release
 * output_data with GenieFree().
 *
 * @param input_file Path to an .mgb file.
 * @param access_unit_id MPEG-G AU id to decompress.
 * @param reference_file Optional .fasta/.fa/.mgb reference path, or NULL.
 * @param working_dir Optional working directory for temporary decoder files.
 * @param threads Decoder thread count. 0 means 1 thread.
 * @param output_data Receives allocated FASTQ buffer.
 * @param output_size Receives FASTQ byte count, excluding null terminator.
 * @return GENIE_SHARED_SUCCESS on success, otherwise a GENIE_SHARED_* code.
 */
uint8_t GenieDecompressAccessUnitToFastq(const char* input_file,
                                         uint64_t access_unit_id,
                                         const char* reference_file,
                                         const char* working_dir,
                                         uint64_t threads,
                                         char** output_data,
                                         uint64_t* output_size);

/**
 * @brief Free memory returned by Genie shared-library functions.
 */
void GenieFree(void* ptr);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GENIE_SHARED_API_H_
