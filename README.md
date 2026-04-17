# LoDB - Protobuf-on-files database

**A synchronous protobuf-on-files database for Arduino-style firmware; uses [LoFS](https://github.com/MeshEnvy/lofs) for filesystem access**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

LoDB provides CRUD operations with powerful SELECT queries supporting filtering, sorting, and limiting, all using Protocol Buffers for data serialization and onboard file storage.

## Features

- **Synchronous Design**: All operations complete immediately with results returned directly
- **Protocol Buffers**: Type-safe data storage using nanopb for efficient serialization
- **CRUD Operations**: Create, Read, Update, and Delete records with simple API calls
- **Powerful Queries**: SELECT with filtering, sorting, and limiting in a single operation
- **Deterministic UUIDs**: Generate consistent UUIDs from strings or auto-generate unique ones
- **Thread-safe FS access**: Operations go through LoFS, which takes `spiLock` per call
- **Filesystem-based**: Records under `/lodb/...` by default, or `/internal/lodb/...` / `/sd/lodb/...` when you pick a filesystem explicitly
- **Memory Efficient**: Designed for resource-constrained embedded systems

## Installation

### PlatformIO (library)

Add LoDB to `lib_deps`. It depends on **LoFS**, **nanopb**, and **rweather/Crypto** (see `library.json`). Your firmware still supplies **your** table schemas (`.proto` / generated `*_pb.h` for your app). The library ships **pre-generated nanopb** headers under **`include/lodb/`** for the optional **`lodb.DiagnosticsTest`** type (`diagnostics.pb.h`); the matching **`diagnostics.pb.c`** is compiled from **`src/`**.

```ini
lib_deps =
  https://github.com/MeshEnvy/lodb.git
```

Optional logging (define before including `<lodb/LoDB.h>`):

```cpp
#define LODB_LOG_DEBUG(...) LOG_DEBUG(__VA_ARGS__)
#define LODB_LOG_INFO(...)  LOG_INFO(__VA_ARGS__)
#define LODB_LOG_WARN(...)  LOG_WARN(__VA_ARGS__)
#define LODB_LOG_ERROR(...) LOG_ERROR(__VA_ARGS__)
```

Override weak **`lodb_now_ms()`** if you need wall time for auto-UUIDs (default: `millis()`).

**Regenerating shipped nanopb** (when editing `src/diagnostics.proto`): run `vendor/lodb/scripts/regen_nanopb.sh` (requires Python package `nanopb`).

**Public includes** (no `src/` in the path):

```cpp
#include <lodb/LoDB.h>
// or: #include <lodb/lodb.h>
#include <lodb/diagnostics.pb.h>  // optional: shipped diagnostics message
#include <lodb/diagnostics.h>     // optional: declares lodb_diagnostics()
```

## Getting Started

### Using LoDB

**Define the schema:**

```proto
syntax = "proto3";

message User {
  string username = 1;
  bytes password_hash = 2;
  uint64 uuid = 3;
}
```

Create a matching `.options` file for nanopb:

```
User.username max_size:32
User.password_hash max_size:32
```

**Note:** Generate nanopb headers for your own `.proto` files with the [nanopb](https://github.com/nanopb/nanopb) generator (or your firmware’s existing protobuf pipeline).

**Initialize the database:**

```cpp
#include <lodb/LoDB.h>
#include "myschema.pb.h"

// Default: legacy `/lodb/...` on internal flash (see LoDB constructor docs)
LoDb *db = new LoDb("myapp");

// Or explicitly specify filesystem
LoDb *dbInternal = new LoDb("myapp", LoFS::FSType::INTERNAL);
LoDb *dbSD = new LoDb("myapp", LoFS::FSType::SD);

db->registerTable("users", &User_msg, sizeof(User));
```

**Perform CRUD operations:**

```cpp
User user = User_init_zero;
strncpy(user.username, "alice", sizeof(user.username) - 1);
user.uuid = lodb_new_uuid("alice", myNodeId);
LoDbError err = db->insert("users", user.uuid, &user);

User loadedUser = User_init_zero;
err = db->get("users", user.uuid, &loadedUser);

loadedUser.some_field = new_value;
err = db->update("users", user.uuid, &loadedUser);

err = db->deleteRecord("users", user.uuid);
```

## Under the Hood

### Storage model

Each record is one `.pr` file named with a 16-character hex UUID. Layout depends on the `LoFS::FSType` you pass to `LoDb`:

- **`LoFS::FSType::AUTO` (default):** **`/lodb/<database>/<table>/<uuid>.pr`** (unprefixed path; LoFS routes to internal flash — matches older single-partition layouts).
- **`LoFS::FSType::INTERNAL`:** **`/internal/lodb/<database>/…`**
- **`LoFS::FSType::SD`:** **`/sd/lodb/<database>/…`** when SD is available; otherwise LoDB falls back to **`/internal/lodb/…`** with a warning.

### Example on-disk layout (AUTO)

```
/lodb/myapp/
  └── items/
      ├── a1b2c3d4e5f67890.pr
      └── 1234567890abcdef.pr
```

### Thread safety

Filesystem calls use **LoFS**, which acquires `LockGuard(spiLock)` inside each `LoFS::open` / `mkdir` / `remove` (and related) call. LoDB does not take `spiLock` itself, to avoid deadlocking with LoFS’s non-recursive mutex.

### UUIDs

- **Deterministic:** `lodb_new_uuid("key", salt)` — SHA-256 of string + salt, first 8 bytes as `uint64_t`.
- **Auto:** `lodb_new_uuid(nullptr, salt)` — uses weak **`lodb_now_ms()`** (default `millis()`) plus randomness; override **`lodb_now_ms()`** in your firmware if you need wall time.
- **Filenames:** `lodb_uuid_to_hex()` → 16 hex chars + `.pr`.

### Patterns (generic)

**Register a table** (your nanopb message `User` from your own `.proto`):

```cpp
LoDb db("myapp");
db.registerTable("users", &User_msg, sizeof(User));
```

**Deterministic id + get:**

```cpp
lodb_uuid_t id = lodb_new_uuid("alice", nodeSalt);
User u = User_init_zero;
if (db.get("users", id, &u) == LODB_OK) { /* ... */ }
```

**Select with filter, sort, limit:**

```cpp
auto active = [](const void *r) -> bool {
  return ((const User *)r)->active;
};
auto byName = [](const void *a, const void *b) -> int {
  return strcmp(((const User *)a)->name, ((const User *)b)->name);
};
auto rows = db.select("users", active, byName, 10);
// ... use rows ...
LoDb::freeRecords(rows);
```

**Read–modify–write:**

```cpp
User u = User_init_zero;
if (db.get("users", uuid, &u) != LODB_OK) return;
u.score = 42;
db.update("users", uuid, &u);
```

### Optional diagnostics

For bring-up, you can call **`lodb_diagnostics()`** (see `<lodb/diagnostics.h>`). It uses the shipped **`lodb.DiagnosticsTest`** type (`<lodb/diagnostics.pb.h>`) to exercise CRUD, `select`, `count`, `truncate`, and `drop` against temporary databases under `/lodb`, `/internal/lodb`, and `/sd/lodb`.

## API Reference

### Types and Constants

#### `lodb_uuid_t`

```cpp
typedef uint64_t lodb_uuid_t;
```

64-bit unsigned integer used as record identifier.

#### `LoDbError`

Error codes returned by database operations:

```cpp
typedef enum {
    LODB_OK = 0,        // Success
    LODB_ERR_NOT_FOUND, // UUID doesn't exist
    LODB_ERR_IO,        // Filesystem error
    LODB_ERR_DECODE,    // Protobuf decode failed
    LODB_ERR_ENCODE,    // Protobuf encode failed
    LODB_ERR_INVALID    // Invalid parameters
} LoDbError;
```

#### `LoDbFilter`

```cpp
typedef std::function<bool(const void *)> LoDbFilter;
```

Filter function for SELECT queries. Returns `true` to include a record in results.

**Example:**

```cpp
auto filter = [targetUserId](const void *rec) -> bool {
    const Message *msg = (const Message *)rec;
    return msg->user_id == targetUserId;
};
```

#### `LoDbComparator`

```cpp
typedef std::function<int(const void *, const void *)> LoDbComparator;
```

Comparator function for sorting SELECT results. Returns:

- `-1` if `a < b`
- `0` if `a == b`
- `1` if `a > b`

**Example:**

```cpp
auto comparator = [](const void *a, const void *b) -> int {
    const User *u1 = (const User *)a;
    const User *u2 = (const User *)b;
    return strcmp(u1->name, u2->name);
};
```

#### UUID Formatting Macros

```cpp
#define LODB_UUID_FMT "%08x%08x"
#define LODB_UUID_ARGS(uuid) (uint32_t)((uuid) >> 32), (uint32_t)((uuid) & 0xFFFFFFFF)
```

Use these for printf-style UUID formatting on platforms without `%llx` support:

```cpp
LOG_INFO("UUID: " LODB_UUID_FMT, LODB_UUID_ARGS(uuid));
// Output: UUID: a1b2c3d4e5f67890
```

### Functions

#### `lodb_new_uuid()`

```cpp
lodb_uuid_t lodb_new_uuid(const char *str, uint64_t salt);
```

Generate or derive a UUID.

**Parameters:**

- `str`: String to hash into UUID, or `NULL` for auto-generated
- `salt`: Salt value (typically node ID), or `0` for none

**Returns:** 64-bit UUID

**Behavior:**

- If `str` is `NULL`: Generates unique UUID from timestamp + random value
- If `str` is provided: Generates deterministic UUID via `SHA256(str + salt)`

**Examples:**

```cpp
// Auto-generated UUID (unique)
lodb_uuid_t uuid = lodb_new_uuid(NULL, 0);

// Deterministic UUID for lookups (same inputs = same UUID)
lodb_uuid_t userUuid = lodb_new_uuid("alice", myNodeId);
```

#### `lodb_uuid_to_hex()`

```cpp
void lodb_uuid_to_hex(lodb_uuid_t uuid, char hex_out[17]);
```

Convert UUID to 16-character hex string.

**Parameters:**

- `uuid`: UUID to convert
- `hex_out`: Buffer for hex string (must be at least 17 bytes)

**Example:**

```cpp
char hex[17];
lodb_uuid_to_hex(uuid, hex);
printf("UUID: %s\n", hex); // UUID: a1b2c3d4e5f67890
```

### LoDb Class

#### Constructor

```cpp
LoDb(const char *db_name, LoFS::FSType filesystem = LoFS::FSType::AUTO);
```

Create a new database instance with namespace `db_name`.

**Parameters:**

- `db_name`: Database name (directory under the LoDB root for the chosen filesystem mode).
- `filesystem`:
  - **`AUTO` (default):** **`/lodb/<db_name>/`** on internal flash (legacy layout).
  - **`INTERNAL`:** **`/internal/lodb/<db_name>/`**
  - **`SD`:** **`/sd/lodb/<db_name>/`** when SD is available; otherwise **`/internal/lodb/<db_name>/`** with a warning.

**Examples:**

```cpp
LoDb *db = new LoDb("myapp");
LoDb *dbInternal = new LoDb("myapp", LoFS::FSType::INTERNAL);
LoDb *dbSD = new LoDb("myapp", LoFS::FSType::SD);
```

#### `registerTable()`

```cpp
LoDbError registerTable(const char *table_name,
                        const pb_msgdesc_t *pb_descriptor,
                        size_t record_size);
```

Register a table with protobuf schema.

**Parameters:**

- `table_name`: Table name (directory name)
- `pb_descriptor`: Nanopb message descriptor (e.g., `&User_msg`)
- `record_size`: Size of struct (e.g., `sizeof(User)`)

**Returns:** `LODB_OK` on success, error code otherwise

**Example:**

```cpp
db->registerTable("users", &User_msg, sizeof(User));
```

#### `insert()`

```cpp
LoDbError insert(const char *table_name,
                 lodb_uuid_t uuid,
                 const void *record);
```

Insert a new record with specified UUID.

**Parameters:**

- `table_name`: Name of table
- `uuid`: UUID for this record
- `record`: Pointer to protobuf record

**Returns:**

- `LODB_OK` on success
- `LODB_ERR_INVALID` if UUID already exists or table not registered
- Other error codes for filesystem/encoding issues

**Example:**

```cpp
User user = User_init_zero;
strncpy(user.username, "alice", sizeof(user.username) - 1);
lodb_uuid_t uuid = lodb_new_uuid("alice", nodeId);
LoDbError err = db->insert("users", uuid, &user);
```

#### `get()`

```cpp
LoDbError get(const char *table_name,
              lodb_uuid_t uuid,
              void *record_out);
```

Retrieve a record by UUID.

**Parameters:**

- `table_name`: Name of table
- `uuid`: UUID of record to retrieve
- `record_out`: Buffer to store decoded record (must be at least `record_size` bytes)

**Returns:**

- `LODB_OK` on success
- `LODB_ERR_NOT_FOUND` if UUID doesn't exist
- Other error codes for filesystem/decoding issues

**Example:**

```cpp
User user = User_init_zero;
lodb_uuid_t uuid = lodb_new_uuid("alice", nodeId);
LoDbError err = db->get("users", uuid, &user);
if (err == LODB_OK) {
    printf("Username: %s\n", user.username);
}
```

#### `update()`

```cpp
LoDbError update(const char *table_name,
                 lodb_uuid_t uuid,
                 const void *record);
```

Update an existing record by UUID.

**Parameters:**

- `table_name`: Name of table
- `uuid`: UUID of record to update
- `record`: Pointer to updated protobuf record

**Returns:**

- `LODB_OK` on success
- `LODB_ERR_NOT_FOUND` if UUID doesn't exist
- Other error codes for filesystem/encoding issues

**Example:**

```cpp
User user = User_init_zero;
db->get("users", uuid, &user);
user.some_field = new_value;
db->update("users", uuid, &user);
```

#### `deleteRecord()`

```cpp
LoDbError deleteRecord(const char *table_name,
                       lodb_uuid_t uuid);
```

Delete a record by UUID.

**Parameters:**

- `table_name`: Name of table
- `uuid`: UUID of record to delete

**Returns:**

- `LODB_OK` on success
- `LODB_ERR_NOT_FOUND` if UUID doesn't exist

**Example:**

```cpp
LoDbError err = db->deleteRecord("users", uuid);
```

#### `select()`

```cpp
std::vector<void *> select(const char *table_name,
                           LoDbFilter filter = LoDbFilter(),
                           LoDbComparator comparator = LoDbComparator(),
                           size_t limit = 0);
```

Query records with optional filtering, sorting, and limiting.

**Operation Order:** FILTER → SORT → LIMIT

**Parameters:**

- `table_name`: Name of table to query
- `filter`: Optional filter function (default: select all)
- `comparator`: Optional comparator for sorting (default: no sorting)
- `limit`: Optional result limit (default: 0 = no limit)

**Returns:** Vector of heap-allocated record pointers

**Memory Management:** Caller must free records using `freeRecords()` or manually with `delete[] (uint8_t *)rec`

**Examples:**

```cpp
// Select all users
auto allUsers = db->select("users");

// Select with filter
auto filter = [](const void *rec) -> bool {
    const User *u = (const User *)rec;
    return u->age >= 18;
};
auto adults = db->select("users", filter);

// Select with filter and sort
auto comparator = [](const void *a, const void *b) -> int {
    const User *u1 = (const User *)a;
    const User *u2 = (const User *)b;
    return strcmp(u1->name, u2->name);
};
auto sortedAdults = db->select("users", filter, comparator);

// Select with filter, sort, and limit (top 10)
auto top10 = db->select("users", filter, comparator, 10);

// Process results
for (auto *ptr : top10) {
    const User *user = (const User *)ptr;
    // ... use user
}

// Free memory
LoDb::freeRecords(top10);
```

#### `freeRecords()`

```cpp
static void freeRecords(std::vector<void *> &records);
```

Helper method to free all records in a vector returned by `select()`.

**Parameters:**

- `records`: Vector of record pointers to free (will be cleared after freeing)

**Example:**

```cpp
auto results = db->select("users");
// ... use results ...
LoDb::freeRecords(results);  // Frees all records and clears vector
```

#### `count()`

```cpp
int count(const char *table_name, LoDbFilter filter = LoDbFilter());
```

Count records in a table with optional filtering.

**Parameters:**

- `table_name`: Name of the table to count
- `filter`: Optional filter function (default: count all records)

**Returns:** Number of matching records, or `-1` on error

**Performance:**

- If no filter is provided, efficiently counts files without loading records
- If a filter is provided, records are loaded and filtered (less efficient)

**Examples:**

```cpp
// Count all users
int totalUsers = db->count("users");

// Count with filter
auto filter = [](const void *rec) -> bool {
    const User *u = (const User *)rec;
    return u->age >= 18;
};
int adultUsers = db->count("users", filter);

if (totalUsers >= 0) {
    LOG_INFO("Total users: %d, Adults: %d", totalUsers, adultUsers);
}
```

#### `truncate()`

```cpp
LoDbError truncate(const char *table_name);
```

Delete all records from a table but keep the table registered.

**Parameters:**

- `table_name`: Name of the table to truncate

**Returns:**

- `LODB_OK` on success
- `LODB_ERR_INVALID` if table not registered

**Example:**

```cpp
// Clear all records from users table
LoDbError err = db->truncate("users");
if (err == LODB_OK) {
    LOG_INFO("Truncated users table");
}

// Table is still registered, can insert new records
User newUser = User_init_zero;
db->insert("users", uuid, &newUser);
```

#### `drop()`

```cpp
LoDbError drop(const char *table_name);
```

Delete all records and unregister the table. The table must be re-registered before use.

**Parameters:**

- `table_name`: Name of the table to drop

**Returns:**

- `LODB_OK` on success
- `LODB_ERR_INVALID` if table not registered

**Example:**

```cpp
// Drop the users table completely
LoDbError err = db->drop("users");
if (err == LODB_OK) {
    LOG_INFO("Dropped users table");
}

// Table is unregistered - must re-register before use
db->registerTable("users", &User_msg, sizeof(User));
```

## Advanced Usage

### Lambda Captures in Filters

Lambdas can capture variables from enclosing scope for dynamic filtering:

```cpp
// Capture multiple variables
uint32_t minAge = 18;
uint32_t maxAge = 65;
const char *country = "USA";

auto filter = [minAge, maxAge, country](const void *rec) -> bool {
    const User *u = (const User *)rec;
    return u->age >= minAge &&
           u->age <= maxAge &&
           strcmp(u->country, country) == 0;
};

auto results = db->select("users", filter);
```

### Complex Sorting

Sort by multiple criteria:

```cpp
auto comparator = [](const void *a, const void *b) -> int {
    const Message *m1 = (const Message *)a;
    const Message *m2 = (const Message *)b;

    // Primary sort: unread messages first
    if (!m1->read && m2->read) return -1;
    if (m1->read && !m2->read) return 1;

    // Secondary sort: newest first (reverse timestamp order)
    if (m2->timestamp > m1->timestamp) return 1;
    if (m2->timestamp < m1->timestamp) return -1;

    return 0;
};

auto messages = db->select("mail", LoDbFilter(), comparator);
```

### Memory Management Best Practices

Always free records returned by `select()`:

```cpp
auto results = db->select("users");

// Use results
for (auto *ptr : results) {
    const User *user = (const User *)ptr;
    processUser(user);
}

// Clean up - CRITICAL!
LoDb::freeRecords(results);
```

### Upsert Pattern

Implement upsert (insert or update) logic:

```cpp
bool upsertUser(LoDb *db, lodb_uuid_t uuid, const User *user) {
    // Try to get existing record
    User existing = User_init_zero;
    LoDbError err = db->get("users", uuid, &existing);

    if (err == LODB_OK) {
        // Record exists, update it
        return db->update("users", uuid, user) == LODB_OK;
    } else if (err == LODB_ERR_NOT_FOUND) {
        // Record doesn't exist, insert it
        return db->insert("users", uuid, user) == LODB_OK;
    }

    // Other error
    return false;
}
```

### Counting Records

Use `count()` to efficiently get the number of records:

```cpp
// Count all records (efficient - doesn't load records)
int totalUsers = db->count("users");

// Count with filter (loads and filters records)
auto activeFilter = [](const void *rec) -> bool {
    const User *u = (const User *)rec;
    return u->active;
};
int activeUsers = db->count("users", activeFilter);

LOG_INFO("Total: %d users, %d active", totalUsers, activeUsers);
```

### Pagination

Implement pagination for large result sets:

```cpp
const size_t PAGE_SIZE = 10;

std::vector<void *> getPage(LoDb *db, size_t pageNum) {
    // Select all with sort
    auto all = db->select("messages", LoDbFilter(), myComparator);

    // Calculate offset
    size_t offset = pageNum * PAGE_SIZE;

    // Extract page
    std::vector<void *> page;
    for (size_t i = offset; i < all.size() && i < offset + PAGE_SIZE; i++) {
        page.push_back(all[i]);
    }

    // Free records not in page
    for (size_t i = 0; i < all.size(); i++) {
        if (i < offset || i >= offset + PAGE_SIZE) {
            delete[] (uint8_t *)all[i];
        }
    }

    return page;
}
```

### Requirements

- **Arduino-style** build (as used by PlatformIO `framework = arduino`)
- **nanopb** 0.4.9+ (pulled via `library.json` when you depend on LoDB as a PlatformIO library, or supplied by your firmware)
- **LoFS** and **rweather/Crypto** (also listed in `library.json`)

## License

MIT License - see [LICENSE](LICENSE) file for details.
