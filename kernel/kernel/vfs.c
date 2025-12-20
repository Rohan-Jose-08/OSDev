#include <kernel/vfs.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Global VFS state
static vfs_node_t *vfs_root = NULL;
static uint32_t next_inode = 1;
static vfs_file_descriptor_t file_descriptors[VFS_MAX_OPEN_FILES];

// Helper function to allocate memory (using a simple approach)
void* vfs_malloc(size_t size) {
    // In a real OS, this would use the kernel's memory allocator
    // For now, we'll use a static pool or implement a simple allocator
    // TODO: Replace with proper kmalloc when available
    static uint8_t memory_pool[1024 * 1024]; // 1MB pool
    static size_t pool_offset = 0;
    
    if (pool_offset + size > sizeof(memory_pool)) {
        return NULL; // Out of memory
    }
    
    void *ptr = &memory_pool[pool_offset];
    pool_offset += size;
    return ptr;
}

// Initialize the VFS
void vfs_init(void) {
    // Create root directory
    vfs_root = vfs_create_node("/", VFS_DIRECTORY, VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC);
    if (!vfs_root) {
        terminal_writestring("VFS: Failed to create root directory\n");
        return;
    }
    vfs_root->parent = NULL; // Root has no parent
    
    // Initialize file descriptors
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        file_descriptors[i].in_use = false;
        file_descriptors[i].node = NULL;
        file_descriptors[i].position = 0;
    }
    
    terminal_writestring("VFS: Initialized with root directory\n");
}

// Create a new VFS node
vfs_node_t* vfs_create_node(const char *name, uint8_t type, uint8_t permissions) {
    if (!name || strlen(name) >= VFS_MAX_NAME_LEN) {
        return NULL;
    }
    
    vfs_node_t *node = (vfs_node_t*)vfs_malloc(sizeof(vfs_node_t));
    if (!node) {
        return NULL;
    }
    
    // Initialize node
    strncpy(node->name, name, VFS_MAX_NAME_LEN - 1);
    node->name[VFS_MAX_NAME_LEN - 1] = '\0';
    node->type = type;
    node->permissions = permissions;
    node->size = 0;
    node->inode = next_inode++;
    node->parent = NULL;
    node->child_count = 0;
    node->data = NULL;
    node->allocated_size = 0;
    node->created = 0; // TODO: Use real timestamp
    node->modified = 0;
    
    // Initialize children array
    for (uint32_t i = 0; i < VFS_MAX_CHILDREN; i++) {
        node->children[i] = NULL;
    }
    
    return node;
}

// Destroy a VFS node
void vfs_destroy_node(vfs_node_t *node) {
    if (!node) {
        return;
    }
    
    // If it's a directory, destroy all children first
    if (node->type == VFS_DIRECTORY) {
        for (uint32_t i = 0; i < node->child_count; i++) {
            vfs_destroy_node(node->children[i]);
        }
    }
    
    // Free file data if allocated
    if (node->data) {
        // In a real OS, we'd free this memory
        // For now, we're using a static pool, so no action needed
    }
    
    // Note: We don't free the node itself as we're using a static pool
}

// Create a directory
vfs_node_t* vfs_mkdir(vfs_node_t *parent, const char *name) {
    if (!parent || parent->type != VFS_DIRECTORY) {
        return NULL;
    }
    
    // Check if child already exists
    if (vfs_find_child(parent, name)) {
        return NULL; // Already exists
    }
    
    vfs_node_t *dir = vfs_create_node(name, VFS_DIRECTORY, VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC);
    if (!dir) {
        return NULL;
    }
    
    if (vfs_add_child(parent, dir) != 0) {
        vfs_destroy_node(dir);
        return NULL;
    }
    
    return dir;
}

// Add a child node to a parent directory
int vfs_add_child(vfs_node_t *parent, vfs_node_t *child) {
    if (!parent || !child || parent->type != VFS_DIRECTORY) {
        return -1;
    }
    
    if (parent->child_count >= VFS_MAX_CHILDREN) {
        return -1; // Directory is full
    }
    
    parent->children[parent->child_count] = child;
    parent->child_count++;
    child->parent = parent;
    
    return 0;
}

// Remove a child node from a parent directory
int vfs_remove_child(vfs_node_t *parent, const char *name) {
    if (!parent || !name || parent->type != VFS_DIRECTORY) {
        return -1;
    }
    
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, name) == 0) {
            // Found the child, remove it
            vfs_destroy_node(parent->children[i]);
            
            // Shift remaining children
            for (uint32_t j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->children[parent->child_count - 1] = NULL;
            parent->child_count--;
            
            return 0;
        }
    }
    
    return -1; // Not found
}

// Find a child node by name
vfs_node_t* vfs_find_child(vfs_node_t *parent, const char *name) {
    if (!parent || !name || parent->type != VFS_DIRECTORY) {
        return NULL;
    }
    
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, name) == 0) {
            return parent->children[i];
        }
    }
    
    return NULL;
}

// List directory contents
int vfs_list_dir(vfs_node_t *dir, vfs_node_t **list, uint32_t max_entries) {
    if (!dir || !list || dir->type != VFS_DIRECTORY) {
        return -1;
    }
    
    uint32_t count = dir->child_count < max_entries ? dir->child_count : max_entries;
    uint32_t valid_count = 0;
    
    for (uint32_t i = 0; i < count; i++) {
        if (!dir->children[i]) {
            continue;
        }
        list[valid_count] = dir->children[i];
        valid_count++;
    }
    
    return valid_count;
}

// Create a file
vfs_node_t* vfs_create_file(vfs_node_t *parent, const char *name, uint8_t permissions) {
    if (!parent || parent->type != VFS_DIRECTORY) {
        return NULL;
    }
    
    // Check if file already exists
    if (vfs_find_child(parent, name)) {
        return NULL; // Already exists
    }
    
    vfs_node_t *file = vfs_create_node(name, VFS_FILE, permissions);
    if (!file) {
        return NULL;
    }
    
    if (vfs_add_child(parent, file) != 0) {
        vfs_destroy_node(file);
        return NULL;
    }
    
    return file;
}

// Write data to a file
int vfs_write_file(vfs_node_t *file, const uint8_t *data, uint32_t size) {
    if (!file || file->type != VFS_FILE || !data) {
        return -1;
    }
    
    // Check write permission
    if (!(file->permissions & VFS_PERM_WRITE)) {
        return -1;
    }
    
    // Allocate or reallocate data buffer
    if (size > file->allocated_size) {
        uint8_t *new_data = (uint8_t*)vfs_malloc(size);
        if (!new_data) {
            return -1; // Out of memory
        }
        
        // Copy old data if exists
        if (file->data) {
            memcpy(new_data, file->data, file->size);
        }
        
        file->data = new_data;
        file->allocated_size = size;
    }
    
    // Write data
    memcpy(file->data, data, size);
    file->size = size;
    file->modified = 0; // TODO: Use real timestamp
    
    return size;
}

// Read data from a file
int vfs_read_file(vfs_node_t *file, uint8_t *buffer, uint32_t size, uint32_t offset) {
    if (!file || file->type != VFS_FILE || !buffer) {
        return -1;
    }
    
    // Check read permission
    if (!(file->permissions & VFS_PERM_READ)) {
        return -1;
    }
    
    if (offset >= file->size) {
        return 0; // End of file
    }
    
    uint32_t available = file->size - offset;
    uint32_t to_read = size < available ? size : available;
    
    memcpy(buffer, file->data + offset, to_read);
    
    return to_read;
}

// Delete a file or directory
int vfs_delete(vfs_node_t *parent, const char *name) {
    return vfs_remove_child(parent, name);
}

// Resolve a path to a VFS node
vfs_node_t* vfs_resolve_path(const char *path) {
    if (!path || !vfs_root) {
        return NULL;
    }
    
    // Handle root directory
    if (strcmp(path, "/") == 0) {
        return vfs_root;
    }
    
    // Start from root
    vfs_node_t *current = vfs_root;
    
    // Make a copy of the path to tokenize
    char path_copy[VFS_MAX_PATH_LEN];
    strncpy(path_copy, path, VFS_MAX_PATH_LEN - 1);
    path_copy[VFS_MAX_PATH_LEN - 1] = '\0';
    
    // Skip leading slash
    char *token = path_copy;
    if (token[0] == '/') {
        token++;
    }
    
    // Traverse the path
    while (*token != '\0') {
        // Find next slash or end of string
        char *next_slash = token;
        while (*next_slash != '\0' && *next_slash != '/') {
            next_slash++;
        }
        
        // Extract component name
        char component[VFS_MAX_NAME_LEN];
        size_t len = next_slash - token;
        if (len >= VFS_MAX_NAME_LEN) {
            return NULL; // Component name too long
        }
        strncpy(component, token, len);
        component[len] = '\0';
        
        // Skip empty components (e.g., double slashes)
        if (len == 0) {
            if (*next_slash == '/') {
                token = next_slash + 1;
            } else {
                token = next_slash;
            }
            continue;
        }
        
        // Find child
        current = vfs_find_child(current, component);
        if (!current) {
            return NULL; // Path not found
        }
        
        // Move to next component
        if (*next_slash == '/') {
            token = next_slash + 1;
        } else {
            token = next_slash;
        }
    }
    
    return current;
}

// Resolve a path relative to a current directory
vfs_node_t* vfs_resolve_relative_path(const char *path, vfs_node_t *current_dir) {
    if (!path) {
        return NULL;
    }
    
    // If path starts with '/', it's absolute
    if (path[0] == '/') {
        return vfs_resolve_path(path);
    }
    
    // If no current directory, use root
    if (!current_dir) {
        current_dir = vfs_root;
    }
    
    // Handle empty path or "."
    if (path[0] == '\0' || (path[0] == '.' && path[1] == '\0')) {
        return current_dir;
    }
    
    // Build absolute path from current directory
    char abs_path[VFS_MAX_PATH_LEN];
    
    // Get current directory path
    if (!vfs_get_full_path(current_dir, abs_path, sizeof(abs_path))) {
        return NULL;
    }
    
    // Append relative path
    size_t abs_len = strlen(abs_path);
    if (abs_len > 0 && abs_path[abs_len - 1] != '/') {
        if (abs_len + 1 >= VFS_MAX_PATH_LEN) {
            return NULL;
        }
        abs_path[abs_len++] = '/';
        abs_path[abs_len] = '\0';
    }
    
    if (abs_len + strlen(path) >= VFS_MAX_PATH_LEN) {
        return NULL; // Path too long
    }
    
    strcat(abs_path, path);
    
    // Resolve the absolute path
    return vfs_resolve_path(abs_path);
}

// Get root directory
vfs_node_t* vfs_get_root(void) {
    return vfs_root;
}

// Open a file
int vfs_open(const char *path) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node || node->type != VFS_FILE) {
        return -1;
    }
    
    // Find free file descriptor
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!file_descriptors[i].in_use) {
            file_descriptors[i].node = node;
            file_descriptors[i].position = 0;
            file_descriptors[i].in_use = true;
            return i;
        }
    }
    
    return -1; // No free descriptors
}

// Close a file
int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        return -1;
    }
    
    file_descriptors[fd].in_use = false;
    file_descriptors[fd].node = NULL;
    file_descriptors[fd].position = 0;
    
    return 0;
}

// Read from an open file
int vfs_read(int fd, uint8_t *buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        return -1;
    }
    
    vfs_file_descriptor_t *desc = &file_descriptors[fd];
    int bytes_read = vfs_read_file(desc->node, buffer, size, desc->position);
    
    if (bytes_read > 0) {
        desc->position += bytes_read;
    }
    
    return bytes_read;
}

// Write to an open file
int vfs_write(int fd, const uint8_t *data, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        return -1;
    }
    
    vfs_file_descriptor_t *desc = &file_descriptors[fd];
    
    // For simplicity, we'll just overwrite the entire file
    // A more sophisticated implementation would support appending
    int bytes_written = vfs_write_file(desc->node, data, size);
    
    if (bytes_written > 0) {
        desc->position = size;
    }
    
    return bytes_written;
}

// Seek in an open file
int vfs_seek(int fd, uint32_t position) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        return -1;
    }
    
    file_descriptors[fd].position = position;
    return 0;
}

// Get VFS statistics
void vfs_get_stats(vfs_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    stats->total_nodes = 0;
    stats->total_files = 0;
    stats->total_directories = 0;
    stats->total_size = 0;
    
    // TODO: Implement recursive traversal to count all nodes
}

// Print VFS tree (for debugging)
void vfs_print_tree(vfs_node_t *node, int depth) {
    if (!node) {
        return;
    }
    
    // Print indentation
    for (int i = 0; i < depth; i++) {
        terminal_writestring("  ");
    }
    
    // Print node info
    if (node->type == VFS_DIRECTORY) {
        terminal_writestring("[DIR] ");
    } else {
        terminal_writestring("[FILE] ");
    }
    terminal_writestring(node->name);
    
    if (node->type == VFS_FILE) {
        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), " (%u bytes)", node->size);
        terminal_writestring(size_buf);
    }
    
    terminal_writestring("\n");
    
    // Recursively print children
    if (node->type == VFS_DIRECTORY) {
        for (uint32_t i = 0; i < node->child_count; i++) {
            vfs_print_tree(node->children[i], depth + 1);
        }
    }
}

// Get full path of a node
char* vfs_get_full_path(vfs_node_t *node, char *buffer, size_t buffer_size) {
    if (!node || !buffer || buffer_size == 0) {
        return NULL;
    }
    
    // Build path backwards from node to root
    char temp[VFS_MAX_PATH_LEN];
    temp[0] = '\0';
    
    vfs_node_t *current = node;
    while (current && current->parent) {
        // Prepend "/" and name
        size_t name_len = strlen(current->name);
        size_t temp_len = strlen(temp);
        
        if (name_len + temp_len + 2 >= VFS_MAX_PATH_LEN) {
            return NULL; // Path too long
        }
        
        // Shift existing content
        memmove(temp + name_len + 1, temp, temp_len + 1);
        temp[0] = '/';
        memcpy(temp + 1, current->name, name_len);
        
        current = current->parent;
    }
    
    // Handle root
    if (temp[0] == '\0') {
        strncpy(buffer, "/", buffer_size);
    } else {
        strncpy(buffer, temp, buffer_size);
    }
    buffer[buffer_size - 1] = '\0';
    
    return buffer;
}

// Helper function: Read from a path directly
int vfs_read_path(const char *path, uint8_t *buffer, uint32_t size, uint32_t offset) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) {
        return -1; // Path not found
    }
    
    return vfs_read_file(node, buffer, size, offset);
}

// Helper function: Write to a path directly (creates file if it doesn't exist)
int vfs_write_path(const char *path, const uint8_t *data, uint32_t size) {
    vfs_node_t *node = vfs_resolve_path(path);
    
    // If file doesn't exist, try to create it
    if (!node) {
        // Extract directory and filename
        char path_copy[VFS_MAX_PATH_LEN];
        strncpy(path_copy, path, VFS_MAX_PATH_LEN - 1);
        path_copy[VFS_MAX_PATH_LEN - 1] = '\0';
        
        // Find last slash
        char *last_slash = strrchr(path_copy, '/');
        if (!last_slash) {
printf("VFS: Invalid path '%s'\n", path);
            return -1; // Invalid path
        }
        
        // Split path into directory and filename
        *last_slash = '\0';
        const char *filename = last_slash + 1;
        const char *dir_path = path_copy[0] == '\0' ? "/" : path_copy;
        
        // Find parent directory
        vfs_node_t *parent = vfs_resolve_path(dir_path);
        if (!parent || parent->type != VFS_DIRECTORY) {
                        printf("VFS: Parent directory not found for path '%s'\n", path);
            return -1; // Parent directory not found
        }
        
        // Create file
        node = vfs_create_file(parent, filename, VFS_PERM_READ | VFS_PERM_WRITE);
        if (!node) {
            return -1; // Failed to create file
        }
    }
    
    return vfs_write_file(node, data, size);
}

// Helper function: Read from a path relative to current directory
int vfs_read_path_relative(const char *path, vfs_node_t *current_dir, uint8_t *buffer, uint32_t size, uint32_t offset) {
    vfs_node_t *node = vfs_resolve_relative_path(path, current_dir);
    if (!node) {
        return -1; // Path not found
    }
    
    return vfs_read_file(node, buffer, size, offset);
}

// Helper function: Write to a path relative to current directory (creates file if it doesn't exist)
int vfs_write_path_relative(const char *path, vfs_node_t *current_dir, const uint8_t *data, uint32_t size) {
    vfs_node_t *node = vfs_resolve_relative_path(path, current_dir);
    
    // If file doesn't exist, try to create it
    if (!node) {
        // Build absolute path
        char abs_path[VFS_MAX_PATH_LEN];
        
        if (path[0] == '/') {
            // Already absolute
            strncpy(abs_path, path, VFS_MAX_PATH_LEN - 1);
            abs_path[VFS_MAX_PATH_LEN - 1] = '\0';
        } else {
            // Make absolute from current directory
            if (!current_dir) {
                current_dir = vfs_root;
            }
            
            if (!vfs_get_full_path(current_dir, abs_path, sizeof(abs_path))) {
                return -1;
            }
            
            size_t abs_len = strlen(abs_path);
            if (abs_len > 0 && abs_path[abs_len - 1] != '/') {
                if (abs_len + 1 >= VFS_MAX_PATH_LEN) {
                    return -1;
                }
                abs_path[abs_len++] = '/';
                abs_path[abs_len] = '\0';
            }
            
            if (abs_len + strlen(path) >= VFS_MAX_PATH_LEN) {
                return -1;
            }
            
            strcat(abs_path, path);
        }
        
        // Extract directory and filename
        char *last_slash = strrchr(abs_path, '/');
        if (!last_slash) {
            return -1;
        }
        
        *last_slash = '\0';
        const char *filename = last_slash + 1;
        const char *dir_path = abs_path[0] == '\0' ? "/" : abs_path;
        
        // Find parent directory
        vfs_node_t *parent = vfs_resolve_path(dir_path);
        if (!parent || parent->type != VFS_DIRECTORY) {
            return -1;
        }
        
        // Create file
        node = vfs_create_file(parent, filename, VFS_PERM_READ | VFS_PERM_WRITE);
        if (!node) {
            return -1;
        }
    }
    
    return vfs_write_file(node, data, size);
}
