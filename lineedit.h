/* lineedit.h — a small line editor for the interactive session.
 *
 * Used only when standard input is a terminal. It puts the terminal in raw
 * mode while a line is being read and supports:
 *   Left/Right, Home/End (also Ctrl-A/Ctrl-E), Backspace, Delete,
 *   Up/Down (also Ctrl-P/Ctrl-N) for history, Ctrl-U / Ctrl-K / Ctrl-W to
 *   delete to the start / to the end / the previous word, Ctrl-L to clear
 *   the screen, Ctrl-C to abandon the line, Ctrl-D on an empty line for
 *   end of input.
 * Tab inserts a tab (Cabby indents with tabs); tabs are shown 4 columns
 * wide. Right after a name or a dot, Tab completes instead, if a completer
 * is set (le_set_completer); it is still a tab when there is nothing to
 * complete, and after a Tab that only listed the choices. Long lines scroll sideways. Bracketed paste is understood, so a
 * pasted block keeps its own indentation instead of gaining the
 * automatic one.
 */
#ifndef LINEEDIT_H
#define LINEEDIT_H

#include <stdbool.h>

typedef struct LineEditor LineEditor;

typedef enum { LE_OK, LE_EOF, LE_INTERRUPT } LeStatus;

/* `history_path` may be NULL (no history file). */
LineEditor *le_new(const char *history_path);
/* Reads one line (without the newline) into a malloc'd *out. `prefill` is
 * placed in the line first (the session's automatic indentation); with
 * `auto_dedent`, typing `}` on a line of only tabs removes one tab. */
LeStatus le_read(LineEditor *le, const char *prompt, const char *prefill, bool auto_dedent, char **out);
void le_history_add(LineEditor *le, const char *line);
/* Saves the history file and frees the editor. */
void le_free(LineEditor *le);

/* Coloring as the line is drawn: classify fills one class per byte of the
 * line (0: plain), and color gives the escape sequence for a class. */
typedef void (*LeClassify)(const char *line, int len, unsigned char *cls);
typedef const char *(*LeColor)(int cls);
void le_set_colors(LineEditor *le, LeClassify classify, LeColor color);

/* Completion: what the word ending at `pos` could become. Sets *start to
 * where that word begins (the part being completed), and *items to a
 * malloc'd array of malloc'd candidates, each starting with the word;
 * returns how many. One candidate replaces the word; several fill in what
 * they share, and if that adds nothing, are listed under the line. */
typedef int (*LeComplete)(const char *line, int pos, int *start, char ***items);
void le_set_completer(LineEditor *le, LeComplete complete);

#endif
