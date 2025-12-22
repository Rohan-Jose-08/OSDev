#include <kernel/fs.h>
#include <kernel/ata.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdio.h>

static fs_context_t fs_ctx;
static uint8_t block_buffer[FS_BLOCK_SIZE];
static uint8_t indirect_buffer[FS_BLOCK_SIZE];  // Separate buffer for indirect blocks
static fs_inode_t inode_cache[FS_MAX_INODES];

// Initialize filesystem driver
void fs_init(void) {
    memset(&fs_ctx, 0, sizeof(fs_context_t));
    fs_ctx.mounted = false;
    printf("FS: Filesystem driver initialized\n");
}

// Get filesystem context
fs_context_t* fs_get_context(void) {
    return &fs_ctx;
}

// Read a block from disk
static bool read_block(uint32_t block_num, uint8_t *buffer) {
    return ata_read_sectors(fs_ctx.drive, block_num, 1, buffer);
}

// Write a block to disk
static bool write_block(uint32_t block_num, const uint8_t *buffer) {
    return ata_write_sectors(fs_ctx.drive, block_num, 1, buffer);
}

// Load inode table from disk
static bool load_inode_table(void) {
    for (uint32_t i = 0; i < fs_ctx.superblock.inode_blocks; i++) {
        if (!read_block(1 + i, block_buffer)) {
            return false;
        }
        
        // Copy inodes from block to cache
        int inodes_per_block = FS_BLOCK_SIZE / sizeof(fs_inode_t);
        for (int j = 0; j < inodes_per_block && (i * inodes_per_block + j) < FS_MAX_INODES; j++) {
            memcpy(&inode_cache[i * inodes_per_block + j],
                   &block_buffer[j * sizeof(fs_inode_t)],
                   sizeof(fs_inode_t));
        }
    }
    return true;
}

// Save inode table to disk
static bool save_inode_table(void) {
    for (uint32_t i = 0; i < fs_ctx.superblock.inode_blocks; i++) {
        memset(block_buffer, 0, FS_BLOCK_SIZE);
        
        // Copy inodes from cache to block
        int inodes_per_block = FS_BLOCK_SIZE / sizeof(fs_inode_t);
        for (int j = 0; j < inodes_per_block && (i * inodes_per_block + j) < FS_MAX_INODES; j++) {
            memcpy(&block_buffer[j * sizeof(fs_inode_t)],
                   &inode_cache[i * inodes_per_block + j],
                   sizeof(fs_inode_t));
        }
        
        if (!write_block(1 + i, block_buffer)) {
            return false;
        }
    }
    return true;
}

// Format a disk with the filesystem
bool fs_format(uint8_t drive) {
    ata_device_t *device = ata_get_device(drive);
    if (!device) {
        printf("FS: Invalid drive %u\n", drive);
        return false;
    }
    
    printf("FS: Formatting drive %u...\n", drive);
    
    // Calculate filesystem layout
    uint32_t inode_blocks = (FS_MAX_INODES * sizeof(fs_inode_t) + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    uint32_t first_data_block = 1 + inode_blocks;
    uint32_t data_blocks = device->size_sectors - first_data_block;
    
    // Create superblock
    fs_superblock_t sb;
    memset(&sb, 0, sizeof(fs_superblock_t));
    sb.magic = FS_MAGIC;
    sb.version = FS_VERSION;
    sb.block_size = FS_BLOCK_SIZE;
    sb.total_blocks = device->size_sectors;
    sb.inode_blocks = inode_blocks;
    sb.data_blocks = data_blocks;
    sb.free_blocks = data_blocks;
    sb.free_inodes = FS_MAX_INODES - 1;  // Reserve inode 0 for root
    sb.first_data_block = first_data_block;
    
    // Write superblock
    memset(block_buffer, 0, FS_BLOCK_SIZE);
    memcpy(block_buffer, &sb, sizeof(fs_superblock_t));
    if (!ata_write_sectors(drive, 0, 1, block_buffer)) {
        printf("FS: Failed to write superblock\n");
        return false;
    }
    
    // Initialize inode table
    memset(inode_cache, 0, sizeof(inode_cache));
    
    // Create root directory inode (inode 0)
    inode_cache[0].type = 2;  // Directory
    inode_cache[0].permissions = 0x07;  // rwx
    inode_cache[0].size = 0;
    strcpy(inode_cache[0].name, "/");
    
    // Write inode table
    for (uint32_t i = 0; i < inode_blocks; i++) {
        memset(block_buffer, 0, FS_BLOCK_SIZE);
        int inodes_per_block = FS_BLOCK_SIZE / sizeof(fs_inode_t);
        for (int j = 0; j < inodes_per_block && (i * inodes_per_block + j) < FS_MAX_INODES; j++) {
            memcpy(&block_buffer[j * sizeof(fs_inode_t)],
                   &inode_cache[i * inodes_per_block + j],
                   sizeof(fs_inode_t));
        }
        if (!ata_write_sectors(drive, 1 + i, 1, block_buffer)) {
            printf("FS: Failed to write inode table\n");
            return false;
        }
    }
    
    printf("FS: Format complete (%u inodes, %u data blocks)\n", FS_MAX_INODES, data_blocks);
    return true;
}

// Mount a filesystem
bool fs_mount(uint8_t drive) {
    ata_device_t *device = ata_get_device(drive);
    if (!device) {
        printf("FS: Invalid drive %u\n", drive);
        return false;
    }
    
    // Read superblock
    if (!ata_read_sectors(drive, 0, 1, block_buffer)) {
        printf("FS: Failed to read superblock\n");
        return false;
    }
    
    memcpy(&fs_ctx.superblock, block_buffer, sizeof(fs_superblock_t));
    
    // Verify magic number
    if (fs_ctx.superblock.magic != FS_MAGIC) {
        printf("FS: Invalid filesystem magic (0x%x)\n", fs_ctx.superblock.magic);
        return false;
    }
    
    // Load inode table
    if (!load_inode_table()) {
        printf("FS: Failed to load inode table\n");
        return false;
    }
    
    fs_ctx.drive = drive;
    fs_ctx.mounted = true;
    
    printf("FS: Mounted drive %u (%u free blocks, %u free inodes)\n",
           drive, fs_ctx.superblock.free_blocks, fs_ctx.superblock.free_inodes);
    
    return true;
}

// Unmount the filesystem
void fs_unmount(void) {
    if (!fs_ctx.mounted) {
        return;
    }
    
    // Save inode table
    save_inode_table();
    
    // Write superblock
    memset(block_buffer, 0, FS_BLOCK_SIZE);
    memcpy(block_buffer, &fs_ctx.superblock, sizeof(fs_superblock_t));
    write_block(0, block_buffer);
    
    fs_ctx.mounted = false;
    printf("FS: Unmounted\n");
}

// Find a free inode
static int find_free_inode(void) {
    for (int i = 1; i < FS_MAX_INODES; i++) {  // Skip root (0)
        if (inode_cache[i].type == 0) {
            return i;
        }
    }
    return -1;
}

// Find a free data block
static int find_free_block(void) {
    if (fs_ctx.superblock.free_blocks == 0) {
        return -1;
    }
    
    // Simple allocation: just use next available
    for (uint32_t i = 0; i < fs_ctx.superblock.data_blocks; i++) {
        uint32_t block_num = fs_ctx.superblock.first_data_block + i;
        
        // Check if block is used by any inode (including direct and indirect blocks)
        bool used = false;
        for (int j = 0; j < FS_MAX_INODES; j++) {
            if (inode_cache[j].type != 0) {
                // Check direct blocks
                for (int k = 0; k < FS_DIRECT_BLOCKS; k++) {
                    if (inode_cache[j].blocks[k] == block_num) {
                        used = true;
                        break;
                    }
                }
                
                // Check indirect block pointer itself
                if (!used && inode_cache[j].blocks[FS_INDIRECT_BLOCK] == block_num) {
                    used = true;
                }
                
                // Check blocks referenced by indirect block
                if (!used && inode_cache[j].blocks[FS_INDIRECT_BLOCK] != 0) {
                    // Read the indirect block
                    static uint8_t temp_buffer[FS_BLOCK_SIZE];
                    if (read_block(inode_cache[j].blocks[FS_INDIRECT_BLOCK], temp_buffer)) {
                        uint32_t *indirect_blocks = (uint32_t*)temp_buffer;
                        for (int k = 0; k < FS_PTRS_PER_BLOCK; k++) {
                            if (indirect_blocks[k] == block_num) {
                                used = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (used) break;
        }
        
        if (!used) {
            return block_num;
        }
    }
    
    return -1;
}

// Parse path into components
static int parse_path(const char *path, char components[][FS_MAX_FILENAME], int max_components) {
    int count = 0;
    const char *start = path;
    
    // Skip leading slashes
    while (*start == '/') start++;
    
    if (*start == '\0') {
        return 0;  // Root directory
    }
    
    while (*start && count < max_components) {
        const char *end = start;
        while (*end && *end != '/') end++;
        
        int len = end - start;
        if (len > 0 && len < FS_MAX_FILENAME) {
            memcpy(components[count], start, len);
            components[count][len] = '\0';
            count++;
        }
        
        start = end;
        while (*start == '/') start++;
    }
    
    return count;
}

// Find inode by name in a specific directory
static int find_inode_in_dir(int parent_inode, const char *name) {
    for (int i = 0; i < FS_MAX_INODES; i++) {
        if (inode_cache[i].type != 0 && 
            inode_cache[i].parent_inode == parent_inode &&
            strcmp(inode_cache[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Resolve a path to an inode number
static int resolve_path(const char *path) {
    if (!path || *path == '\0') {
        return -1;
    }
    
    // Root directory
    if (strcmp(path, "/") == 0) {
        return 0;
    }
    
    char components[16][FS_MAX_FILENAME];
    int count = parse_path(path, components, 16);
    
    if (count == 0) {
        return 0;  // Root
    }
    
    // Traverse the path
    int current_inode = 0;  // Start at root
    for (int i = 0; i < count; i++) {
        // Make sure current inode is a directory
        if (inode_cache[current_inode].type != 2) {
            return -1;  // Not a directory
        }
        
        // Find the next component
        int next = find_inode_in_dir(current_inode, components[i]);
        if (next < 0) {
            return -1;  // Not found
        }
        current_inode = next;
    }
    
    return current_inode;
}

// Find inode by path (legacy wrapper for simple filenames in root)
static int find_inode_by_name(const char *name) {
    // If it's a simple name (no slashes), search in root
    if (strchr(name, '/') == NULL) {
        return find_inode_in_dir(0, name);
    }
    // Otherwise resolve the full path
    return resolve_path(name);
}

// Create a file
int fs_create_file(const char *path) {
    if (!fs_ctx.mounted) {
        return -1;
    }
    
    // Parse the path to get parent directory and filename
    char components[16][FS_MAX_FILENAME];
    int count = parse_path(path, components, 16);
    
    if (count == 0) {
        return -1;  // Can't create root
    }
    
    // Find parent directory
    int parent_inode = 0;  // Default to root
    if (count > 1) {
        // Navigate to parent directory
        for (int i = 0; i < count - 1; i++) {
            if (inode_cache[parent_inode].type != 2) {
                return -1;  // Parent is not a directory
            }
            int next = find_inode_in_dir(parent_inode, components[i]);
            if (next < 0) {
                return -1;  // Parent directory doesn't exist
            }
            parent_inode = next;
        }
    }
    
    // Check if file already exists in parent directory
    const char *filename = components[count - 1];
    if (find_inode_in_dir(parent_inode, filename) >= 0) {
        return -2;  // Already exists
    }
    
    // Find free inode
    int inode_num = find_free_inode();
    if (inode_num < 0) {
        return -3;  // No free inodes
    }
    
    // Initialize inode
    memset(&inode_cache[inode_num], 0, sizeof(fs_inode_t));
    inode_cache[inode_num].type = 1;  // File
    inode_cache[inode_num].permissions = 0x06;  // rw-
    inode_cache[inode_num].size = 0;
    inode_cache[inode_num].parent_inode = parent_inode;
    strncpy(inode_cache[inode_num].name, filename, FS_MAX_FILENAME - 1);
    
    fs_ctx.superblock.free_inodes--;
    save_inode_table();
    
    return inode_num;
}

// Create a directory
int fs_create_dir(const char *path) {
    if (!fs_ctx.mounted) {
        return -1;
    }
    
    // Parse the path to get parent directory and directory name
    char components[16][FS_MAX_FILENAME];
    int count = parse_path(path, components, 16);
    
    if (count == 0) {
        return -1;  // Can't create root
    }
    
    // Find parent directory
    int parent_inode = 0;  // Default to root
    if (count > 1) {
        for (int i = 0; i < count - 1; i++) {
            if (inode_cache[parent_inode].type != 2) {
                return -1;
            }
            int next = find_inode_in_dir(parent_inode, components[i]);
            if (next < 0) {
                return -1;
            }
            parent_inode = next;
        }
    }
    
    // Check if directory already exists
    const char *dirname = components[count - 1];
    if (find_inode_in_dir(parent_inode, dirname) >= 0) {
        return -2;  // Already exists
    }
    
    // Find free inode
    int inode_num = find_free_inode();
    if (inode_num < 0) {
        return -3;  // No free inodes
    }
    
    // Initialize inode
    memset(&inode_cache[inode_num], 0, sizeof(fs_inode_t));
    inode_cache[inode_num].type = 2;  // Directory
    inode_cache[inode_num].permissions = 0x07;  // rwx
    inode_cache[inode_num].size = 0;
    inode_cache[inode_num].parent_inode = parent_inode;
    strncpy(inode_cache[inode_num].name, dirname, FS_MAX_FILENAME - 1);
    
    fs_ctx.superblock.free_inodes--;
    save_inode_table();
    
    return inode_num;
}

// Get the block number for a given file block index (supports indirect blocks)
static int get_file_block(fs_inode_t *inode, uint32_t block_index, bool allocate) {
    // Direct blocks
    if (block_index < FS_DIRECT_BLOCKS) {
        if (inode->blocks[block_index] == 0 && allocate) {
            int block = find_free_block();
            if (block < 0) return -1;
            inode->blocks[block_index] = block;
            fs_ctx.superblock.free_blocks--;
        }
        return inode->blocks[block_index];
    }
    
    // Indirect blocks
    block_index -= FS_DIRECT_BLOCKS;
    if (block_index >= FS_PTRS_PER_BLOCK) {
        return -1;  // Beyond maximum file size
    }
    
    // Allocate indirect block if needed
    if (inode->blocks[FS_INDIRECT_BLOCK] == 0) {
        if (!allocate) return 0;
        int block = find_free_block();
        if (block < 0) return -1;
        inode->blocks[FS_INDIRECT_BLOCK] = block;
        fs_ctx.superblock.free_blocks--;
        // Initialize indirect block to zeros
        memset(indirect_buffer, 0, FS_BLOCK_SIZE);
        if (!write_block(block, indirect_buffer)) return -1;
    }
    
    // Read indirect block
    if (!read_block(inode->blocks[FS_INDIRECT_BLOCK], indirect_buffer)) {
        return -1;
    }
    
    uint32_t *indirect_blocks = (uint32_t*)indirect_buffer;
    if (indirect_blocks[block_index] == 0 && allocate) {
        int block = find_free_block();
        if (block < 0) return -1;
        indirect_blocks[block_index] = block;
        fs_ctx.superblock.free_blocks--;
        // Write back indirect block
        if (!write_block(inode->blocks[FS_INDIRECT_BLOCK], indirect_buffer)) {
            return -1;
        }
    }
    
    return indirect_blocks[block_index];
}

// Free all blocks used by a file
static void free_file_blocks(fs_inode_t *inode) {
    if (!inode || inode->type != 1) return;
    
    // Free direct blocks
    for (int i = 0; i < FS_DIRECT_BLOCKS; i++) {
        if (inode->blocks[i] != 0) {
            inode->blocks[i] = 0;
            fs_ctx.superblock.free_blocks++;
        }
    }
    
    // Free indirect block and all blocks it points to
    if (inode->blocks[FS_INDIRECT_BLOCK] != 0) {
        // Read indirect block to get list of data blocks
        if (read_block(inode->blocks[FS_INDIRECT_BLOCK], indirect_buffer)) {
            uint32_t *indirect_blocks = (uint32_t*)indirect_buffer;
            for (int i = 0; i < FS_PTRS_PER_BLOCK; i++) {
                if (indirect_blocks[i] != 0) {
                    indirect_blocks[i] = 0;  // Clear the entry
                    fs_ctx.superblock.free_blocks++;
                }
            }
            // Write back the cleared indirect block to disk
            write_block(inode->blocks[FS_INDIRECT_BLOCK], indirect_buffer);
        }
        // Free the indirect block itself
        inode->blocks[FS_INDIRECT_BLOCK] = 0;
        fs_ctx.superblock.free_blocks++;
    }
    
    inode->size = 0;
}

// Write to a file
int fs_write_file(const char *path, const uint8_t *buffer, uint32_t size, uint32_t offset) {
    if (!fs_ctx.mounted) {
        return -1;
    }
    
    int inode_num = find_inode_by_name(path);
    if (inode_num < 0) {
        return -1;
    }
    
    fs_inode_t *inode = &inode_cache[inode_num];
    if (inode->type != 1) {
        return -1;  // Not a file
    }
    
    // For simplicity, only support writing from beginning
    if (offset != 0) {
        return -1;
    }
    
    // Free existing blocks before writing new content
    free_file_blocks(inode);
    
    // Save inode table immediately after freeing so find_free_block sees the cleared blocks
    save_inode_table();
    
    // Save superblock after freeing to persist free_blocks count
    memset(block_buffer, 0, FS_BLOCK_SIZE);
    memcpy(block_buffer, &fs_ctx.superblock, sizeof(fs_superblock_t));
    write_block(0, block_buffer);
    
    // Calculate blocks needed
    uint32_t blocks_needed = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    uint32_t max_blocks = FS_DIRECT_BLOCKS + FS_PTRS_PER_BLOCK;  // 11 direct + 128 indirect = 139 blocks = ~71KB
    if (blocks_needed > max_blocks) {
        blocks_needed = max_blocks;
    }
    
    // Write data block by block (safer than batching for now)
    uint32_t written = 0;
    static uint8_t write_buffer[FS_BLOCK_SIZE];
    
    for (uint32_t i = 0; i < blocks_needed && written < size; i++) {
        // Get or allocate block
        int block_num = get_file_block(inode, i, true);
        if (block_num <= 0) {
            break;  // No more blocks available
        }
        
        uint32_t to_write = size - written;
        if (to_write > FS_BLOCK_SIZE) {
            to_write = FS_BLOCK_SIZE;
        }
        
        // Prepare block with padding if needed
        memset(write_buffer, 0, FS_BLOCK_SIZE);
        memcpy(write_buffer, buffer + written, to_write);
        
        // Write single block
        if (!ata_write_sectors(fs_ctx.drive, block_num, 1, write_buffer)) {
            break;
        }
        
        written += to_write;
    }
    
    inode->size = written;
    
    // Save inode table after all writes complete
    save_inode_table();
    
    // Update superblock to reflect freed/allocated blocks
    memset(block_buffer, 0, FS_BLOCK_SIZE);
    memcpy(block_buffer, &fs_ctx.superblock, sizeof(fs_superblock_t));
    write_block(0, block_buffer);
    
    return written;
}

// Read from a file
int fs_read_file(const char *path, uint8_t *buffer, uint32_t size, uint32_t offset) {
    if (!fs_ctx.mounted) {
        return -1;
    }
    
    int inode_num = find_inode_by_name(path);
    if (inode_num < 0) {
        return -1;
    }
    
    fs_inode_t *inode = &inode_cache[inode_num];
    if (inode->type != 1) {
        return -1;  // Not a file
    }
    
    // Adjust size if needed
    if (offset >= inode->size) {
        return 0;
    }
    if (offset + size > inode->size) {
        size = inode->size - offset;
    }
    
    // Read data
    uint32_t start_block = offset / FS_BLOCK_SIZE;
    uint32_t block_offset = offset % FS_BLOCK_SIZE;
    uint32_t read_bytes = 0;
    uint32_t max_blocks = FS_DIRECT_BLOCKS + FS_PTRS_PER_BLOCK;
    
    static uint8_t read_buffer[FS_BLOCK_SIZE];
    
    for (uint32_t i = start_block; i < max_blocks && read_bytes < size; i++) {
        int block_num = get_file_block(inode, i, false);
        if (block_num <= 0) {
            break;
        }
        
        if (!read_block(block_num, read_buffer)) {
            return -1;
        }
        
        uint32_t to_read = FS_BLOCK_SIZE - block_offset;
        if (to_read > size - read_bytes) {
            to_read = size - read_bytes;
        }
        
        memcpy(buffer + read_bytes, read_buffer + block_offset, to_read);
        read_bytes += to_read;
        block_offset = 0;  // Only first block has offset
    }
    
    return read_bytes;
}

// List directory entries
int fs_list_dir(const char *path, fs_dirent_t *entries, int max_entries) {
    if (!fs_ctx.mounted) {
        return -1;
    }
    
    // Resolve the directory path
    int dir_inode = resolve_path(path);
    if (dir_inode < 0) {
        return -1;  // Directory not found
    }
    
    // Make sure it's a directory
    if (inode_cache[dir_inode].type != 2) {
        return -1;  // Not a directory
    }
    
    // List all entries in this directory
    int count = 0;
    for (int i = 0; i < FS_MAX_INODES && count < max_entries; i++) {
        if (inode_cache[i].type != 0 && inode_cache[i].parent_inode == dir_inode) {
            entries[count].inode = i;
            strncpy(entries[count].name, inode_cache[i].name, FS_MAX_FILENAME);
            count++;
        }
    }
    
    return count;
}

// Get file info
bool fs_stat(const char *path, fs_inode_t *inode) {
    if (!fs_ctx.mounted) {
        return false;
    }
    
    int inode_num = find_inode_by_name(path);
    if (inode_num < 0) {
        return false;
    }
    
    memcpy(inode, &inode_cache[inode_num], sizeof(fs_inode_t));
    return true;
}

// Delete a file
bool fs_delete(const char *path) {
    if (!fs_ctx.mounted) {
        return false;
    }
    
    int inode_num = find_inode_by_name(path);
    if (inode_num < 0) {
        return false;  // File not found
    }
    
    fs_inode_t *inode = &inode_cache[inode_num];
    
    // Use free_file_blocks to properly free all blocks (including indirect)
    if (inode->type == 1) {
        free_file_blocks(inode);
    } else {
        // For directories, just free direct blocks (no indirect support yet)
        for (int i = 0; i < FS_DIRECT_BLOCKS; i++) {
            if (inode->blocks[i] != 0) {
                inode->blocks[i] = 0;
                fs_ctx.superblock.free_blocks++;
            }
        }
    }
    
    // Clear the inode
    memset(inode, 0, sizeof(fs_inode_t));
    fs_ctx.superblock.free_inodes++;
    
    // Save changes
    save_inode_table();
    
    // Update superblock
    memset(block_buffer, 0, FS_BLOCK_SIZE);
    memcpy(block_buffer, &fs_ctx.superblock, sizeof(fs_superblock_t));
    write_block(0, block_buffer);
    
    return true;
}

// Rename a file or directory
bool fs_rename(const char *old_path, const char *new_name) {
    if (!fs_ctx.mounted) {
        return false;
    }
    
    // Find the inode
    int inode_num = find_inode_by_name(old_path);
    if (inode_num < 0) {
        return false;  // File not found
    }
    
    // Check if new name is too long
    if (strlen(new_name) >= FS_MAX_FILENAME) {
        return false;
    }
    
    // Check if new name contains invalid characters
    if (strchr(new_name, '/') != NULL) {
        return false;
    }
    
    // Get parent directory path to check for duplicates
    char parent_path[128];
    const char *last_slash = strrchr(old_path, '/');
    if (last_slash && last_slash != old_path) {
        int len = last_slash - old_path;
        memcpy(parent_path, old_path, len);
        parent_path[len] = '\0';
    } else {
        strcpy(parent_path, "/");
    }
    
    // Get parent inode
    int parent_inode_num = resolve_path(parent_path);
    if (parent_inode_num < 0) {
        return false;
    }
    
    // Check if a file with new name already exists in same directory
    int existing = find_inode_in_dir(parent_inode_num, new_name);
    if (existing >= 0 && existing != inode_num) {
        return false;  // Name already exists
    }
    
    // Update the inode's name
    fs_inode_t *inode = &inode_cache[inode_num];
    strncpy(inode->name, new_name, FS_MAX_FILENAME - 1);
    inode->name[FS_MAX_FILENAME - 1] = '\0';
    
    // Save changes
    save_inode_table();
    
    return true;
}

// Get free blocks count
uint32_t fs_get_free_blocks(void) {
    if (!fs_ctx.mounted) {
        return 0;
    }
    return fs_ctx.superblock.free_blocks;
}
