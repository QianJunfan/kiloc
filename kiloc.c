/**
 * @file kiloc.c
 * @brief Core implementation file for the kiloc TUI framework.
 *
 * Contains buffer management, terminal I/O functions, component tree rendering logic,
 * and the main rendering loop.
 */
#include "kiloc.h"

/*-------- Global --------*/
struct kiloc kiloc_config;
static struct kiloc *k = &kiloc_config; /* Convenience pointer to the global state. */

/*-------- Base APIs --------*/
/* Static */

/**
 * @brief Checks for terminal window size changes.
 *
 * Uses ioctl(TIOCGWINSZ) to query the current terminal dimensions.
 *
 * @return True if the terminal size has changed, false otherwise.
 */
static bool _kiloc_check_tersize(void)
{
        struct winsize ws;

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
                return false;

        if (k->ter_w != ws.ws_col || k->ter_h != ws.ws_row) {
                k->ter_w = ws.ws_col;
                k->ter_h = ws.ws_row;
                return true;
        }

        return false;
}

/**
 * @brief Draws the window border using UTF-8 box characters.
 *
 * This function calculates the boundary position based on alignment offsets and
 * prints the border to the terminal using ANSI escape codes for cursor positioning.
 */
static void _kiloc_draw_bound(void)
{
    if (!k->bdry || k->ter_w < k->max_w + 2 || k->ter_h < k->max_h + 2) return;

    uint16_t sx = k->offset_x + 1, sy = k->offset_y + 1;
    uint16_t ex = sx + k->max_w + 1, ey = sy + k->max_h + 1;
    
    // Top border: Corner + Horizontal line + Corner
    printf("\033[%d;%dH%s", sy, sx, TOP_LEFT_CORNER); for (uint16_t i = 0; i < k->max_w; ++i) printf("%s", HORIZONTAL_LINE); printf("%s", TOP_RIGHT_CORNER);

    // Vertical lines: Left and Right side
    for (uint16_t y = sy + 1; y < ey; ++y) printf("\033[%d;%dH%s\033[%d;%dH%s", y, sx, VERTICAL_LINE, y, ex, VERTICAL_LINE);

    // Bottom border: Corner + Horizontal line + Corner 
    printf("\033[%d;%dH%s", ey, sx, BOTTOM_LEFT_CORNER); for (uint16_t i = 0; i < k->max_w; ++i) printf("%s", HORIZONTAL_LINE); printf("%s", BOTTOM_RIGHT_CORNER);
}

/**
 * @brief Sets the terminal to raw mode (non-canonical, no echo).
 *
 * This is crucial for interactive TUI (Win) mode to handle key presses immediately.
 * The original settings are stored in k->org_ter for restoration.
 */
static void _kiloc_set_row_mode(void)
{
        struct termios raw;
        tcgetattr(STDIN_FILENO, &k->org_ter);
        raw = k->org_ter;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/**
 * @brief Determines the byte length of the first UTF-8 character.
 *
 * @param s The UTF-8 string pointer.
 * @return The byte length (1-4), or 0 for invalid or null input.
 */
static int _kiloc_get_utf8_len(const char *s)
{
    if (s == NULL || *s == '\0') return 0;

    unsigned char byte = (unsigned char)s[0];
    if (byte <= 0x7F) return 1;          // 0xxxxxxx (ASCII)
    else if ((byte & 0xE0) == 0xC0) return 2; // 110xxxxx
    else if ((byte & 0xF0) == 0xE0) return 3; // 1110xxxx
    else if ((byte & 0xF8) == 0xF0) return 4; // 11110xxx
    return 0; // Invalid start byte
}

/**
 * @brief Determines the column width (1 or 2) of a character.
 *
 * Uses mbtowc and wcwidth for standard compliant wide character width calculation.
 *
 * @param s The UTF-8 character string.
 * @param len The byte length of the character.
 * @return The width in columns (1 or 2).
 */
static int _kiloc_get_char_width(const char *s, int len)
{
    wchar_t wc;
    if (mbtowc(&wc, s, len) == -1) {
        return 1; // If conversion fails, assume width 1
    }
    int width = wcwidth(wc);
    return (width > 0) ? width : 1; // Treat non-printable/zero-width as 1 to avoid infinite loops
}


/* API */
/**
 * @brief See header for details. Initializes the core framework.
 */
void kiloc_init(uint16_t min_w, uint16_t min_h, uint16_t max_w, uint16_t max_h,
                enum kiloc_mode mode, bool show_boundary, uint16_t num_comp)
{       
        // Set the encoding to UTF-8.
        setlocale(LC_ALL, "");

        // Set the basic size and style information.
        k->min_w = min_w;
        k->min_h = min_h;
        k->max_w = max_w;
        k->max_h = max_h;
        k->bdry  = show_boundary;

        // Initialize the front and back buffers.
        k->b_buffer = (struct kiloc_cell **)calloc(max_h, sizeof(struct kiloc_cell *));
        k->f_buffer = (struct kiloc_cell **)calloc(max_h, sizeof(struct kiloc_cell *));


        for (uint16_t y = 0; y < max_h; ++y) {
                k->b_buffer[y] = (struct kiloc_cell *)calloc(max_w, sizeof(struct kiloc_cell));
                k->f_buffer[y] = (struct kiloc_cell *)calloc(max_w, sizeof(struct kiloc_cell));
                for (uint16_t x = 0; x < max_w; ++x) {
                        strcpy(k->f_buffer[y][x].content, " ");
                        strcpy(k->b_buffer[y][x].content, " ");
                }
        }

        // Initialize component storage
        k->cids = (struct kiloc_cmp **)calloc(num_comp, sizeof(struct kiloc_cmp *));
        // The root component is abstracted as a container.
        struct container *root_comp = (struct container *)malloc(sizeof(struct container));

        k->root.cid = 0;
        k->root.pid = 0;
        k->root.abs_x = 0;
        k->root.abs_y = 0;
        k->root.type = root; 
        k->root.parent = NULL;
        k->root.children = NULL;
        k->root.child_count = 0;
        root_comp->base = &k->root;
        root_comp->x = 0;
        root_comp->y = 0;
        root_comp->w = k->max_w;
        root_comp->h = k->max_h;
        k->root.self = root_comp;
        k->cids[k->root.cid] = &k->root;

        // Set terminal
        if (mode == Win) {
                printf("\033[2J");    // Clear terminal
                printf("\033[?25l");  // Hide cursor

                // Set row mode
                _kiloc_set_row_mode();
        }
}


/**
 * @brief See header for details. Places a single char in the back buffer.
 */
void kiloc_putchr(uint16_t x, uint16_t y, const char *content, uint64_t style)
{
        if (x >= k->max_w || y >= k->max_h)
                return;

        int len = _kiloc_get_utf8_len(content);
        if (len == 0) return; 

        struct kiloc_cell *c = &k->b_buffer[y][x];

        if (len < 5) {
            strncpy(c->content, content, len);
            c->content[len] = '\0';
        } else {
            strncpy(c->content, content, 4);
            c->content[4] = '\0';
        }

        c->style = style;
}

/**
 * @brief See header for details. Places an entire string.
 */
void kiloc_putstr(uint16_t x, uint16_t y, const char *content, uint64_t style)
{
        uint16_t cur_x = x;
        const char *ptr = content;

        while (*ptr != '\0' && cur_x < k->max_w) {
                int len = _kiloc_get_utf8_len(ptr);
                if (len == 0)
                    break;

                int width = _kiloc_get_char_width(ptr, len);


                if (cur_x + width > k->max_w)
                    break;

                kiloc_putchr(cur_x, y, ptr, style);
                
                if (width > 1) {
                    struct kiloc_cell *next_cell = &k->b_buffer[y][cur_x + 1];
                    next_cell->content[0] = '\0';
                    next_cell->style = style;
                }

                cur_x += width;
                ptr += len; 
        }
}


/*-------- Component APIs --------*/
/* Style flag bit masks. */
#define STYLE_BOLD      (1ULL << 0)
#define STYLE_ITALIC    (1ULL << 1)
#define STYLE_UNDERLINE (1ULL << 2)

/* Static component helpers */
static struct container *_kiloc_cmp_container_init(struct kiloc_cmp *c);
static struct text *_kiloc_cmp_text_init(struct kiloc_cmp *c);
static void _kiloc_cmp_add_child(struct kiloc_cmp *p, struct kiloc_cmp *c);
static void _kiloc_cmp_render(struct kiloc_cmp *c);
static void _kiloc_cmp_root_render(struct kiloc_cmp *c);
static void _kiloc_cmp_container_render(struct kiloc_cmp *c);
static void _kiloc_cmp_text_render(struct kiloc_cmp* c);

/**
 * @brief Allocates and links the component-specific struct container.
 * @param c The generic component base.
 * @return The allocated struct container pointer.
 */
static struct container *_kiloc_cmp_container_init(struct kiloc_cmp *c)
{
        c->self = (struct container *)malloc(sizeof(struct container));
        struct container *self = c->self;

        self->base = c;
        return self;
}

/**
 * @brief Allocates and links the component-specific struct text.
 * @param c The generic component base.
 * @return The allocated struct text pointer.
 */
static struct text *_kiloc_cmp_text_init(struct kiloc_cmp *c)
{
        c->self = (struct text *)malloc(sizeof(struct text));
        struct text *s = c->self;

        s->base = c;
        return s;       
}

/**
 * @brief Dynamically adds a child component to a parent's children array.
 * @param p The parent component.
 * @param c The child component.
 */
static void _kiloc_cmp_add_child(struct kiloc_cmp *p, struct kiloc_cmp *c)
{
        c->parent = p;
        uint16_t new_cont = p->child_count + 1;
        p->children = realloc(p->children, new_cont * sizeof(struct kiloc_cmp *));
        p->children[p->child_count] = c;
        p->child_count = new_cont;
}

/**
 * @brief Renders a component by dispatching to the type-specific handler.
 * @param c The component to render.
 */
static void _kiloc_cmp_render(struct kiloc_cmp *c)
{
        if (c == NULL) return;

        switch (c->type) {
                case root:
                        _kiloc_cmp_root_render(c);
                        break;
                case container:
                        _kiloc_cmp_container_render(c);
                        break;
                case text:
                        _kiloc_cmp_text_render(c);
                        break;
        }
}

/**
 * @brief Recursively renders all top-level children attached to the root.
 * @param c The root component.
 */
static void _kiloc_cmp_root_render(struct kiloc_cmp *c)
{
        for (uint16_t i = 0; i < c->child_count; ++i) {
                _kiloc_cmp_render(c->children[i]);
        }
}

/**
 * @brief Calculates the absolute position of a container and recursively renders children.
 * @param c The container component.
 */
static void _kiloc_cmp_container_render(struct kiloc_cmp *c)
{
        struct container *s = (struct container *)c->self;
        uint16_t pid = c->pid;
        if (pid > 0) {
                c->abs_x = k->cids[pid]->abs_x + s->x;
                c->abs_y = k->cids[pid]->abs_y + s->y;
        } else {
                c->abs_x = s->x;
                c->abs_y = s->y;
        }

        for (uint16_t i = 0; i < c->child_count; ++i) {
                _kiloc_cmp_render(c->children[i]);
        }
}


/**
 * @brief Calculates the absolute position of a text component and outputs the string.
 * @param c The text component.
 */
static void _kiloc_cmp_text_render(struct kiloc_cmp* c)
{
        struct text *s = (struct text *)c->self;
        uint16_t pid = c->pid;

                
        if (pid > 0) {
                c->abs_x = k->cids[pid]->abs_x + s->x;
                c->abs_y = k->cids[pid]->abs_y + s->y;
        } else {
                c->abs_x = s->x;
                c->abs_y = s->y;
        }

        
        kiloc_putstr(c->abs_x, c->abs_y, s->content, s->style);
}


/**
 * @brief Applies the ANSI Style Graphics Rendition (SGR) sequence based on the packed style word.
 * @param style The packed 64-bit style word.
 */
static void _kiloc_apply_style(uint64_t style)
{
    uint32_t fg_rgb = (uint32_t)((style >> 40) & 0xFFFFFF); 

    uint32_t bg_rgb = (uint32_t)((style >> 16) & 0xFFFFFF); 

    uint8_t flags = (uint8_t)(style & 0x7);

    uint8_t fg_r = (fg_rgb >> 16) & 0xFF;
    uint8_t fg_g = (fg_rgb >> 8) & 0xFF;
    uint8_t fg_b = fg_rgb & 0xFF;
    
    uint8_t bg_r = (bg_rgb >> 16) & 0xFF;
    uint8_t bg_g = (bg_rgb >> 8) & 0xFF;
    uint8_t bg_b = bg_rgb & 0xFF;

    
    // Reset (Start the sequence with \033[0)
    fputs("\033[0", stdout);

    // Apply style flags
    if (flags & 0x1)
        fputs(";1", stdout);
    if (flags & 0x2)
        fputs(";3", stdout);
    if (flags & 0x4)
        fputs(";4", stdout);

    // Apply Foreground Color (True Color: \033[38;2;R;G;Bm)
    if (fg_rgb != 0) {
        printf(";38;2;%d;%d;%d", fg_r, fg_g, fg_b);
    }

    // Apply Background Color (True Color: \033[48;2;R;G;Bm)
    if (bg_rgb != 0) {
        printf(";48;2;%d;%d;%d", bg_r, bg_g, bg_b);
    }

    // End the escape sequence
    fputs("m", stdout);
}

/* API */
/**
 * @brief See header for details. Packs individual style parameters into a 64-bit word.
 */
uint64_t kiloc_make_style(uint32_t fg_rgb, uint32_t bg_rgb, 
                          bool bold, bool italic, bool underline)
{
        uint64_t style = 0;
        if (bold) {
        style |= STYLE_BOLD;
        }
        if (italic) {
        style |= STYLE_ITALIC;
        }
        if (underline) {
        style |= STYLE_UNDERLINE;
        }
        style |= ((uint64_t)bg_rgb << 16);
        style |= ((uint64_t)fg_rgb << 40);

        return style;
}



/**
 * @brief See header for details. Registers the component into the framework.
 */
void *kiloc_addcmp (struct kiloc_cmp *c)
{         
        k->cids[c->cid] = c;
        if (c->pid > 0)
                _kiloc_cmp_add_child(k->cids[c->pid], c);
        else if (c->pid == 0 && c->cid != 0)
                _kiloc_cmp_add_child(&k->root, c);
        
        switch (c->type) {
                case root:
                        break;
                case container:
                        return _kiloc_cmp_container_init(c);
                case text:
                        return _kiloc_cmp_text_init(c);
        }

        return NULL;
}


/**
 * @brief See header for details. The core rendering loop.
 */
void kiloc_render(void)
{
        uint16_t x, y;
        // Check for terminal size changes
        if (_kiloc_check_tersize()) {
                printf("\033[2J");
                // Force a full screen redraw, reset the front buffer, and apply default style.
                for (y = 0; y < k->max_h; ++y)
                        for (x = 0; x < k->max_w; ++x) {
                                k->f_buffer[y][x].content[0] = '\0';
                                // Use an impossible style value to ensure the style is different during the first render
                                k->f_buffer[y][x].style = (uint64_t)-1;
                        }
        }

        // Calculate offsets and check minimum size requirements (this logic remains unchanged)
        k->offset_x = (k->ter_w > k->max_w + (k->bdry ? 2 : 0)) ? (k->ter_w - (k->max_w + (k->bdry ? 2 : 0))) / 2 : 0;
        k->offset_y = (k->ter_h > k->max_h + (k->bdry ? 2 : 0)) ? (k->ter_h - (k->max_h + (k->bdry ? 2 : 0))) / 2 : 0;

        if (k->ter_w < k->min_w || k->ter_h < k->min_h) {
                printf("\033[1;1HPlease resize your terminal to at least %d x %d to view this content. :)\n", k->min_w, k->min_h);
                fflush(stdout);
                return;
        }

        // Clear the back buffer (b_buffer)
        for (uint16_t y = 0; y < k->max_h; ++y) {
                for (uint16_t x = 0; x < k->max_w; ++x) {
                        strcpy(k->b_buffer[y][x].content, " ");
                        k->b_buffer[y][x].style = 0;
                }
        }

        // Render components to b_buffer
        _kiloc_cmp_render(&k->root);

        // Double-buffering comparison and rendering
        for (y = 0; y < k->max_h; ++y) {
                for (x = 0; x < k->max_w; ++x) {
                        struct kiloc_cell *f = &k->f_buffer[y][x];
                        struct kiloc_cell *b = &k->b_buffer[y][x];

                        // Only output if content or style has changed
                        if (strcmp(f->content, b->content) != 0 || f->style != b->style) {
                                // Position the cursor (row/column needs +1)
                                printf("\033[%d;%dH", k->offset_y + y + 1, k->offset_x + x + 1);

                                // Apply style
                                _kiloc_apply_style(b->style);

                                // Print content
                                printf("%s", b->content);

                                // Update the front buffer
                                strcpy(f->content, b->content);
                                f->style = b->style;
                        }
                }
        }

        // Ensure the style is reset to default after rendering to prevent polluting the terminal prompt
        fputs("\033[0m", stdout);

        // Draw the window boundary
        _kiloc_draw_bound();

        // Flush output
        fflush(stdout);
}
