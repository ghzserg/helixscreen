# lv_markdown — LVGL Markdown Viewer Widget

**Date:** 2026-02-08
**Status:** Design

## Overview

A standalone, reusable C99 LVGL widget that renders markdown content. Uses
[md4c](https://github.com/mity/md4c) for parsing and LVGL spangroups/labels
for rendering. Designed to be usable by any LVGL 9.x project — no external
dependencies beyond md4c and LVGL itself.

## Goals

- Pure C99, no C++ required
- Zero-config defaults — provide one font and it works
- Minimal API surface
- Block-based rendering architecture (future-proofed for lazy loading)
- Well-tested, suitable for reuse outside HelixScreen

## Supported Markdown (Tier 2)

| Supported | Not Supported (v1) |
|-----------|---------------------|
| Headers H1-H6 | Links |
| Bold (`**`) | Images |
| Italic (`*`) | Tables |
| Inline code (`` ` ``) | Strikethrough |
| Fenced code blocks (`` ``` ``) | HTML inline |
| Bullet lists (`-`/`*`) | |
| Numbered lists (`1.`) | |
| Nested lists | |
| Blockquotes (`>`) | |
| Horizontal rules (`---`) | |
| Paragraphs and line breaks | |

**Unsupported syntax** is rendered as plain text (raw markdown shown as-is).

## Library Structure

```
lv_markdown/
├── src/
│   ├── lv_markdown.h            # Public API
│   ├── lv_markdown.c            # Widget class + lifecycle
│   ├── lv_markdown_style.h      # Style config struct + defaults
│   ├── lv_markdown_style.c      # Default style initialization
│   └── lv_markdown_renderer.c   # md4c callbacks → LVGL widgets
├── deps/
│   └── md4c/                    # md4c source (md4c.h, md4c.c)
├── tests/
│   └── ...
├── LICENSE
└── README.md
```

## Public API

```c
/* Creation / content */
lv_obj_t * lv_markdown_create(lv_obj_t * parent);
void lv_markdown_set_text(lv_obj_t * obj, const char * markdown);
void lv_markdown_set_text_static(lv_obj_t * obj, const char * markdown);

/* Styling */
void lv_markdown_style_init(lv_markdown_style_t * style);
void lv_markdown_set_style(lv_obj_t * obj, const lv_markdown_style_t * style);

/* Querying */
const char * lv_markdown_get_text(lv_obj_t * obj);
uint32_t lv_markdown_get_block_count(lv_obj_t * obj);
```

- `set_text` clears all children and re-renders.
- `set_text_static` avoids string copy — caller owns the lifetime.
- `get_block_count` exposes parsed block count (nod to future lazy loading).

## Style Configuration

```c
typedef struct {
    /* Body text — the base everything derives from */
    const lv_font_t * body_font;           /* required */
    lv_color_t         body_color;

    /* Headings — NULL font = derive from body */
    const lv_font_t * heading_font[6];     /* H1-H6 */
    lv_color_t         heading_color[6];

    /* Emphasis */
    const lv_font_t * bold_font;           /* NULL = faux bold */
    const lv_font_t * italic_font;         /* NULL = underline */
    const lv_font_t * bold_italic_font;    /* NULL = faux bold + underline */

    /* Code */
    const lv_font_t * code_font;           /* NULL = use body_font */
    lv_color_t         code_color;
    lv_color_t         code_bg_color;
    int32_t            code_corner_radius;

    /* Code blocks */
    lv_color_t         code_block_bg_color;
    int32_t            code_block_corner_radius;
    int32_t            code_block_pad;

    /* Blockquote */
    lv_color_t         blockquote_border_color;
    int32_t            blockquote_border_width;
    int32_t            blockquote_pad_left;

    /* Horizontal rule */
    lv_color_t         hr_color;
    int32_t            hr_height;

    /* Spacing */
    int32_t            paragraph_spacing;  /* gap between blocks */
    int32_t            line_spacing;       /* within a block */
    int32_t            list_indent;        /* per nesting level */
    const char *       list_bullet;        /* bullet character, default "•" */
} lv_markdown_style_t;
```

`lv_markdown_style_init()` fills every field with sensible defaults.

### Font Fallback Strategy

When optional font pointers are NULL:

| Missing Font | Fallback |
|---|---|
| `bold_font` | Faux bold: text shadow with 1px X offset, same color as text |
| `italic_font` | Underline decoration via `lv_style_set_text_decor()` |
| `bold_italic_font` | Faux bold + underline combined |
| `heading_font[N]` | Use `body_font` (future: derive size if LVGL supports it) |
| `code_font` | Use `body_font` |

**Rationale:** LVGL cannot synthesize bold/italic at runtime (no font weight
interpolation, no per-span skew transform). Faux bold via shadow is a
well-known embedded graphics trick. Underline for italic is a clean visual
signal that stays within spangroup capabilities.

## Internal Architecture

### Parsing

md4c (C99, ~3k lines, zero dependencies) parses markdown via SAX-style
callbacks. We respond to the events we support and ignore the rest (which
gives us the plain-text fallback for free).

### Block-Based Rendering

md4c fires two types of callbacks:

- **Block-level:** `enter_block(type)` / `leave_block(type)` — paragraphs,
  headings, lists, code blocks, blockquotes, horizontal rules
- **Span-level:** `enter_span(type)` / `leave_span(type)` — bold, italic,
  inline code
- **Text:** `text(content)` — raw text content

The renderer maintains a state stack:

```c
typedef struct {
    lv_obj_t * container;        /* current parent object */
    lv_obj_t * spangroup;        /* current spangroup being built */
    uint8_t    bold_depth;       /* nesting count for bold */
    uint8_t    italic_depth;     /* nesting count for italic */
    uint8_t    code_depth;       /* inline code nesting */
    uint8_t    list_depth;       /* list nesting level */
} lv_markdown_renderer_t;
```

**Flow example** for `# Hello **world**`:

1. `enter_block(HEADING, level=1)` → create new spangroup, set heading style
2. `text("Hello ")` → add span with heading font
3. `enter_span(BOLD)` → increment `bold_depth`, update current style
4. `text("world")` → add span with bold style
5. `leave_span(BOLD)` → decrement `bold_depth`
6. `leave_block(HEADING)` → finalize spangroup, append to container

**Each completed block is a single LVGL object.** This is the unit of work
for future lazy loading — block creation can be deferred until near the
viewport.

### Block Type → LVGL Widget Mapping

| Markdown Block | LVGL Widget | Notes |
|---|---|---|
| Paragraph | `lv_spangroup` | Spans for inline formatting |
| Heading H1-H6 | `lv_spangroup` | Heading font from style config |
| Fenced code block | `lv_label` | Code font, background-colored container |
| Blockquote | `lv_obj` container | Left border + padding, child blocks inside |
| Bullet list | `lv_obj` container | Each item: bullet label + content spangroup |
| Numbered list | `lv_obj` container | Each item: number label + content spangroup |
| Nested list | Recursive container | Indented by `list_indent * depth` |
| Horizontal rule | `lv_obj` | Fixed-height obj with bg color, full width |
| Line break | — | `\n` within spangroup |

**Why spangroup for paragraphs but label for code blocks?** Code blocks have
no inline formatting — a single label is simpler and avoids spangroup overhead.

**Blockquotes can contain other blocks** (paragraphs, lists, nested
blockquotes). md4c handles this nesting via enter/leave callbacks.

## Scrolling

The widget does **not** manage its own scrolling. It grows to fit content.
The caller wraps it in a scrollable parent if needed. This follows standard
LVGL conventions and avoids nested-scroll conflicts.

## Future: Lazy Loading

Not implemented in v1, but the architecture supports it:

- The renderer already works block-by-block.
- A future version could cache the parsed block list and only materialize
  LVGL widgets for visible blocks + a buffer zone.
- `lv_markdown_get_block_count()` is already in the API.
- The rendering code barely changes — we just control *when* each block
  gets created.

## Testing Strategy

Three layers, using LVGL's test framework:

### 1. Parser Integration Tests

Feed markdown strings, verify correct widget tree:

```c
lv_obj_t * md = lv_markdown_create(lv_screen_active());
lv_markdown_set_text(md, "# Hello **world**");
TEST_ASSERT_EQUAL(1, lv_obj_get_child_count(md));
/* verify span contents, styles... */
```

### 2. Style / Fallback Tests

- Faux bold shadow applied when `bold_font` is NULL
- Underline decoration applied when `italic_font` is NULL
- Default colors match expectations
- Custom style overrides take effect

### 3. Edge Cases

- Empty string
- NULL input
- Only unsupported syntax (renders as plain text)
- Deeply nested lists (3-4 levels)
- Mixed block types (heading → paragraph → list → blockquote)
- Very long single paragraph
- `set_text` called twice (old content cleaned up)
- `set_text_static` lifetime semantics

## HelixScreen Integration

The library is pure C and standalone. HelixScreen integration is a thin
wrapper that:

1. Fills `lv_markdown_style_t` from HelixScreen theme tokens (Noto Sans
   fonts, theme colors, spacing tokens)
2. Optionally registers as an XML component for declarative use
3. Lives in HelixScreen source, NOT in the library

```c
/* HelixScreen-side helper */
lv_markdown_style_t style;
lv_markdown_style_init(&style);
style.body_font = ui_theme_get_font("body");
style.bold_font = ui_theme_get_font("bold");
style.heading_font[0] = ui_theme_get_font("heading");
style.body_color = ui_theme_get_color("text_primary");
/* etc. */
```

No HelixScreen-specific code, no `ui_theme_*` calls, no spdlog, no C++
in the library itself.
