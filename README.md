# LoDB - Micro Database for Meshtastic

**A Meshtastic firmware plugin providing a synchronous, protobuf-based database**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

LoDB provides CRUD operations with powerful SELECT queries supporting filtering, sorting, and limiting, all using Protocol Buffers for data serialization and onboard file storage.

## Features

- **Synchronous Design**: All operations complete immediately with results returned directly
- **Protocol Buffers**: Type-safe data storage using nanopb for efficient serialization
- **CRUD Operations**: Create, Read, Update, and Delete records with simple API calls
- **Powerful Queries**: SELECT with filtering, sorting, and limiting in a single operation
- **Deterministic UUIDs**: Generate consistent UUIDs from strings or auto-generate unique ones
- **Thread-Safe**: Built-in locking for concurrent access
- **Filesystem-Based**: Simple, human-readable file structure on any filesystem
- **Memory Efficient**: Designed for resource-constrained embedded systems

## Installation

LoDB is a Meshtastic plugin that is automatically discovered and integrated by the Meshtastic Plugin Manager (MPM). To install LoDB:

1. **Install the Meshtastic Plugin Manager:**

```bash
pip install mesh-plugin-manager
```

2. **Install LoDB:**

```bash
cd /path/to/meshtastic/firmware
mpm install lodb
```

3. **Build and flash:**

The Meshtastic Plugin Manager automatically discovers the plugin, generates protobuf files (if the plugin uses any), and integrates it into the build. Simply build and flash as usual:

```bash
pio run -e esp32 -t upload
```

**Note:** For detailed information about Meshtastic plugin development, see the [Plugin Development Guide](/path/to/meshtastic/src/plugins/README.md).

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

**Note:** Protobuf generation is handled automatically by the Meshtastic Plugin Manager.

**Initialize the database:**

```cpp
#include "LoDB.h"
#include "myschema.pb.h"

LoDb *db = new LoDb("myapp");
db->registerTable("users", &User_msg, sizeof(User));
```

**Note:** The include path is simply `"LoDB.h"` because the plugin's `src/` directory is automatically added to the compiler's include path.

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

### Storage Model

LoDB uses a filesystem-based storage model with a clear directory hierarchy:

```
/lodb/
  └── <database_name>/
      └── <table_name>/
          ├── <uuid_hex>.pr
          ├── <uuid_hex>.pr
          └── ...
```

Each record is stored as a separate `.pr` (protobuf) file named with its 16-character hexadecimal UUID.

**Example file structure:**

```
/lodb/lobbs/
  ├── users/
  │   ├── a1b2c3d4e5f67890.pr
  │   └── 1234567890abcdef.pr
  ├── sessions/
  │   └── 00000000deadbeef.pr
  └── mail/
      ├── f0e1d2c3b4a59687.pr
      └── 9876543210fedcba.pr
```

### Thread Safety

All filesystem operations use `LockGuard(spiLock)` to ensure thread-safe access across concurrent operations.

### UUID System

LoDB uses 64-bit unsigned integers as UUIDs:

- **Deterministic UUIDs**: Generated from strings using SHA256 with optional salt (useful for lookups by key)
- **Auto-generated UUIDs**: Created from timestamp + random value for unique records
- **Hex Format**: UUIDs are formatted as 16-character hex strings for filenames

### Table Registration

```cpp
LoBBSDal::LoBBSDal(uint32_t hostNodeId) : hostNodeId(hostNodeId)
{
    // Initialize LoDB database
    db = new LoDb("lobbs");

    // Register tables with their protobuf descriptors
    db->registerTable("users", &meshtastic_LoBBSUser_msg, sizeof(meshtastic_LoBBSUser));
    db->registerTable("sessions", &meshtastic_LoBBSSession_msg, sizeof(meshtastic_LoBBSSession));
    db->registerTable("mail", &meshtastic_LoBBSMail_msg, sizeof(meshtastic_LoBBSMail));
}
```

### Deterministic UUIDs for Lookups

```cpp
bool LoBBSDal::loadUserByUsername(const char *username, meshtastic_LoBBSUser *user)
{
    // Normalize username to lowercase for case-insensitive lookup
    char normalized[LOBBS_USERNAME_BUFFER_SIZE];
    normalizeUsername(username, normalized);

    // Convert username to UUID with host node ID as salt
    lodb_uuid_t userUuid = lodb_new_uuid(normalized, hostNodeId);

    // Look up user by deterministic UUID
    LoDbError err = db->get("users", userUuid, user);
    return (err == LODB_OK);
}
```

### User Creation with Session Management

```cpp
bool LoBBSDal::createUser(const char *username, const char *password, uint32_t nodeId)
{
    // Normalize username for case-insensitive storage
    char normalized[LOBBS_USERNAME_BUFFER_SIZE];
    normalizeUsername(username, normalized);

    // Calculate deterministic UUID
    lodb_uuid_t userUuid = lodb_new_uuid(normalized, hostNodeId);

    // Create and populate user record
    meshtastic_LoBBSUser user = meshtastic_LoBBSUser_init_zero;
    strncpy(user.username, username, sizeof(user.username) - 1);
    user.uuid = userUuid;
    user.password_hash.size = 32;
    hashPassword(password, user.password_hash.bytes);

    // Insert into database
    LoDbError err = db->insert("users", userUuid, &user);
    if (err != LODB_OK) {
        LOG_ERROR("Failed to create user: %s", username);
        return false;
    }

    LOG_INFO("Created user: %s", username);

    // Log in the user (create session)
    return loginUser(username, nodeId);
}
```

### Filtering with Lambdas

```cpp
// Get all mail messages for a specific user
std::vector<void *> LoBBSDal::getMailForUser(uint64_t userUuid, uint32_t offset, uint32_t limit)
{
    // Build filter lambda that captures userUuid
    auto mail_filter = [userUuid](const void *rec) -> bool {
        const meshtastic_LoBBSMail *m = (const meshtastic_LoBBSMail *)rec;
        return m->to_user_uuid == userUuid;
    };

    // Comparator for sorting by timestamp descending (newest first)
    auto comparator = [](const void *a, const void *b) -> int {
        const meshtastic_LoBBSMail *m1 = (const meshtastic_LoBBSMail *)a;
        const meshtastic_LoBBSMail *m2 = (const meshtastic_LoBBSMail *)b;
        // Reverse order: newer (larger timestamp) first
        if (m2->timestamp > m1->timestamp) return 1;
        if (m2->timestamp < m1->timestamp) return -1;
        return 0;
    };

    // Execute select with filter and sort
    auto allMail = db->select("mail", mail_filter, comparator);

    // Manual offset/limit handling
    std::vector<void *> result;
    for (size_t i = offset; i < allMail.size() && i < offset + limit; i++) {
        result.push_back(allMail[i]);
    }

    // Free records not included in result
    for (size_t i = 0; i < allMail.size(); i++) {
        if (i < offset || i >= offset + limit) {
            delete[] (uint8_t *)allMail[i];
        }
    }

    return result;
}
```

### User Directory with Search

```cpp
// From LoBBSModule.cpp - /users command with optional filter
char *filterStr = strtok(NULL, " ");

// Build filter lambda for username matching (captures filterStr)
auto username_filter = [filterStr](const void *rec) -> bool {
    const meshtastic_LoBBSUser *u = (const meshtastic_LoBBSUser *)rec;
    return !filterStr || !filterStr[0] || stristr(u->username, filterStr) != nullptr;
};

// Comparator for alphabetical sorting
auto comparator = [](const void *a, const void *b) -> int {
    const meshtastic_LoBBSUser *u1 = (const meshtastic_LoBBSUser *)a;
    const meshtastic_LoBBSUser *u2 = (const meshtastic_LoBBSUser *)b;
    return strcasecmp(u1->username, u2->username);
};

// Execute synchronous select with filter and sort
auto users = db->select("users", username_filter, comparator);

// Use results
for (size_t i = 0; i < users.size(); i++) {
    const meshtastic_LoBBSUser *u = (const meshtastic_LoBBSUser *)users[i];
    // ... process user
}

// Free allocated records
for (auto *userPtr : users) {
    delete[] (uint8_t *)userPtr;
}
```

### Update Pattern (Read-Modify-Write)

```cpp
bool LoBBSDal::markMailAsRead(uint64_t mailUuid)
{
    // Load the mail record
    meshtastic_LoBBSMail mail = meshtastic_LoBBSMail_init_zero;
    LoDbError err = db->get("mail", mailUuid, &mail);
    if (err != LODB_OK) {
        LOG_WARN("Mail not found: " LODB_UUID_FMT, LODB_UUID_ARGS(mailUuid));
        return false;
    }

    // Modify the field
    mail.read = true;

    // Update in database
    err = db->update("mail", mailUuid, &mail);
    if (err != LODB_OK) {
        LOG_ERROR("Failed to mark mail as read: " LODB_UUID_FMT, LODB_UUID_ARGS(mailUuid));
        return false;
    }

    return true;
}
```

## Examples

LoDB was built for LoBBS. Check out the [LoBBS repo](https://github.com/MeshEnvy/lobbs) for a complete application that uses LoDB.

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
LoDb(const char *db_name);
```

Create a new database instance with namespace `db_name`.

**Parameters:**

- `db_name`: Database name (creates `/lodb/{db_name}/` directory)

**Example:**

```cpp
LoDb *db = new LoDb("myapp");
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

**Memory Management:** Caller must free each record with `delete[] (uint8_t *)rec`

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

// Process and free results
for (auto *ptr : top10) {
    const User *user = (const User *)ptr;
    // ... use user
    delete[] (uint8_t *)ptr;  // IMPORTANT: Free memory
}
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
for (auto *ptr : results) {
    delete[] (uint8_t *)ptr;
}

// Or use a helper pattern
auto cleanup = [&results]() {
    for (auto *ptr : results) {
        delete[] (uint8_t *)ptr;
    }
    results.clear();
};

// ... use results ...
cleanup();
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

- Python 3.x
- nanopb 0.4.9+ (included in Meshtastic firmware)
- Meshtastic 2.7 or higher

## License

MIT License - see [LICENSE](LICENSE) file for details.
