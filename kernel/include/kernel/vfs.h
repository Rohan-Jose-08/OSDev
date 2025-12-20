#ifndef _KERNEL_VFS_H
#define _KERNEL_VFS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// File types
#define VFS_FILE      0
#define VFS_DIRECTORY 1

// Maximum limits
#define VFS_MAX_NAME_LEN 128
#define VFS_MAX_PATH_LEN 512
#define VFS_MAX_CHILDREN 64
#define VFS_MAX_OPEN_FILES 32

// File permissions (simplified)
#define VFS_PERM_READ  0x01
#define VFS_PERM_WRITE 0x02
#define VFS_PERM_EXEC  0x04

// VFS Node structure
typedef struct vfs_node {
    char name[VFS_MAX_NAME_LEN];           // Node name
    uint8_t type;                           // VFS_FILE or VFS_DIRECTORY
    uint8_t permissions;                    // Permission flags
    uint32_t size;                          // Size in bytes (for files)
    uint32_t inode;                         // Unique inode number
    
    // Hierarchical structure
    struct vfs_node *parent;                // Parent directory
    struct vfs_node *children[VFS_MAX_CHILDREN]; // Child nodes (for directories)
    uint32_t child_count;                   // Number of children
    
    // File data
    uint8_t *data;                          // File content (NULL for directories)
    uint32_t allocated_size;                // Allocated size for data
    
    // Timestamps (simplified)
    uint32_t created;
    uint32_t modified;
} vfs_node_t;

// File descriptor structure
typedef struct {
    vfs_node_t *node;
    uint32_t position;
    bool in_use;
} vfs_file_descriptor_t;

// VFS statistics
typedef struct {
    uint32_t total_nodes;
    uint32_t total_files;
    uint32_t total_directories;
    uint32_t total_size;
} vfs_stats_t;

// VFS initialization
void vfs_init(void);

// Node management
vfs_node_t* vfs_create_node(const char *name, uint8_t type, uint8_t permissions);
void vfs_destroy_node(vfs_node_t *node);

// Directory operations
vfs_node_t* vfs_mkdir(vfs_node_t *parent, const char *name);
int vfs_add_child(vfs_node_t *parent, vfs_node_t *child);
int vfs_remove_child(vfs_node_t *parent, const char *name);
vfs_node_t* vfs_find_child(vfs_node_t *parent, const char *name);
int vfs_list_dir(vfs_node_t *dir, vfs_node_t **list, uint32_t max_entries);

// File operations
vfs_node_t* vfs_create_file(vfs_node_t *parent, const char *name, uint8_t permissions);
int vfs_write_file(vfs_node_t *file, const uint8_t *data, uint32_t size);
int vfs_read_file(vfs_node_t *file, uint8_t *buffer, uint32_t size, uint32_t offset);
int vfs_delete(vfs_node_t *parent, const char *name);

// Path resolution
vfs_node_t* vfs_resolve_path(const char *path);
vfs_node_t* vfs_resolve_relative_path(const char *path, vfs_node_t *current_dir);
vfs_node_t* vfs_get_root(void);

// File descriptor operations
int vfs_open(const char *path);
int vfs_close(int fd);
int vfs_read(int fd, uint8_t *buffer, uint32_t size);
int vfs_write(int fd, const uint8_t *data, uint32_t size);
int vfs_seek(int fd, uint32_t position);

// Utility functions
void vfs_get_stats(vfs_stats_t *stats);
void vfs_print_tree(vfs_node_t *node, int depth);
char* vfs_get_full_path(vfs_node_t *node, char *buffer, size_t buffer_size);
void* vfs_malloc(size_t size);

// Helper functions for direct path operations (convenience wrappers)
int vfs_read_path(const char *path, uint8_t *buffer, uint32_t size, uint32_t offset);
int vfs_write_path(const char *path, const uint8_t *data, uint32_t size);
int vfs_read_path_relative(const char *path, vfs_node_t *current_dir, uint8_t *buffer, uint32_t size, uint32_t offset);
int vfs_write_path_relative(const char *path, vfs_node_t *current_dir, const uint8_t *data, uint32_t size);

#endif // _KERNEL_VFS_H
