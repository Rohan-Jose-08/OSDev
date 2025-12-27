#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include <stdint.h>
#include <stdbool.h>

// Simple filesystem layout:
// Sector 0: Superblock
// Sector 1-N: Inode table
// Sector N+1-K: Block bitmap
// Sector K+1-M: Data blocks

#define FS_MAGIC 0x524F4853  // "ROHS" - RohanOS
#define FS_VERSION 5
#define FS_BLOCK_SIZE 512
#define FS_MAX_INODES 256
#define FS_MAX_FILENAME 28
#define FS_INODE_BLOCKS 50  // Total block pointers per inode
#define FS_DIRECT_BLOCKS 48  // Direct block pointers (blocks[0-47])
#define FS_INDIRECT_BLOCK 48  // Indirect block pointer (blocks[48])
#define FS_DOUBLE_INDIRECT_BLOCK 49  // Double-indirect block pointer (blocks[49])
#define FS_PTRS_PER_BLOCK (FS_BLOCK_SIZE / sizeof(uint32_t))  // 128 pointers per block

// Filesystem superblock (sector 0)
typedef struct {
    uint32_t magic;              // Magic number for validation
    uint32_t version;            // Filesystem version
    uint32_t block_size;         // Block size (512 bytes)
    uint32_t total_blocks;       // Total blocks on disk
    uint32_t inode_blocks;       // Number of blocks for inode table
    uint32_t data_blocks;        // Number of data blocks
    uint32_t free_blocks;        // Number of free data blocks
    uint32_t free_inodes;        // Number of free inodes
    uint32_t first_data_block;   // First data block number
    uint32_t bitmap_start;       // First bitmap block number
    uint32_t bitmap_blocks;      // Number of bitmap blocks
    uint8_t reserved[468];       // Pad to 512 bytes
} __attribute__((packed)) fs_superblock_t;

// Permission bits (rwx in lowest 9 bits, legacy values <= 0x7 apply to all).
#define FS_PERM_READ  0x4
#define FS_PERM_WRITE 0x2
#define FS_PERM_EXEC  0x1

// Filesystem inode
typedef struct {
    uint32_t size;               // File size in bytes
    uint16_t permissions;        // Permission bits
    uint8_t type;                // 0 = free, 1 = file, 2 = directory
    uint8_t reserved;            // Padding
    uint16_t parent_inode;       // Parent directory inode (0 for root)
    uint16_t uid;                // Owner UID
    uint16_t gid;                // Owner GID
    uint32_t atime;              // Access time (ticks)
    uint32_t mtime;              // Modify time (ticks)
    uint32_t ctime;              // Status change time (ticks)
    uint32_t blocks[FS_INODE_BLOCKS]; // blocks[0-47]=direct, blocks[48]=indirect, blocks[49]=double-indirect
    char name[FS_MAX_FILENAME];  // Filename
} __attribute__((packed)) fs_inode_t;

// Directory entry
typedef struct {
    uint32_t inode;              // Inode number
    char name[FS_MAX_FILENAME];  // Entry name
} __attribute__((packed)) fs_dirent_t;

// Filesystem context
typedef struct {
    uint8_t drive;               // ATA drive number
    fs_superblock_t superblock;  // Cached superblock
    bool mounted;                // Is filesystem mounted?
    bool superblock_dirty;       // Track pending superblock updates
    bool defer_superblock_flush; // Defer superblock flush during bulk writes
    uint8_t *block_bitmap;       // In-memory bitmap for data blocks
    uint32_t bitmap_bytes;       // Size of bitmap in bytes
    uint32_t bitmap_bits;        // Number of data blocks tracked
    uint32_t next_free_block;    // Hint index for next free block search
    uint8_t *bitmap_dirty;       // Dirty flags per bitmap block
    uint32_t bitmap_dirty_bytes; // Size of dirty flag array
    bool defer_bitmap_flush;     // Defer bitmap flush during bulk writes
    uint16_t next_free_inode;    // Hint index for next free inode search
    uint16_t max_inodes;         // Maximum inodes supported by on-disk layout
} fs_context_t;

// Initialize filesystem driver
void fs_init(void);

// Format a disk with the filesystem
bool fs_format(uint8_t drive);

// Mount a filesystem
bool fs_mount(uint8_t drive);

// Unmount the filesystem
void fs_unmount(void);

// Create a file
int fs_create_file(const char *path);

// Create a directory
int fs_create_dir(const char *path);

// Delete a file or directory
bool fs_delete(const char *path);

// Rename a file or directory
bool fs_rename(const char *old_path, const char *new_name);

// Read from a file
int fs_read_file(const char *path, uint8_t *buffer, uint32_t size, uint32_t offset);

// Write to a file
int fs_write_file(const char *path, const uint8_t *buffer, uint32_t size, uint32_t offset);

// List directory entries
int fs_list_dir(const char *path, fs_dirent_t *entries, int max_entries);

// Get file/directory info
bool fs_stat(const char *path, fs_inode_t *inode);

// Get free blocks count
uint32_t fs_get_free_blocks(void);

// Get filesystem context
fs_context_t* fs_get_context(void);

#endif // _KERNEL_FS_H
