#include <kernel/fs.h>
#include <kernel/ata.h>
#include <kernel/kmalloc.h>
#include <kernel/process.h>
#include <kernel/timer.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdio.h>

static fs_context_t fs_ctx;
static uint8_t block_buffer[FS_BLOCK_SIZE];
static uint8_t indirect_buffer[FS_BLOCK_SIZE];  // Separate buffer for indirect blocks
static uint8_t dbl_indirect_buffer[FS_BLOCK_SIZE];
static fs_inode_t inode_cache[FS_MAX_INODES];

#define FS_BLOCK_CACHE_SIZE 64

typedef struct {
    uint32_t block_num;
    uint32_t last_used;
    bool valid;
    bool dirty;
    uint8_t data[FS_BLOCK_SIZE];
} fs_block_cache_entry_t;

static fs_block_cache_entry_t block_cache[FS_BLOCK_CACHE_SIZE];
static uint32_t block_cache_tick = 1;

static void block_cache_reset(void) {
    memset(block_cache, 0, sizeof(block_cache));
    block_cache_tick = 1;
}

static fs_block_cache_entry_t *block_cache_find(uint32_t block_num) {
    for (int i = 0; i < FS_BLOCK_CACHE_SIZE; i++) {
        if (block_cache[i].valid && block_cache[i].block_num == block_num) {
            return &block_cache[i];
        }
    }
    return NULL;
}

static bool block_cache_flush_entry(fs_block_cache_entry_t *entry) {
    if (!entry || !entry->valid || !entry->dirty) {
        return true;
    }
    if (!ata_write_sectors(fs_ctx.drive, entry->block_num, 1, entry->data)) {
        return false;
    }
    entry->dirty = false;
    return true;
}

static fs_block_cache_entry_t *block_cache_get_slot(void) {
    fs_block_cache_entry_t *free_entry = NULL;
    fs_block_cache_entry_t *lru_entry = NULL;

    for (int i = 0; i < FS_BLOCK_CACHE_SIZE; i++) {
        if (!block_cache[i].valid) {
            free_entry = &block_cache[i];
            break;
        }
        if (!lru_entry || block_cache[i].last_used < lru_entry->last_used) {
            lru_entry = &block_cache[i];
        }
    }

    if (free_entry) {
        return free_entry;
    }

    if (lru_entry && !block_cache_flush_entry(lru_entry)) {
        return NULL;
    }
    if (lru_entry) {
        lru_entry->valid = false;
        lru_entry->dirty = false;
    }
    return lru_entry;
}

static bool block_cache_flush_block(uint32_t block_num) {
    fs_block_cache_entry_t *entry = block_cache_find(block_num);
    return block_cache_flush_entry(entry);
}

static void block_cache_flush_all(void) {
    for (int i = 0; i < FS_BLOCK_CACHE_SIZE; i++) {
        block_cache_flush_entry(&block_cache[i]);
    }
}

typedef struct {
    uint32_t size;
    uint8_t type;
    uint8_t permissions;
    uint16_t parent_inode;
    uint32_t blocks[FS_INODE_BLOCKS];
    char name[FS_MAX_FILENAME];
} __attribute__((packed)) fs_inode_v4_t;

static uint32_t fs_now(void) {
    return timer_get_ticks();
}

static uint16_t fs_calc_max_inodes(uint32_t inode_blocks, uint32_t inode_size) {
    if (inode_blocks == 0 || inode_size == 0) {
        return 0;
    }
    uint32_t per_block = FS_BLOCK_SIZE / inode_size;
    if (per_block == 0) {
        return 0;
    }
    uint32_t total = inode_blocks * per_block;
    if (total > FS_MAX_INODES) {
        total = FS_MAX_INODES;
    }
    return (uint16_t)total;
}

static uint16_t fs_inode_count(void) {
    return fs_ctx.max_inodes ? fs_ctx.max_inodes : FS_MAX_INODES;
}

static uint16_t fs_count_used_inodes(uint16_t max_inodes) {
    uint16_t used = 0;
    for (uint16_t i = 0; i < max_inodes; i++) {
        if (inode_cache[i].type != 0) {
            used++;
        }
    }
    return used;
}

static void fs_get_ids(uint16_t *uid, uint16_t *gid) {
    uint16_t out_uid = 0;
    uint16_t out_gid = 0;
    process_t *proc = process_current();
    if (proc) {
        out_uid = proc->uid;
        out_gid = proc->gid;
    }
    if (uid) {
        *uid = out_uid;
    }
    if (gid) {
        *gid = out_gid;
    }
}

static uint8_t fs_select_perm(const fs_inode_t *inode, uint16_t uid, uint16_t gid) {
    if (!inode) {
        return 0;
    }
    if (uid == 0) {
        return FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    }
    uint16_t perm = inode->permissions;
    if (perm <= 0x7) {
        return (uint8_t)(perm & 0x7);
    }
    uint8_t owner = (uint8_t)((perm >> 6) & 0x7);
    uint8_t group = (uint8_t)((perm >> 3) & 0x7);
    uint8_t other = (uint8_t)(perm & 0x7);
    if (uid == inode->uid) {
        return owner;
    }
    if (gid == inode->gid) {
        return group;
    }
    return other;
}

static bool fs_has_perm(const fs_inode_t *inode, uint16_t uid, uint16_t gid, uint8_t want) {
    return (fs_select_perm(inode, uid, gid) & want) == want;
}

static inline bool bitmap_test(const uint8_t *bitmap, uint32_t index) {
    return (bitmap[index / 8] & (1u << (index % 8))) != 0;
}

static inline void bitmap_set(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8] |= (uint8_t)(1u << (index % 8));
}

static inline void bitmap_clear(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8] &= (uint8_t)~(1u << (index % 8));
}

static bool block_num_to_index(uint32_t block_num, uint32_t *index_out) {
    if (block_num < fs_ctx.superblock.first_data_block) {
        return false;
    }
    uint32_t index = block_num - fs_ctx.superblock.first_data_block;
    if (index >= fs_ctx.superblock.data_blocks) {
        return false;
    }
    if (index_out) {
        *index_out = index;
    }
    return true;
}

// Initialize filesystem driver
void fs_init(void) {
    memset(&fs_ctx, 0, sizeof(fs_context_t));
    fs_ctx.mounted = false;
    fs_ctx.next_free_inode = 1;
    fs_ctx.max_inodes = FS_MAX_INODES;
    fs_ctx.superblock_dirty = false;
    fs_ctx.defer_superblock_flush = false;
    block_cache_reset();
    printf("FS: Filesystem driver initialized\n");
}

// Get filesystem context
fs_context_t* fs_get_context(void) {
    return &fs_ctx;
}

static bool read_block(uint32_t block_num, uint8_t *buffer);
static bool write_block(uint32_t block_num, const uint8_t *buffer);
static bool flush_block_bitmap_block(uint32_t bitmap_block_index);

static bool init_block_bitmap(void) {
    if (fs_ctx.block_bitmap) {
        kfree(fs_ctx.block_bitmap);
        fs_ctx.block_bitmap = NULL;
    }
    if (fs_ctx.bitmap_dirty) {
        kfree(fs_ctx.bitmap_dirty);
        fs_ctx.bitmap_dirty = NULL;
    }

    fs_ctx.bitmap_bits = fs_ctx.superblock.data_blocks;
    fs_ctx.bitmap_bytes = (fs_ctx.bitmap_bits + 7) / 8;
    if (fs_ctx.bitmap_bytes == 0) {
        fs_ctx.bitmap_bits = 0;
        fs_ctx.bitmap_dirty_bytes = 0;
        fs_ctx.next_free_block = 0;
        return false;
    }

    fs_ctx.block_bitmap = kcalloc(fs_ctx.bitmap_bytes, 1);
    if (!fs_ctx.block_bitmap) {
        fs_ctx.bitmap_bits = 0;
        fs_ctx.bitmap_bytes = 0;
        fs_ctx.bitmap_dirty_bytes = 0;
        fs_ctx.next_free_block = 0;
        return false;
    }

    fs_ctx.bitmap_dirty_bytes = fs_ctx.superblock.bitmap_blocks;
    if (fs_ctx.bitmap_dirty_bytes > 0) {
        fs_ctx.bitmap_dirty = kcalloc(fs_ctx.bitmap_dirty_bytes, 1);
    }

    fs_ctx.next_free_block = 0;
    return true;
}

static void mark_bitmap_dirty(uint32_t bitmap_block_index) {
    if (!fs_ctx.bitmap_dirty) {
        return;
    }
    if (bitmap_block_index >= fs_ctx.bitmap_dirty_bytes) {
        return;
    }
    fs_ctx.bitmap_dirty[bitmap_block_index] = 1;
}

static void flush_bitmap_dirty(void) {
    if (!fs_ctx.bitmap_dirty || fs_ctx.superblock.bitmap_blocks == 0) {
        return;
    }

    for (uint32_t i = 0; i < fs_ctx.superblock.bitmap_blocks; i++) {
        if (fs_ctx.bitmap_dirty[i]) {
            flush_block_bitmap_block(i);
            fs_ctx.bitmap_dirty[i] = 0;
        }
    }
}

static void update_next_free_block(void) {
    fs_ctx.next_free_block = 0;
    for (uint32_t i = 0; i < fs_ctx.superblock.data_blocks; i++) {
        if (!bitmap_test(fs_ctx.block_bitmap, i)) {
            fs_ctx.next_free_block = i;
            return;
        }
    }
}

static uint32_t count_used_blocks(void) {
    if (!fs_ctx.block_bitmap || fs_ctx.bitmap_bits == 0) {
        return 0;
    }

    uint32_t used = 0;
    uint32_t full_bytes = fs_ctx.bitmap_bits / 8;
    uint32_t remaining_bits = fs_ctx.bitmap_bits % 8;

    for (uint32_t i = 0; i < full_bytes; i++) {
        uint8_t value = fs_ctx.block_bitmap[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (value & (1u << bit)) {
                used++;
            }
        }
    }

    if (remaining_bits != 0) {
        uint8_t value = fs_ctx.block_bitmap[full_bytes];
        for (uint8_t bit = 0; bit < remaining_bits; bit++) {
            if (value & (1u << bit)) {
                used++;
            }
        }
    }

    return used;
}

static bool load_block_bitmap(void) {
    if (!fs_ctx.block_bitmap || fs_ctx.superblock.bitmap_blocks == 0) {
        return false;
    }

    for (uint32_t i = 0; i < fs_ctx.superblock.bitmap_blocks; i++) {
        if (!read_block(fs_ctx.superblock.bitmap_start + i, block_buffer)) {
            return false;
        }

        uint32_t offset = i * FS_BLOCK_SIZE;
        if (offset >= fs_ctx.bitmap_bytes) {
            break;
        }
        uint32_t to_copy = fs_ctx.bitmap_bytes - offset;
        if (to_copy > FS_BLOCK_SIZE) {
            to_copy = FS_BLOCK_SIZE;
        }
        memcpy(&fs_ctx.block_bitmap[offset], block_buffer, to_copy);
    }

    return true;
}

static bool flush_block_bitmap_block(uint32_t bitmap_block_index) {
    if (!fs_ctx.block_bitmap || fs_ctx.superblock.bitmap_blocks == 0) {
        return false;
    }
    if (bitmap_block_index >= fs_ctx.superblock.bitmap_blocks) {
        return false;
    }

    memset(block_buffer, 0, FS_BLOCK_SIZE);
    uint32_t offset = bitmap_block_index * FS_BLOCK_SIZE;
    if (offset < fs_ctx.bitmap_bytes) {
        uint32_t to_copy = fs_ctx.bitmap_bytes - offset;
        if (to_copy > FS_BLOCK_SIZE) {
            to_copy = FS_BLOCK_SIZE;
        }
        memcpy(block_buffer, &fs_ctx.block_bitmap[offset], to_copy);
    }

    uint32_t block_num = fs_ctx.superblock.bitmap_start + bitmap_block_index;
    if (!write_block(block_num, block_buffer)) {
        return false;
    }
    return block_cache_flush_block(block_num);
}

static void flush_block_bitmap_all(void) {
    if (!fs_ctx.block_bitmap || fs_ctx.superblock.bitmap_blocks == 0) {
        return;
    }

    for (uint32_t i = 0; i < fs_ctx.superblock.bitmap_blocks; i++) {
        flush_block_bitmap_block(i);
    }
}

static void sync_bitmap_index(uint32_t data_block_index, bool set_bit) {
    if (fs_ctx.superblock.bitmap_blocks == 0) {
        return;
    }

    uint32_t byte_index = data_block_index / 8;
    uint32_t bitmap_block_index = byte_index / FS_BLOCK_SIZE;
    if (bitmap_block_index >= fs_ctx.superblock.bitmap_blocks) {
        return;
    }

    if (fs_ctx.block_bitmap) {
        if (fs_ctx.defer_bitmap_flush && fs_ctx.bitmap_dirty) {
            mark_bitmap_dirty(bitmap_block_index);
            return;
        }
        flush_block_bitmap_block(bitmap_block_index);
        return;
    }

    uint32_t byte_in_block = byte_index % FS_BLOCK_SIZE;
    uint8_t mask = (uint8_t)(1u << (data_block_index % 8));
    uint32_t bitmap_block_num = fs_ctx.superblock.bitmap_start + bitmap_block_index;
    if (!read_block(bitmap_block_num, block_buffer)) {
        return;
    }

    if (set_bit) {
        block_buffer[byte_in_block] |= mask;
    } else {
        block_buffer[byte_in_block] &= (uint8_t)~mask;
    }

    if (write_block(bitmap_block_num, block_buffer)) {
        block_cache_flush_block(bitmap_block_num);
    }
}

static void flush_superblock(void) {
    if (!fs_ctx.superblock_dirty) {
        return;
    }

    memset(block_buffer, 0, FS_BLOCK_SIZE);
    memcpy(block_buffer, &fs_ctx.superblock, sizeof(fs_superblock_t));
    if (write_block(0, block_buffer)) {
        block_cache_flush_block(0);
    }
    fs_ctx.superblock_dirty = false;
}

static void mark_superblock_dirty(void) {
    fs_ctx.superblock_dirty = true;
    if (!fs_ctx.defer_superblock_flush) {
        flush_superblock();
    }
}

static void update_next_free_inode(void) {
    fs_ctx.next_free_inode = 0;
    uint16_t max_inodes = fs_inode_count();
    for (uint16_t i = 1; i < max_inodes; i++) {
        if (inode_cache[i].type == 0) {
            fs_ctx.next_free_inode = i;
            return;
        }
    }
}

static void mark_block_used(uint32_t block_num, uint32_t *used_blocks) {
    uint32_t index = 0;
    if (!block_num_to_index(block_num, &index)) {
        return;
    }
    if (!bitmap_test(fs_ctx.block_bitmap, index)) {
        bitmap_set(fs_ctx.block_bitmap, index);
        if (used_blocks) {
            (*used_blocks)++;
        }
    }
}

static void rebuild_block_bitmap(void) {
    if (!fs_ctx.block_bitmap) {
        return;
    }

    memset(fs_ctx.block_bitmap, 0, fs_ctx.bitmap_bytes);
    uint32_t used_blocks = 0;

    int max_inodes = fs_inode_count();
    for (int i = 0; i < max_inodes; i++) {
        if (inode_cache[i].type == 0) {
            continue;
        }

        for (int j = 0; j < FS_DIRECT_BLOCKS; j++) {
            if (inode_cache[i].blocks[j] != 0) {
                mark_block_used(inode_cache[i].blocks[j], &used_blocks);
            }
        }

        uint32_t indirect_block = inode_cache[i].blocks[FS_INDIRECT_BLOCK];
        if (indirect_block != 0) {
            mark_block_used(indirect_block, &used_blocks);
            if (read_block(indirect_block, indirect_buffer)) {
                uint32_t *indirect_blocks = (uint32_t*)indirect_buffer;
                for (int j = 0; j < FS_PTRS_PER_BLOCK; j++) {
                    if (indirect_blocks[j] != 0) {
                        mark_block_used(indirect_blocks[j], &used_blocks);
                    }
                }
            }
        }

        uint32_t dbl_block = inode_cache[i].blocks[FS_DOUBLE_INDIRECT_BLOCK];
        if (dbl_block != 0) {
            mark_block_used(dbl_block, &used_blocks);
            if (read_block(dbl_block, dbl_indirect_buffer)) {
                uint32_t *dbl_blocks = (uint32_t*)dbl_indirect_buffer;
                for (int j = 0; j < FS_PTRS_PER_BLOCK; j++) {
                    if (dbl_blocks[j] != 0) {
                        mark_block_used(dbl_blocks[j], &used_blocks);
                        if (read_block(dbl_blocks[j], indirect_buffer)) {
                            uint32_t *indirect_blocks = (uint32_t*)indirect_buffer;
                            for (int k = 0; k < FS_PTRS_PER_BLOCK; k++) {
                                if (indirect_blocks[k] != 0) {
                                    mark_block_used(indirect_blocks[k], &used_blocks);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (used_blocks > fs_ctx.superblock.data_blocks) {
        used_blocks = fs_ctx.superblock.data_blocks;
    }
    fs_ctx.superblock.free_blocks = fs_ctx.superblock.data_blocks - used_blocks;
    update_next_free_block();
}

static void free_block(uint32_t block_num) {
    uint32_t index = 0;
    if (!block_num_to_index(block_num, &index)) {
        return;
    }

    if (fs_ctx.block_bitmap) {
        if (bitmap_test(fs_ctx.block_bitmap, index)) {
            bitmap_clear(fs_ctx.block_bitmap, index);
            fs_ctx.superblock.free_blocks++;
            if (index < fs_ctx.next_free_block) {
                fs_ctx.next_free_block = index;
            }
            sync_bitmap_index(index, false);
            mark_superblock_dirty();
        }
        return;
    }

    fs_ctx.superblock.free_blocks++;
    sync_bitmap_index(index, false);
    mark_superblock_dirty();
}

// Read a block from disk
static bool read_block(uint32_t block_num, uint8_t *buffer) {
    fs_block_cache_entry_t *entry = block_cache_find(block_num);
    if (entry) {
        entry->last_used = block_cache_tick++;
        memcpy(buffer, entry->data, FS_BLOCK_SIZE);
        return true;
    }

    fs_block_cache_entry_t *slot = block_cache_get_slot();
    if (!slot) {
        return false;
    }
    if (!ata_read_sectors(fs_ctx.drive, block_num, 1, slot->data)) {
        return false;
    }
    slot->block_num = block_num;
    slot->valid = true;
    slot->dirty = false;
    slot->last_used = block_cache_tick++;
    memcpy(buffer, slot->data, FS_BLOCK_SIZE);
    return true;
}

// Write a block to disk
static bool write_block(uint32_t block_num, const uint8_t *buffer) {
    fs_block_cache_entry_t *entry = block_cache_find(block_num);
    if (!entry) {
        entry = block_cache_get_slot();
        if (!entry) {
            return false;
        }
        entry->block_num = block_num;
        entry->valid = true;
        entry->dirty = false;
    }
    memcpy(entry->data, buffer, FS_BLOCK_SIZE);
    entry->dirty = true;
    entry->last_used = block_cache_tick++;
    return true;
}

// Load inode table from disk
static bool load_inode_table(void) {
    for (uint32_t i = 0; i < fs_ctx.superblock.inode_blocks; i++) {
        if (!read_block(1 + i, block_buffer)) {
            return false;
        }
        
        // Copy inodes from block to cache
        int inodes_per_block = FS_BLOCK_SIZE / sizeof(fs_inode_t);
        uint16_t max_inodes = fs_inode_count();
        for (int j = 0; j < inodes_per_block && (i * inodes_per_block + j) < max_inodes; j++) {
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
        uint16_t max_inodes = fs_inode_count();
        for (int j = 0; j < inodes_per_block && (i * inodes_per_block + j) < max_inodes; j++) {
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

static bool load_inode_table_v4(void) {
    uint16_t old_max = fs_calc_max_inodes(fs_ctx.superblock.inode_blocks,
                                          sizeof(fs_inode_v4_t));
    if (old_max == 0) {
        return false;
    }
    memset(inode_cache, 0, sizeof(inode_cache));
    fs_ctx.max_inodes = old_max;
    uint32_t now = fs_now();

    for (uint32_t i = 0; i < fs_ctx.superblock.inode_blocks; i++) {
        if (!read_block(1 + i, block_buffer)) {
            return false;
        }

        int inodes_per_block = FS_BLOCK_SIZE / sizeof(fs_inode_v4_t);
        for (int j = 0; j < inodes_per_block; j++) {
            uint32_t idx = i * inodes_per_block + (uint32_t)j;
            if (idx >= old_max) {
                break;
            }
            fs_inode_v4_t *old = (fs_inode_v4_t *)(block_buffer + j * sizeof(fs_inode_v4_t));
            fs_inode_t *inode = &inode_cache[idx];
            memset(inode, 0, sizeof(*inode));
            inode->size = old->size;
            inode->type = old->type;
            uint16_t perm = (uint16_t)(old->permissions & 0x7);
            inode->permissions = (uint16_t)((perm << 6) | (perm << 3) | perm);
            inode->parent_inode = old->parent_inode;
            inode->uid = 0;
            inode->gid = 0;
            inode->atime = now;
            inode->mtime = now;
            inode->ctime = now;
            memcpy(inode->blocks, old->blocks, sizeof(old->blocks));
            strncpy(inode->name, old->name, FS_MAX_FILENAME - 1);
            inode->name[FS_MAX_FILENAME - 1] = '\0';
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
    uint32_t bitmap_blocks = 0;
    uint32_t first_data_block = 0;
    uint32_t data_blocks = 0;

    for (;;) {
        first_data_block = 1 + inode_blocks + bitmap_blocks;
        if (device->size_sectors <= first_data_block) {
            data_blocks = 0;
            break;
        }
        data_blocks = device->size_sectors - first_data_block;
        uint32_t bits_per_block = FS_BLOCK_SIZE * 8;
        uint32_t needed_bitmap_blocks = (data_blocks + bits_per_block - 1) / bits_per_block;
        if (needed_bitmap_blocks == bitmap_blocks) {
            break;
        }
        bitmap_blocks = needed_bitmap_blocks;
    }
    
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
    uint16_t max_inodes = fs_calc_max_inodes(inode_blocks, sizeof(fs_inode_t));
    if (max_inodes == 0) {
        printf("FS: Inode table too small\n");
        return false;
    }
    sb.free_inodes = max_inodes - 1;  // Reserve inode 0 for root
    sb.first_data_block = first_data_block;
    sb.bitmap_start = 1 + inode_blocks;
    sb.bitmap_blocks = bitmap_blocks;
    
    // Write superblock
    memset(block_buffer, 0, FS_BLOCK_SIZE);
    memcpy(block_buffer, &sb, sizeof(fs_superblock_t));
    if (!ata_write_sectors(drive, 0, 1, block_buffer)) {
        printf("FS: Failed to write superblock\n");
        return false;
    }
    
    fs_ctx.max_inodes = max_inodes;

    // Initialize inode table
    memset(inode_cache, 0, sizeof(inode_cache));
    
    // Create root directory inode (inode 0)
    uint32_t now = fs_now();
    inode_cache[0].type = 2;  // Directory
    inode_cache[0].permissions = 0777;
    inode_cache[0].size = 0;
    inode_cache[0].uid = 0;
    inode_cache[0].gid = 0;
    inode_cache[0].atime = now;
    inode_cache[0].mtime = now;
    inode_cache[0].ctime = now;
    strcpy(inode_cache[0].name, "/");
    
    // Write inode table
    for (uint32_t i = 0; i < inode_blocks; i++) {
        memset(block_buffer, 0, FS_BLOCK_SIZE);
        int inodes_per_block = FS_BLOCK_SIZE / sizeof(fs_inode_t);
        for (int j = 0; j < inodes_per_block && (i * inodes_per_block + j) < max_inodes; j++) {
            memcpy(&block_buffer[j * sizeof(fs_inode_t)],
                   &inode_cache[i * inodes_per_block + j],
                   sizeof(fs_inode_t));
        }
        if (!ata_write_sectors(drive, 1 + i, 1, block_buffer)) {
            printf("FS: Failed to write inode table\n");
            return false;
        }
    }

    // Initialize block bitmap
    for (uint32_t i = 0; i < bitmap_blocks; i++) {
        memset(block_buffer, 0, FS_BLOCK_SIZE);
        if (!ata_write_sectors(drive, sb.bitmap_start + i, 1, block_buffer)) {
            printf("FS: Failed to write block bitmap\n");
            return false;
        }
    }
    
    printf("FS: Format complete (%u inodes, %u data blocks)\n",
           (unsigned int)max_inodes, data_blocks);
    return true;
}

// Mount a filesystem
bool fs_mount(uint8_t drive) {
    ata_device_t *device = ata_get_device(drive);
    if (!device) {
        printf("FS: Invalid drive %u\n", drive);
        return false;
    }

    fs_ctx.drive = drive;
    block_cache_reset();
    
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

    bool upgrade_v4 = false;
    if (fs_ctx.superblock.version != FS_VERSION) {
        if (fs_ctx.superblock.version == 4) {
            upgrade_v4 = true;
        } else {
            printf("FS: Unsupported filesystem version %u\n", fs_ctx.superblock.version);
            return false;
        }
    }

    if (upgrade_v4) {
        printf("FS: Upgrading filesystem from v4 to v%u...\n", FS_VERSION);
        if (!load_inode_table_v4()) {
            printf("FS: Failed to load v4 inode table\n");
            return false;
        }
        uint16_t new_max = fs_calc_max_inodes(fs_ctx.superblock.inode_blocks, sizeof(fs_inode_t));
        if (new_max == 0) {
            printf("FS: Inode table too small for upgrade\n");
            return false;
        }
        if (new_max < fs_ctx.max_inodes) {
            for (uint16_t i = new_max; i < fs_ctx.max_inodes; i++) {
                if (inode_cache[i].type != 0) {
                    printf("FS: Upgrade requires format (inode overflow)\n");
                    return false;
                }
            }
        }
        fs_ctx.max_inodes = new_max;
        fs_ctx.superblock.version = FS_VERSION;
        uint16_t used = fs_count_used_inodes(fs_ctx.max_inodes);
        fs_ctx.superblock.free_inodes =
            (fs_ctx.max_inodes > used) ? (fs_ctx.max_inodes - used) : 0;
        mark_superblock_dirty();
        if (!save_inode_table()) {
            printf("FS: Failed to write upgraded inode table\n");
            return false;
        }
        flush_superblock();
    } else {
        fs_ctx.max_inodes = fs_calc_max_inodes(fs_ctx.superblock.inode_blocks, sizeof(fs_inode_t));
        if (fs_ctx.max_inodes == 0) {
            printf("FS: Inode table invalid\n");
            return false;
        }
        // Load inode table
        if (!load_inode_table()) {
            printf("FS: Failed to load inode table\n");
            return false;
        }
        if (fs_ctx.superblock.free_inodes > fs_ctx.max_inodes) {
            fs_ctx.superblock.free_inodes = fs_ctx.max_inodes ? (fs_ctx.max_inodes - 1) : 0;
            mark_superblock_dirty();
        }
    }

    if (init_block_bitmap()) {
        bool superblock_dirty = false;
        if (!load_block_bitmap()) {
            printf("FS: Failed to load block bitmap, rebuilding\n");
            rebuild_block_bitmap();
            flush_block_bitmap_all();
            superblock_dirty = true;
        } else {
            uint32_t used_blocks = count_used_blocks();
            if (used_blocks <= fs_ctx.superblock.data_blocks) {
                uint32_t new_free = fs_ctx.superblock.data_blocks - used_blocks;
                if (new_free != fs_ctx.superblock.free_blocks) {
                    fs_ctx.superblock.free_blocks = new_free;
                    superblock_dirty = true;
                }
            }
            update_next_free_block();
        }
        if (superblock_dirty) {
            fs_ctx.superblock_dirty = true;
            flush_superblock();
        }
    } else {
        printf("FS: Block bitmap unavailable, using slow allocator\n");
    }

    update_next_free_inode();

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

    // Flush block bitmap before writing superblock
    flush_bitmap_dirty();
    flush_block_bitmap_all();

    fs_ctx.defer_superblock_flush = false;
    flush_superblock();
    block_cache_flush_all();
    block_cache_reset();

    if (fs_ctx.block_bitmap) {
        kfree(fs_ctx.block_bitmap);
        fs_ctx.block_bitmap = NULL;
    }
    if (fs_ctx.bitmap_dirty) {
        kfree(fs_ctx.bitmap_dirty);
        fs_ctx.bitmap_dirty = NULL;
    }
    fs_ctx.bitmap_bytes = 0;
    fs_ctx.bitmap_bits = 0;
    fs_ctx.bitmap_dirty_bytes = 0;
    fs_ctx.next_free_block = 0;
    fs_ctx.next_free_inode = 1;
    fs_ctx.max_inodes = FS_MAX_INODES;
    fs_ctx.defer_bitmap_flush = false;
    fs_ctx.superblock_dirty = false;
    fs_ctx.defer_superblock_flush = false;

    fs_ctx.mounted = false;
    printf("FS: Unmounted\n");
}

// Find a free inode
static int find_free_inode(void) {
    int start = fs_ctx.next_free_inode ? fs_ctx.next_free_inode : 1;
    int idx = start;
    int max_inodes = fs_inode_count();

    for (int scanned = 0; scanned < (max_inodes - 1); scanned++) {
        if (inode_cache[idx].type == 0) {
            int next = idx + 1;
            if (next >= max_inodes) {
                next = 1;
            }
            fs_ctx.next_free_inode = (uint16_t)next;
            return idx;
        }

        idx++;
        if (idx >= max_inodes) {
            idx = 1;
        }
    }

    fs_ctx.next_free_inode = 0;
    return -1;
}

// Find a free data block by scanning inodes (slow path)
static int find_free_block_slow(void) {
    if (fs_ctx.superblock.free_blocks == 0) {
        return -1;
    }
    static uint8_t temp_buffer[FS_BLOCK_SIZE];
    static uint8_t temp_buffer2[FS_BLOCK_SIZE];

    for (uint32_t i = 0; i < fs_ctx.superblock.data_blocks; i++) {
        uint32_t block_num = fs_ctx.superblock.first_data_block + i;

        // Check if block is used by any inode (including direct and indirect blocks)
        bool used = false;
        int max_inodes = fs_inode_count();
        for (int j = 0; j < max_inodes; j++) {
            if (inode_cache[j].type != 0) {
                for (int k = 0; k < FS_DIRECT_BLOCKS; k++) {
                    if (inode_cache[j].blocks[k] == block_num) {
                        used = true;
                        break;
                    }
                }

                if (!used && inode_cache[j].blocks[FS_INDIRECT_BLOCK] == block_num) {
                    used = true;
                }

                if (!used && inode_cache[j].blocks[FS_INDIRECT_BLOCK] != 0) {
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
                if (!used && inode_cache[j].blocks[FS_DOUBLE_INDIRECT_BLOCK] == block_num) {
                    used = true;
                }
                if (!used && inode_cache[j].blocks[FS_DOUBLE_INDIRECT_BLOCK] != 0) {
                    if (read_block(inode_cache[j].blocks[FS_DOUBLE_INDIRECT_BLOCK], temp_buffer)) {
                        uint32_t *dbl_blocks = (uint32_t*)temp_buffer;
                        for (int k = 0; k < FS_PTRS_PER_BLOCK && !used; k++) {
                            if (dbl_blocks[k] == block_num) {
                                used = true;
                                break;
                            }
                            if (dbl_blocks[k] != 0 &&
                                read_block(dbl_blocks[k], temp_buffer2)) {
                                uint32_t *indirect_blocks = (uint32_t*)temp_buffer2;
                                for (int m = 0; m < FS_PTRS_PER_BLOCK; m++) {
                                    if (indirect_blocks[m] == block_num) {
                                        used = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (used) {
                break;
            }
        }

        if (!used) {
            return block_num;
        }
    }

    return -1;
}

static int allocate_block(void) {
    if (fs_ctx.superblock.free_blocks == 0) {
        return -1;
    }

    if (!fs_ctx.block_bitmap) {
        int block = find_free_block_slow();
        if (block < 0) {
            return -1;
        }
        fs_ctx.superblock.free_blocks--;
        mark_superblock_dirty();
        uint32_t index = 0;
        if (block_num_to_index((uint32_t)block, &index)) {
            sync_bitmap_index(index, true);
        }
        return block;
    }

    uint32_t total = fs_ctx.superblock.data_blocks;
    uint32_t start = fs_ctx.next_free_block;
    for (uint32_t i = 0; i < total; i++) {
        uint32_t index = start + i;
        if (index >= total) {
            index -= total;
        }
        if (!bitmap_test(fs_ctx.block_bitmap, index)) {
            bitmap_set(fs_ctx.block_bitmap, index);
            fs_ctx.superblock.free_blocks--;
            sync_bitmap_index(index, true);
            mark_superblock_dirty();
            fs_ctx.next_free_block = index + 1;
            if (fs_ctx.next_free_block >= total) {
                fs_ctx.next_free_block = 0;
            }
            return fs_ctx.superblock.first_data_block + index;
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
    int max_inodes = fs_inode_count();
    for (int i = 0; i < max_inodes; i++) {
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

    uint16_t uid = 0;
    uint16_t gid = 0;
    fs_get_ids(&uid, &gid);
    if (!fs_has_perm(&inode_cache[parent_inode], uid, gid,
                     FS_PERM_WRITE | FS_PERM_EXEC)) {
        return -1;
    }
    
    // Find free inode
    int inode_num = find_free_inode();
    if (inode_num < 0) {
        return -3;  // No free inodes
    }
    
    // Initialize inode
    memset(&inode_cache[inode_num], 0, sizeof(fs_inode_t));
    inode_cache[inode_num].type = 1;  // File
    inode_cache[inode_num].permissions = 0666;
    inode_cache[inode_num].size = 0;
    inode_cache[inode_num].parent_inode = parent_inode;
    inode_cache[inode_num].uid = uid;
    inode_cache[inode_num].gid = gid;
    uint32_t now = fs_now();
    inode_cache[inode_num].atime = now;
    inode_cache[inode_num].mtime = now;
    inode_cache[inode_num].ctime = now;
    inode_cache[parent_inode].mtime = now;
    inode_cache[parent_inode].ctime = now;
    strncpy(inode_cache[inode_num].name, filename, FS_MAX_FILENAME - 1);
    
    fs_ctx.superblock.free_inodes--;
    mark_superblock_dirty();
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

    uint16_t uid = 0;
    uint16_t gid = 0;
    fs_get_ids(&uid, &gid);
    if (!fs_has_perm(&inode_cache[parent_inode], uid, gid,
                     FS_PERM_WRITE | FS_PERM_EXEC)) {
        return -1;
    }
    
    // Find free inode
    int inode_num = find_free_inode();
    if (inode_num < 0) {
        return -3;  // No free inodes
    }
    
    // Initialize inode
    memset(&inode_cache[inode_num], 0, sizeof(fs_inode_t));
    inode_cache[inode_num].type = 2;  // Directory
    inode_cache[inode_num].permissions = 0777;
    inode_cache[inode_num].size = 0;
    inode_cache[inode_num].parent_inode = parent_inode;
    inode_cache[inode_num].uid = uid;
    inode_cache[inode_num].gid = gid;
    uint32_t now = fs_now();
    inode_cache[inode_num].atime = now;
    inode_cache[inode_num].mtime = now;
    inode_cache[inode_num].ctime = now;
    inode_cache[parent_inode].mtime = now;
    inode_cache[parent_inode].ctime = now;
    strncpy(inode_cache[inode_num].name, dirname, FS_MAX_FILENAME - 1);
    
    fs_ctx.superblock.free_inodes--;
    mark_superblock_dirty();
    save_inode_table();
    
    return inode_num;
}

// Get the block number for a given file block index (supports indirect blocks)
static int get_file_block(fs_inode_t *inode, uint32_t block_index, bool allocate) {
    // Direct blocks
    if (block_index < FS_DIRECT_BLOCKS) {
        if (inode->blocks[block_index] == 0 && allocate) {
            int block = allocate_block();
            if (block < 0) return -1;
            inode->blocks[block_index] = block;
        }
        return inode->blocks[block_index];
    }
    
    // Indirect blocks
    block_index -= FS_DIRECT_BLOCKS;
    if (block_index < FS_PTRS_PER_BLOCK) {
        // Allocate indirect block if needed
        if (inode->blocks[FS_INDIRECT_BLOCK] == 0) {
            if (!allocate) return 0;
            int block = allocate_block();
            if (block < 0) return -1;
            inode->blocks[FS_INDIRECT_BLOCK] = block;
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
            int block = allocate_block();
            if (block < 0) return -1;
            indirect_blocks[block_index] = block;
            // Write back indirect block
            if (!write_block(inode->blocks[FS_INDIRECT_BLOCK], indirect_buffer)) {
                return -1;
            }
        }

        return indirect_blocks[block_index];
    }

    // Double-indirect blocks
    block_index -= FS_PTRS_PER_BLOCK;
    if (block_index >= FS_PTRS_PER_BLOCK * FS_PTRS_PER_BLOCK) {
        return -1;  // Beyond maximum file size
    }

    uint32_t dbl_index = block_index / FS_PTRS_PER_BLOCK;
    uint32_t indirect_index = block_index % FS_PTRS_PER_BLOCK;

    if (inode->blocks[FS_DOUBLE_INDIRECT_BLOCK] == 0) {
        if (!allocate) return 0;
        int block = allocate_block();
        if (block < 0) return -1;
        inode->blocks[FS_DOUBLE_INDIRECT_BLOCK] = block;
        memset(dbl_indirect_buffer, 0, FS_BLOCK_SIZE);
        if (!write_block(block, dbl_indirect_buffer)) return -1;
    }

    if (!read_block(inode->blocks[FS_DOUBLE_INDIRECT_BLOCK], dbl_indirect_buffer)) {
        return -1;
    }

    uint32_t *dbl_blocks = (uint32_t*)dbl_indirect_buffer;
    if (dbl_blocks[dbl_index] == 0) {
        if (!allocate) return 0;
        int block = allocate_block();
        if (block < 0) return -1;
        dbl_blocks[dbl_index] = block;
        memset(indirect_buffer, 0, FS_BLOCK_SIZE);
        if (!write_block(block, indirect_buffer)) return -1;
        if (!write_block(inode->blocks[FS_DOUBLE_INDIRECT_BLOCK], dbl_indirect_buffer)) {
            return -1;
        }
    }

    if (!read_block(dbl_blocks[dbl_index], indirect_buffer)) {
        return -1;
    }

    uint32_t *indirect_blocks = (uint32_t*)indirect_buffer;
    if (indirect_blocks[indirect_index] == 0 && allocate) {
        int block = allocate_block();
        if (block < 0) return -1;
        indirect_blocks[indirect_index] = block;
        if (!write_block(dbl_blocks[dbl_index], indirect_buffer)) {
            return -1;
        }
    }

    return indirect_blocks[indirect_index];
}

// Free all blocks used by a file
static void free_file_blocks(fs_inode_t *inode) {
    if (!inode || inode->type != 1) return;
    
    // Free direct blocks
    for (int i = 0; i < FS_DIRECT_BLOCKS; i++) {
        if (inode->blocks[i] != 0) {
            free_block(inode->blocks[i]);
            inode->blocks[i] = 0;
        }
    }
    
    // Free indirect block and all blocks it points to
    if (inode->blocks[FS_INDIRECT_BLOCK] != 0) {
        // Read indirect block to get list of data blocks
        if (read_block(inode->blocks[FS_INDIRECT_BLOCK], indirect_buffer)) {
            uint32_t *indirect_blocks = (uint32_t*)indirect_buffer;
            for (int i = 0; i < FS_PTRS_PER_BLOCK; i++) {
                if (indirect_blocks[i] != 0) {
                    free_block(indirect_blocks[i]);
                    indirect_blocks[i] = 0;  // Clear the entry
                }
            }
            // Write back the cleared indirect block to disk
            write_block(inode->blocks[FS_INDIRECT_BLOCK], indirect_buffer);
        }
        // Free the indirect block itself
        free_block(inode->blocks[FS_INDIRECT_BLOCK]);
        inode->blocks[FS_INDIRECT_BLOCK] = 0;
    }

    // Free double-indirect block and all blocks it points to
    if (inode->blocks[FS_DOUBLE_INDIRECT_BLOCK] != 0) {
        if (read_block(inode->blocks[FS_DOUBLE_INDIRECT_BLOCK], dbl_indirect_buffer)) {
            uint32_t *dbl_blocks = (uint32_t*)dbl_indirect_buffer;
            for (int i = 0; i < FS_PTRS_PER_BLOCK; i++) {
                if (dbl_blocks[i] == 0) {
                    continue;
                }
                if (read_block(dbl_blocks[i], indirect_buffer)) {
                    uint32_t *indirect_blocks = (uint32_t*)indirect_buffer;
                    for (int j = 0; j < FS_PTRS_PER_BLOCK; j++) {
                        if (indirect_blocks[j] != 0) {
                            free_block(indirect_blocks[j]);
                            indirect_blocks[j] = 0;
                        }
                    }
                    write_block(dbl_blocks[i], indirect_buffer);
                }
                free_block(dbl_blocks[i]);
                dbl_blocks[i] = 0;
            }
            write_block(inode->blocks[FS_DOUBLE_INDIRECT_BLOCK], dbl_indirect_buffer);
        }
        free_block(inode->blocks[FS_DOUBLE_INDIRECT_BLOCK]);
        inode->blocks[FS_DOUBLE_INDIRECT_BLOCK] = 0;
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

    uint16_t uid = 0;
    uint16_t gid = 0;
    fs_get_ids(&uid, &gid);
    if (!fs_has_perm(inode, uid, gid, FS_PERM_WRITE)) {
        return -1;
    }
    
    // For simplicity, only support writing from beginning
    if (offset != 0) {
        return -1;
    }
    
    fs_ctx.defer_superblock_flush = true;

    // Free existing blocks before writing new content
    free_file_blocks(inode);
    
    // Save inode table immediately after freeing so allocation sees the cleared blocks
    save_inode_table();
    
    // Calculate blocks needed
    uint32_t blocks_needed = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    uint32_t max_blocks = FS_DIRECT_BLOCKS + FS_PTRS_PER_BLOCK +
                          (FS_PTRS_PER_BLOCK * FS_PTRS_PER_BLOCK);
    if (blocks_needed > max_blocks) {
        blocks_needed = max_blocks;
    }
    
    // Write data block by block (safer than batching for now)
    uint32_t written = 0;
    static uint8_t write_buffer[FS_BLOCK_SIZE];

    fs_ctx.defer_bitmap_flush = true;
    
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
        
        // Write single block (cached)
        if (!write_block(block_num, write_buffer)) {
            break;
        }
        
        written += to_write;
    }
    
    fs_ctx.defer_bitmap_flush = false;
    flush_bitmap_dirty();

    inode->size = written;
    uint32_t now = fs_now();
    inode->mtime = now;
    inode->ctime = now;
    
    // Save inode table after all writes complete
    save_inode_table();

    fs_ctx.defer_superblock_flush = false;
    flush_superblock();
    
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

    uint16_t uid = 0;
    uint16_t gid = 0;
    fs_get_ids(&uid, &gid);
    if (!fs_has_perm(inode, uid, gid, FS_PERM_READ)) {
        return -1;
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
    uint32_t max_blocks = FS_DIRECT_BLOCKS + FS_PTRS_PER_BLOCK +
                          (FS_PTRS_PER_BLOCK * FS_PTRS_PER_BLOCK);
    
    static uint8_t read_buffer[FS_BLOCK_SIZE];
    
    for (uint32_t i = start_block; i < max_blocks && read_bytes < size; i++) {
        int block_num = get_file_block(inode, i, false);
        if (block_num <= 0) {
            break;
        }
        
        if (!read_block(block_num, read_buffer)) {
            // Retry once in case the device was briefly busy.
            if (!read_block(block_num, read_buffer)) {
                return -1;
            }
        }
        
        uint32_t to_read = FS_BLOCK_SIZE - block_offset;
        if (to_read > size - read_bytes) {
            to_read = size - read_bytes;
        }
        
        memcpy(buffer + read_bytes, read_buffer + block_offset, to_read);
        read_bytes += to_read;
        block_offset = 0;  // Only first block has offset
    }
    
    if (read_bytes > 0) {
        inode->atime = fs_now();
        save_inode_table();
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

    uint16_t uid = 0;
    uint16_t gid = 0;
    fs_get_ids(&uid, &gid);
    if (!fs_has_perm(&inode_cache[dir_inode], uid, gid, FS_PERM_READ)) {
        return -1;
    }
    
    // List all entries in this directory
    int count = 0;
    int max_inodes = fs_inode_count();
    for (int i = 0; i < max_inodes && count < max_entries; i++) {
        if (inode_cache[i].type != 0 && inode_cache[i].parent_inode == dir_inode) {
            entries[count].inode = i;
            strncpy(entries[count].name, inode_cache[i].name, FS_MAX_FILENAME);
            count++;
        }
    }
    
    inode_cache[dir_inode].atime = fs_now();
    save_inode_table();
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

    uint16_t uid = 0;
    uint16_t gid = 0;
    fs_get_ids(&uid, &gid);
    if (!fs_has_perm(&inode_cache[inode_num], uid, gid, FS_PERM_READ)) {
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

    // Determine parent directory for permission check
    char components[16][FS_MAX_FILENAME];
    int count = parse_path(path, components, 16);
    if (count == 0) {
        return false;
    }
    int parent_inode = 0;
    if (count > 1) {
        for (int i = 0; i < count - 1; i++) {
            if (inode_cache[parent_inode].type != 2) {
                return false;
            }
            int next = find_inode_in_dir(parent_inode, components[i]);
            if (next < 0) {
                return false;
            }
            parent_inode = next;
        }
    }
    uint16_t uid = 0;
    uint16_t gid = 0;
    fs_get_ids(&uid, &gid);
    if (!fs_has_perm(&inode_cache[parent_inode], uid, gid,
                     FS_PERM_WRITE | FS_PERM_EXEC)) {
        return false;
    }
    
    int inode_num = find_inode_by_name(path);
    if (inode_num < 0) {
        return false;  // File not found
    }
    
    fs_inode_t *inode = &inode_cache[inode_num];
    fs_ctx.defer_superblock_flush = true;
    
    // Use free_file_blocks to properly free all blocks (including indirect)
    if (inode->type == 1) {
        free_file_blocks(inode);
    } else {
        // For directories, just free direct blocks (no indirect support yet)
        for (int i = 0; i < FS_DIRECT_BLOCKS; i++) {
            if (inode->blocks[i] != 0) {
                free_block(inode->blocks[i]);
                inode->blocks[i] = 0;
            }
        }
    }
    
    // Clear the inode
    memset(inode, 0, sizeof(fs_inode_t));
    fs_ctx.superblock.free_inodes++;
    if (inode_num > 0 && (inode_num < fs_ctx.next_free_inode || fs_ctx.next_free_inode == 0)) {
        fs_ctx.next_free_inode = (uint16_t)inode_num;
    }
    mark_superblock_dirty();

    uint32_t now = fs_now();
    inode_cache[parent_inode].mtime = now;
    inode_cache[parent_inode].ctime = now;
    
    // Save changes
    save_inode_table();
    
    fs_ctx.defer_superblock_flush = false;
    flush_superblock();
    
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

    uint16_t uid = 0;
    uint16_t gid = 0;
    fs_get_ids(&uid, &gid);
    if (!fs_has_perm(&inode_cache[parent_inode_num], uid, gid,
                     FS_PERM_WRITE | FS_PERM_EXEC)) {
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
    inode->ctime = fs_now();
    inode_cache[parent_inode_num].mtime = inode->ctime;
    inode_cache[parent_inode_num].ctime = inode->ctime;
    
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
