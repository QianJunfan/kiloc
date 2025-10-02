/**
 * @file kiloc.h
 * @brief Main header file for the kiloc TUI framework.
 *
 * Defines all public data structures, component types, configuration,
 * and the core API functions required to initialize and run the kiloc TUI.
 */
#ifndef KILOC_H
#define KILOC_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <wchar.h>
#include <locale.h>
#include <string.h>

/** @name UTF-8 Box Drawing Characters
 * These macros define the UTF-8 bytes for basic box drawing elements.
 * @{
 */
#define TOP_LEFT_CORNER      "\xE2\x94\x8C"
#define TOP_RIGHT_CORNER     "\xE2\x94\x90"
#define BOTTOM_LEFT_CORNER   "\xE2\x94\x94"
#define BOTTOM_RIGHT_CORNER  "\xE2\x94\x98"
#define HORIZONTAL_LINE      "\xE2\x94\x80"
#define VERTICAL_LINE        "\xE2\x94\x82"


#define T_RIGHT_JOINT        "\xE2\x94\xA4" // ┣
#define T_LEFT_JOINT         "\xE2\x94\x9C" // ┠
#define TOP_TITLE_LEFT       "\xE2\x95\x82" // ┏
#define TOP_TITLE_RIGHT      "\xE2\x95\x8A" // ┓
/** @} */


/**
 * @brief Component types supported by the kiloc framework.
 */
enum cmp_type {
        root,
        container,
        text,
        box
};

/**
 * @brief Runtime data structure for a layout container component.
 */
struct container {
        uint16_t x, y, w, h;

        struct kiloc_cmp *base;
};


/**
 * @brief Runtime data structure for a text element component.
 */
struct text {
        uint16_t x, y;
        char *content;
        uint64_t style;

        struct kiloc_cmp *base;      
};

struct box {
        uint16_t x, y, w, h;
        char *title;
        uint64_t border_style;

        struct kiloc_cmp *base;
};

/**
 * @brief The generic base structure for all components in the TUI tree.
 *
 * This structure is used for tree traversal and position calculation,
 * holding the common metadata for any component type.
 */
struct kiloc_cmp {
        /* Manually set */
        uint16_t cid;           // Component ID
        uint16_t pid;           // Parent ID

        enum cmp_type type;     // Specifying the component type.

        /* Automatically set */
        uint16_t abs_x, abs_y;  // The absolute coordinates of the component.
        void *self;             // The specific pointer to the component itself.
        struct kiloc_cmp *parent;     // The parent component of this component.
        struct kiloc_cmp **children;  // The child components of this component.
        uint16_t child_count;   // The number of child components.
};

/**
 * @brief Runtime modes for the TUI framework.
 */
enum kiloc_mode {
        Win,    // Create an interactive, continuously running terminal application.
        Txt     // Create an application that only outputs information once.
};


/**
 * @brief Represents a single character cell in the rendering buffer.
 */
struct kiloc_cell {
        // Stores a single UTF-8 character.
        char content[5];

        uint64_t style;         /* The highest 24 bits store the foreground color, 
                                   the next 24 bits store the background color, 
                                   and the lowest 3 bits store style information (italics, underline, bold). */
};

/**
 * @brief Global configuration and state structure for the kiloc framework.
 */
struct kiloc {
        /* Manually set */
        uint16_t min_w, min_h, max_w, max_h;    // Minimum and maximum width/height of the window.
        uint16_t n_cmp;                         // Number of components.
        bool bdry;                              // Boolean flag to show the boundary (border) or not.

        /* Automatically set */
        uint16_t offset_x, offset_y;            // The offset value of the display area relative to the border (used to achieve center alignment).
        struct kiloc_cmp **cids;                      // Stores pointers to all components; the array index corresponds to the Component ID (CID).
        struct kiloc_cmp root;                        // The root node of every component, whose Component ID (CID) is 0.

        // The buffers used for double-buffering.
        struct kiloc_cell **f_buffer;                 // Front buffer.
        struct kiloc_cell **b_buffer;                 // Back buffer.

        // Saving the original terminal configuration (to be restored upon exit).
        struct termios org_ter;

        // Current terminal info.
        uint16_t ter_w, ter_h;                  // Current terminal width and height.

        enum kiloc_mode mode;
};

/* APIs */

/**
 * @brief Initializes the framework resources, terminal state, and core structures.
 *
 * This function must be called before any other kiloc function. It sets up UTF-8,
 * allocates buffers, initializes the root component, and configures terminal modes
 * if running in interactive (Win) mode.
 *
 * @param min_w Minimum required terminal width.
 * @param min_h Minimum required terminal height.
 * @param max_w Maximum width of the virtual rendering canvas.
 * @param max_h Maximum height of the virtual rendering canvas.
 * @param mode The operating mode (Win or Txt).
 * @param show_boundary True to draw a border around the canvas.
 * @param num_comp The expected maximum number of components.
 */
void kiloc_init(uint16_t min_w, uint16_t min_h, uint16_t max_w, uint16_t max_h, enum kiloc_mode mode, bool show_boundary, uint16_t num_comp);

/**
 * @brief Writes a single UTF-8 character to the back buffer at the specified position.
 * @param x Column coordinate (0-indexed) relative to the virtual canvas.
 * @param y Row coordinate (0-indexed) relative to the virtual canvas.
 * @param content The UTF-8 character string (up to 4 bytes).
 * @param style The packed 64-bit style word.
 */
void kiloc_putchr(uint16_t x, uint16_t y, const char *content, uint64_t style);

/**
 * @brief Writes a UTF-8 string to the back buffer, handling wrapping and wide characters.
 * @param x Starting column coordinate (0-indexed).
 * @param y Row coordinate (0-indexed).
 * @param content The UTF-8 string to render.
 * @param style The packed 64-bit style word.
 */
void kiloc_putstr(uint16_t x, uint16_t y, const char *content, uint64_t style);


/**
 * @brief Creates the 64-bit style word from individual parameters.
 *
 * The bit layout is: FG_RGB (40-63) | BG_RGB (16-39) | Flags (0-2).
 *
 * @param fg_rgb Foreground color as 0xRRGGBB.
 * @param bg_rgb Background color as 0xRRGGBB.
 * @param bold Enable bold flag.
 * @param italic Enable italic flag.
 * @param underline Enable underline flag.
 * @return The packed 64-bit style word.
 */
uint64_t kiloc_make_style(uint32_t fg_rgb, uint32_t bg_rgb, 
                        bool bold, bool italic, bool underline);

/**
 * @brief Registers a new component, links it to its parent, and allocates its specific runtime data.
 * @param c Pointer to the initialized kiloc_cmp structure.
 * @return A void pointer to the component's type-specific data (e.g., struct text *).
 */
void *kiloc_addcmp (struct kiloc_cmp *c);

/**
 * @brief The main rendering function, called once per frame in Win mode.
 *
 * Handles terminal resize events, component tree traversal/rendering to the back buffer,
 * and performs double-buffering diff-draw to update only changed cells on the screen.
 */
void kiloc_render(void);


/* Global config */

/**
 * @brief The global kiloc configuration instance.
 */
extern struct kiloc kiloc_config;

#endif // KILOC_H
