#include <asm-generic/ioctls.h>
#include <bits/pthreadtypes-arch.h>
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

#define TOP_LEFT_CORNER      "\xE2\x94\x8C"
#define TOP_RIGHT_CORNER     "\xE2\x94\x90"
#define BOTTOM_LEFT_CORNER   "\xE2\x94\x94"
#define BOTTOM_RIGHT_CORNER  "\xE2\x94\x98"
#define HORIZONTAL_LINE      "\xE2\x94\x80"
#define VERTICAL_LINE        "\xE2\x94\x82"


// Compoment System
enum cmp_type {
        root,
        container,
        text   
};

struct cmp {
        uint16_t cid, pid;
        uint16_t abs_x, abs_y;
        enum cmp_type type;
        // link
        void *self;
        struct cmp *parent;
        struct cmp **children;
        uint16_t child_count;
};

struct container {
        uint16_t x, y, w, h;

        // link
        struct cmp *base;

};


struct text {
        uint16_t x, y;
        char *content;

        // link
        struct cmp *base;      
};

enum mode {
        Win,
        Txt      
};

// This struct is used to store the character and its style within each cell.
struct cell {
        char content;
};

// This struct is used to initialize the most basic window settings for kiloc.
// Users only need to set the maximum and minimum sizes and whether to display the border.
struct kiloc {
        uint16_t min_w, min_h;
        uint16_t max_w, max_h;
        
        bool boundary;
        
        uint16_t offset_x, offset_y;
        
        struct cmp *cids[256];
        struct cmp root;
        // Render buffer
        struct cell **f_buffer;
        struct cell **b_buffer;

        // Original terminal
        struct termios org_ter;
        
        // Terminal info
        uint16_t ter_w, ter_h;
};





static struct kiloc kiloc_glb;
static struct kiloc *k = &kiloc_glb;


// Used to monitor whether the terminal size has changed,
// and if so, it returns true.
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


// This function is used to draw the window border when the boolean boundary is true.
static void _kiloc_draw_bound(void)
{
        if (!k->boundary || k->ter_w < k->max_w + 2 || k->ter_h < k->max_h + 2)
                return;
        uint16_t sx = k->offset_x;
        uint16_t ex = k->offset_x + k->max_w + 1;
        uint16_t sy = k->offset_y;
        uint16_t ey = k->offset_y + k->max_h + 1;
        uint16_t x, y;
        printf("\033[%d;%dH%s", sy + 1, sx + 1, TOP_LEFT_CORNER);
        for (x = 0; x < k->max_w; ++x)
                printf("%s", HORIZONTAL_LINE);
        printf("%s", TOP_RIGHT_CORNER);
        for (y = 0; y < k->max_h; ++y) {
                printf("\033[%d;%dH%s", sy + y + 2, sx + 1, VERTICAL_LINE);
                printf("\033[%d;%dH%s", sy + y + 2, ex + 1, VERTICAL_LINE);
        }
        printf("\033[%d;%dH%s", ey + 1, sx + 1, BOTTOM_LEFT_CORNER);
        for (x = 0; x < k->max_w; ++x)
                printf("%s", HORIZONTAL_LINE);
        printf("%s", BOTTOM_RIGHT_CORNER);
}

void kiloc_init(uint16_t min_w, uint16_t min_h, uint16_t max_w, uint16_t max_h,
                enum mode mode, bool boundary)
{
        setlocale(LC_ALL, "");
        k->min_w = min_w;
        k->min_h = min_h;
        k->max_w = max_w;
        k->max_h = max_h;
        k->boundary = boundary;
        k->b_buffer = (struct cell **)calloc(max_h, sizeof(struct cell *));
        k->f_buffer = (struct cell **)calloc(max_h, sizeof(struct cell *));

        for (uint16_t i = 0; i < max_h; i++) {
                k->b_buffer[i] = (struct cell *)calloc(max_w, sizeof(struct cell));
                k->f_buffer[i] = (struct cell *)calloc(max_w, sizeof(struct cell));      
        }

        for (uint16_t y = 0; y < max_h; ++y) {
                for (uint16_t x = 0; x < max_w; ++x) {
                        k->f_buffer[y][x].content = ' ';
                        k->b_buffer[y][x].content = ' ';   
                }
        }

        k->root.cid = 0;
        k->root.pid = 0;
        k->root.abs_x = 0;
        k->root.abs_y = 0;
        k->root.type = root; 
        k->root.parent = NULL;
        k->root.children = NULL;
        k->root.child_count = 0;
        
        struct container *root_cont = (struct container *)malloc(sizeof(struct container));
        root_cont->base = &k->root;
        root_cont->x = 0;
        root_cont->y = 0;
        root_cont->w = k->max_w;
        root_cont->h = k->max_h;

        k->root.self = root_cont;
        k->cids[k->root.cid] = &k->root;
        
        // set terminal
        if (mode == 0) {
                printf("\033[2J");    // clear terminal
                printf("\033[?25l");  // hide cursor

                // set raw mode
                struct termios raw;
                tcgetattr(STDIN_FILENO, &k->org_ter);
                raw = k->org_ter;
                raw.c_lflag &= ~(ICANON | ECHO);
                raw.c_cc[VMIN] = 0;
                raw.c_cc[VTIME] = 0;
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        }
}

// Placing a character into the buffer.
// (you are not expected to use this function, even if it is an API.)
void kiloc_putchr(uint16_t x, uint16_t y, const char *content)
{
        if (x >= k->max_w || y >= k->max_h) {
                return;
        }

        struct cell *c = &k->b_buffer[y][x];
        c->content = *content;
}


// Placing a string into the buffer.
// (you are not expected to use this function, even if it is an API.)
void kiloc_putstr(uint16_t x, uint16_t y, const char *content)
{
        uint16_t cur_x = x;
        const char *ptr = content;

        while (*ptr != '\0' && cur_x < k->max_w) {
                kiloc_putchr(cur_x, y, ptr);
                cur_x++;
                ptr++;
        }   
}


static void _kiloc_cmp_add_child(struct cmp *p, struct cmp *c)
{
        c->parent = p;

        uint16_t new_cont = p->child_count + 1;
        p->children = realloc(p->children, new_cont * sizeof(struct cmp *));

        p->children[p->child_count] = c;
        p->child_count = new_cont;
}

static struct container *_kiloc_cmp_container_init(struct cmp *c)
{
        c->self = (struct container *)malloc(sizeof(struct container));
        struct container *self = c->self;

        self->base = c;
        return self;
}

static struct text *_kiloc_cmp_text_init(struct cmp *c)
{
        c->self = (struct text *)malloc(sizeof(struct text));
        struct text *s = c->self;

        s->base = c;
        return s;       
}

void *kiloc_addcmp (struct cmp *c)
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


static void _kiloc_cmp_container_render(struct cmp *c);
static void _kiloc_cmp_text_render(struct cmp *c);
static void _kiloc_cmp_root_render(struct cmp *c);
static void _kiloc_cmp_render(struct cmp *c)
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

static void _kiloc_cmp_root_render(struct cmp *c)
{
        for (uint16_t i = 0; i < c->child_count; ++i) {
                _kiloc_cmp_render(c->children[i]);
        }
}

static void _kiloc_cmp_container_render(struct cmp *c)
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

static void _kiloc_cmp_text_render(struct cmp* c)
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

        
        kiloc_putstr(c->abs_x, c->abs_y, s->content);
}

void kiloc_render(void)
{
        uint16_t x, y;
        if (_kiloc_check_tersize()) {
                printf("\033[2J");
                for (y = 0; y < k->max_h; ++y)
                        for (x = 0; x < k->max_w; ++x)
                                k->f_buffer[y][x].content = '\0';
        }

        k->offset_x = (k->ter_w > k->max_w) ? (k->ter_w - k->max_w) / 2 : 0;
        k->offset_y = (k->ter_h > k->max_h) ? (k->ter_h - k->max_h) / 2 : 0;

        if (k->ter_w < k->min_w || k->ter_h < k->min_h) {
                printf("\033[1;1HPlease resize your terminal to at least %d x %d to view this content. :)\n", k->min_w, k->min_h);
                fflush(stdout);
                return;
        }

        // clear b_buffer
        for (uint16_t y = 0; y < k->max_h; ++y)
                for (uint16_t x = 0; x < k->max_w; ++x)
                        k->b_buffer[y][x].content = ' ';
         _kiloc_cmp_render(&k->root);  
        
        for (y = 0; y < k->max_h; ++y)
                for (x = 0; x < k->max_w; ++x)
                        if (k->f_buffer[y][x].content != k->b_buffer[y][x].content) {
                                printf("\033[%d;%dH%c", k->offset_y + y + 1, k->offset_x + x + 1, k->b_buffer[y][x].content); 
                                k->f_buffer[y][x].content = k->b_buffer[y][x].content;
                        }
        
         _kiloc_draw_bound();
        fflush(stdout);
}
