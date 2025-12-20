#ifndef _KERNEL_GRAPHICS_DEMO_H
#define _KERNEL_GRAPHICS_DEMO_H

#include <kernel/vfs.h>

// Graphics demonstration functions
void graphics_demo(void);
void graphics_animation_demo(void);
void graphics_paint_demo(void);
void graphics_paint_demo_with_dir(vfs_node_t *current_dir);

#endif
