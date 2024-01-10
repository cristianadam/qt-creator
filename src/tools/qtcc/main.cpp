#include <ncurses.h>
#include <stdlib.h>

#define MAX_ROWS 50
#define MAX_COLS 100

char buffer[MAX_ROWS][MAX_COLS];
int current_row = 0;
int current_col = 0;

void display() {
    clear();

    for (int i = 0; i < MAX_ROWS; i++) {
        mvprintw(i, 0, "%.*s", MAX_COLS, buffer[i]);
    }

    move(current_row, current_col);
    refresh();
}

int main() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    int ch;
    while ((ch = getch()) != KEY_F(1)) {
        switch (ch) {
            case KEY_UP:
                current_row = (current_row - 1 + MAX_ROWS) % MAX_ROWS;
                break;
            case KEY_DOWN:
                current_row = (current_row + 1) % MAX_ROWS;
                break;
            case KEY_LEFT:
                current_col = (current_col - 1 + MAX_COLS) % MAX_COLS;
                break;
            case KEY_RIGHT:
                current_col = (current_col + 1) % MAX_COLS;
                break;
            case '\n':
                // Move to the next row when Enter is pressed
                current_row = (current_row + 1) % MAX_ROWS;
                current_col = 0;
                break;
            default:
                // Insert the character into the buffer
                buffer[current_row][current_col] = ch;
                current_col = (current_col + 1) % MAX_COLS;
                break;
        }

        display();
    }

    endwin();
    return 0;
}