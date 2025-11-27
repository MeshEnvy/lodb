#include "LoDB.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "configuration.h"
#include "gps/RTC.h"
#include <Arduino.h>
#include <SHA256.h>
#include <algorithm>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

/**
 * LoDB Implementation - Synchronous Design
 *
 * Threading Model:
 * - All filesystem operations use LockGuard(spiLock) for thread safety
 * - All operations complete immediately and return results synchronously
 * - SELECT returns complete result sets with optional filtering, sorting, and limiting
 */

// Convert UUID to 16-character hex string
void lodb_uuid_to_hex(lodb_uuid_t uuid, char hex_out[17])
{
    snprintf(hex_out, 17, LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
}

// Generate or derive a UUID
lodb_uuid_t lodb_new_uuid(const char *str, uint64_t salt)
{
    char generated_str[32];
    const char *input_str = str;

    // If no string provided, generate one from timestamp and random
    if (str == nullptr) {
        uint32_t timestamp = getTime();
        uint32_t random_val = random(0xFFFFFFFF);
        snprintf(generated_str, sizeof(generated_str), "%u:%u", timestamp, random_val);
        input_str = generated_str;
    }

    // Always hash string with salt
    SHA256 sha256;
    uint8_t hash[32];

    sha256.reset();
    sha256.update(input_str, strlen(input_str));

    // Add salt (always included now)
    uint8_t salt_bytes[8];
    memcpy(salt_bytes, &salt, 8);
    sha256.update(salt_bytes, 8);

    sha256.finalize(hash, 32);

    // Use first 8 bytes as uint64_t
    lodb_uuid_t uuid;
    memcpy(&uuid, hash, sizeof(lodb_uuid_t));
    return uuid;
}

// LoDb Class Implementation

LoDb::LoDb(const char *db_name) : db_name(db_name)
{
    // Build database path
    snprintf(db_path, sizeof(db_path), "/lodb/%s", db_name);

#ifdef FSCom
    // Create directories
    concurrency::LockGuard g(spiLock);
    FSCom.mkdir("/lodb");
    if (!FSCom.mkdir(db_path)) {
        LOG_DEBUG("Database directory may already exist or created: %s", db_path);
    }
#else
    LOG_ERROR("Filesystem not available");
#endif

    LOG_INFO("Initialized LoDB database: %s", db_path);
}

LoDb::~LoDb()
{
    // Nothing to clean up for now
}

LoDbError LoDb::registerTable(const char *table_name, const pb_msgdesc_t *pb_descriptor, size_t record_size)
{
    if (!table_name || !pb_descriptor || record_size == 0) {
        return LODB_ERR_INVALID;
    }

    TableMetadata metadata;
    metadata.table_name = table_name;
    metadata.pb_descriptor = pb_descriptor;
    metadata.record_size = record_size;

    // Build table path: /lodb/{db_name}/{table_name}/
    snprintf(metadata.table_path, sizeof(metadata.table_path), "%s/%s", db_path, table_name);

#ifdef FSCom
    // Create table directory
    concurrency::LockGuard g(spiLock);
    if (!FSCom.mkdir(metadata.table_path)) {
        LOG_DEBUG("Table directory may already exist or created: %s", metadata.table_path);
    }
#else
    LOG_ERROR("Filesystem not available");
    return LODB_ERR_IO;
#endif

    tables[table_name] = metadata;
    LOG_INFO("Registered table: %s at %s", table_name, metadata.table_path);
    return LODB_OK;
}

LoDb::TableMetadata *LoDb::getTable(const char *table_name)
{
    auto it = tables.find(table_name);
    if (it == tables.end()) {
        LOG_ERROR("Table not registered: %s", table_name);
        return nullptr;
    }
    return &it->second;
}

// Insert a record with a UUID
LoDbError LoDb::insert(const char *table_name, lodb_uuid_t uuid, const void *record)
{
    if (!table_name || !record) {
        return LODB_ERR_INVALID;
    }

    TableMetadata *table = getTable(table_name);
    if (!table) {
        return LODB_ERR_INVALID;
    }

#ifdef FSCom
    // Convert UUID to hex for filename
    char uuid_hex[17];
    lodb_uuid_to_hex(uuid, uuid_hex);

    // Build file path
    char file_path[192];
    snprintf(file_path, sizeof(file_path), "%s/%s.pr", table->table_path, uuid_hex);

    // Check if file already exists
    {
        concurrency::LockGuard g(spiLock);
        auto existing = FSCom.open(file_path, FILE_O_READ);
        if (existing) {
            existing.close();
            LOG_ERROR("UUID already exists: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
            return LODB_ERR_INVALID;
        }
    }

    // Encode to buffer
    uint8_t buffer[2048];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, table->pb_descriptor, record)) {
        LOG_ERROR("Failed to encode protobuf for insert");
        return LODB_ERR_ENCODE;
    }

    size_t encoded_size = stream.bytes_written;
    LOG_DEBUG("Encoded record: %d bytes", encoded_size);

    // Write to file
    {
        concurrency::LockGuard g(spiLock);
        auto file = FSCom.open(file_path, FILE_O_WRITE);
        if (!file) {
            LOG_ERROR("Failed to open file for writing: %s", file_path);
            return LODB_ERR_IO;
        }

        size_t written = file.write(buffer, encoded_size);
        if (written != encoded_size) {
            LOG_ERROR("Failed to write file, wrote %d of %d bytes", written, encoded_size);
            file.close();
            return LODB_ERR_IO;
        }

        file.flush();
        file.close();
        LOG_DEBUG("Wrote record to: %s (%d bytes)", file_path, encoded_size);
    }

    LOG_INFO("Inserted record with custom UUID: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
    return LODB_OK;
#else
    LOG_ERROR("Filesystem not available");
    return LODB_ERR_IO;
#endif
}

// Get a record by UUID
LoDbError LoDb::get(const char *table_name, lodb_uuid_t uuid, void *record_out)
{
    if (!table_name || !record_out) {
        return LODB_ERR_INVALID;
    }

    TableMetadata *table = getTable(table_name);
    if (!table) {
        return LODB_ERR_INVALID;
    }

#ifdef FSCom
    // Convert UUID to hex for filename
    char uuid_hex[17];
    lodb_uuid_to_hex(uuid, uuid_hex);

    // Build file path
    char file_path[192];
    snprintf(file_path, sizeof(file_path), "%s/%s.pr", table->table_path, uuid_hex);
    LOG_DEBUG("file_path: %s", file_path);

    // Read file into buffer
    uint8_t buffer[2048];
    size_t file_size = 0;

    {
        concurrency::LockGuard g(spiLock);

        auto file = FSCom.open(file_path, FILE_O_READ);
        if (!file) {
            LOG_DEBUG("Record not found: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
            return LODB_ERR_NOT_FOUND;
        }

        file_size = file.read(buffer, sizeof(buffer));
        file.close();

        if (file_size == 0) {
            LOG_ERROR("Record file is empty: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
            return LODB_ERR_IO;
        }

        LOG_DEBUG("Read record file: %s (%d bytes)", file_path, file_size);
    }

    // Decode from buffer
    pb_istream_t stream = pb_istream_from_buffer(buffer, file_size);
    memset(record_out, 0, table->record_size);

    if (!pb_decode(&stream, table->pb_descriptor, record_out)) {
        LOG_ERROR("Failed to decode protobuf from " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
        return LODB_ERR_DECODE;
    }

    LOG_DEBUG("Retrieved record: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
    return LODB_OK;
#else
    LOG_ERROR("Filesystem not available");
    return LODB_ERR_IO;
#endif
}

// Update a single record by UUID
LoDbError LoDb::update(const char *table_name, lodb_uuid_t uuid, const void *record)
{
    if (!table_name || !record) {
        return LODB_ERR_INVALID;
    }

    TableMetadata *table = getTable(table_name);
    if (!table) {
        return LODB_ERR_INVALID;
    }

#ifdef FSCom
    // Convert UUID to hex for filename
    char uuid_hex[17];
    lodb_uuid_to_hex(uuid, uuid_hex);

    // Build file path
    char file_path[192];
    snprintf(file_path, sizeof(file_path), "%s/%s.pr", table->table_path, uuid_hex);

    // Check if record exists first
    {
        concurrency::LockGuard g(spiLock);
        auto file = FSCom.open(file_path, FILE_O_READ);
        if (!file) {
            LOG_DEBUG("Record not found for update: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
            return LODB_ERR_NOT_FOUND;
        }
        file.close();
    }

    // Encode to buffer
    uint8_t buffer[2048];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, table->pb_descriptor, record)) {
        LOG_ERROR("Failed to encode updated record: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
        return LODB_ERR_ENCODE;
    }

    size_t encoded_size = stream.bytes_written;

    // Write to file
    {
        concurrency::LockGuard g(spiLock);
        FSCom.remove(file_path); // Remove old file
        auto file = FSCom.open(file_path, FILE_O_WRITE);
        if (!file) {
            LOG_ERROR("Failed to open file for update: %s", file_path);
            return LODB_ERR_IO;
        }

        size_t written = file.write(buffer, encoded_size);
        if (written != encoded_size) {
            LOG_ERROR("Failed to write updated file");
            file.close();
            return LODB_ERR_IO;
        }

        file.flush();
        file.close();
    }

    LOG_INFO("Updated record: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
    return LODB_OK;
#else
    LOG_ERROR("Filesystem not available");
    return LODB_ERR_IO;
#endif
}

// Delete a single record by UUID
LoDbError LoDb::deleteRecord(const char *table_name, lodb_uuid_t uuid)
{
    if (!table_name) {
        return LODB_ERR_INVALID;
    }

    TableMetadata *table = getTable(table_name);
    if (!table) {
        return LODB_ERR_INVALID;
    }

#ifdef FSCom
    // Convert UUID to hex for filename
    char uuid_hex[17];
    lodb_uuid_to_hex(uuid, uuid_hex);

    char file_path[192];
    snprintf(file_path, sizeof(file_path), "%s/%s.pr", table->table_path, uuid_hex);

    {
        concurrency::LockGuard g(spiLock);
        if (FSCom.remove(file_path)) {
            LOG_DEBUG("Deleted record: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
            return LODB_OK;
        } else {
            LOG_WARN("Failed to delete record (may not exist): " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
            return LODB_ERR_NOT_FOUND;
        }
    }
#else
    LOG_ERROR("Filesystem not available");
    return LODB_ERR_IO;
#endif
}

// Select records with optional filtering, sorting, and limiting
std::vector<void *> LoDb::select(const char *table_name, LoDbFilter filter, LoDbComparator comparator, size_t limit)
{
    std::vector<void *> results;

    if (!table_name) {
        LOG_ERROR("Invalid table_name");
        return results;
    }

    TableMetadata *table = getTable(table_name);
    if (!table) {
        LOG_ERROR("Table not found: %s", table_name);
        return results;
    }

#ifdef FSCom
    // PHASE 1: FILTER - iterate directory and collect matching records
    {
        concurrency::LockGuard g(spiLock);

        File dir = FSCom.open(table->table_path, FILE_O_READ);
        if (!dir) {
            LOG_DEBUG("Table directory not found: %s", table->table_path);
            return results; // Empty result set
        }

        if (!dir.isDirectory()) {
            LOG_ERROR("Table path is not a directory: %s", table->table_path);
            dir.close();
            return results;
        }

        // Iterate through all files in directory
        while (true) {
            File file = dir.openNextFile();
            if (!file) {
                break; // No more files
            }

            // Skip directories
            if (file.isDirectory()) {
                file.close();
                continue;
            }

            // Get filename
            std::string pathStr = file.name();
            file.close();

            // Extract just the filename (after last /)
            size_t lastSlash = pathStr.rfind('/');
            std::string filename = (lastSlash != std::string::npos) ? pathStr.substr(lastSlash + 1) : pathStr;

            // Extract UUID (remove .pr extension)
            size_t prPos = filename.find(".pr");
            if (prPos == std::string::npos) {
                LOG_DEBUG("Skipped non-.pr file: %s", filename.c_str());
                continue;
            }

            std::string uuid_hex_str = filename.substr(0, prPos);

            // Parse hex string to uint64_t UUID
            lodb_uuid_t uuid;
            uint32_t high, low;
            if (sscanf(uuid_hex_str.c_str(), "%08x%08x", &high, &low) != 2) {
                LOG_WARN("Failed to parse UUID from filename: %s", uuid_hex_str.c_str());
                continue;
            }
            uuid = ((uint64_t)high << 32) | (uint64_t)low;

            // Allocate buffer for record
            uint8_t *record_buffer = new uint8_t[table->record_size];
            if (!record_buffer) {
                LOG_ERROR("Failed to allocate record buffer");
                continue;
            }

            // Read and decode the record (releases spiLock internally)
            memset(record_buffer, 0, table->record_size);

            // We need to release the lock before calling get() since it acquires it
            g.~LockGuard(); // Release lock
            LoDbError err = get(table_name, uuid, record_buffer);
            new (&g) concurrency::LockGuard(spiLock); // Re-acquire lock

            if (err != LODB_OK) {
                LOG_WARN("Failed to read record " LODB_UUID_FMT " during select", LODB_UUID_ARGS(uuid));
                delete[] record_buffer;
                continue;
            }

            // Apply filter if provided
            if (filter && !filter(record_buffer)) {
                LOG_DEBUG("Record " LODB_UUID_FMT " filtered out", LODB_UUID_ARGS(uuid));
                delete[] record_buffer;
                continue;
            }

            // Record passed filter, add to results
            results.push_back(record_buffer);
            LOG_DEBUG("Added record " LODB_UUID_FMT " to results", LODB_UUID_ARGS(uuid));
        }

        dir.close();
    }

    LOG_INFO("Select from %s: %d records after filtering", table_name, results.size());

    // PHASE 2: SORT - sort results if comparator provided
    if (comparator && !results.empty()) {
        std::sort(results.begin(), results.end(), [comparator](const void *a, const void *b) { return comparator(a, b) < 0; });
        LOG_DEBUG("Sorted %d records", results.size());
    }

    // PHASE 3: LIMIT - apply limit if specified
    if (limit > 0 && results.size() > limit) {
        // Free records beyond limit
        for (size_t i = limit; i < results.size(); i++) {
            delete[] (uint8_t *)results[i];
        }
        results.resize(limit);
        LOG_DEBUG("Limited results to %d records", limit);
    }

    LOG_INFO("Select from %s complete: %d records returned", table_name, results.size());
#else
    LOG_ERROR("Filesystem not available");
#endif

    return results;
}
