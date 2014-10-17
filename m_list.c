/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "omxd.h"

static struct playlist { int i; int size; char **arr_sz; }
		list = {    -1,        0,          NULL, };

static char *now_next[2];
static char next_file[LINE_LENGTH];
static char inserted_next_file = 0;

enum list_pos { L_START, L_ACT, L_END, L_ALL };
enum e_dirs { D_1ST, D_PREV, D_ACT, D_NEXT, D_LAST, D_NUMOF };
static int dirs[D_NUMOF] = { 0, };

static void load_list(void);
static void save_list(void);
/* Return true if actual played position modified */
static int insert(enum list_pos base, int offs, char *file);
static int delete(enum list_pos base, int offs);
static int jump(enum list_pos base, int offs);

static int get_pos(enum list_pos base, int offs, int wrap);

static void update_dirs(void);
int new_dir(char *last, char *this);

char **m_list(char *cmd, char *file)
{
	if (list.i == -1)
		load_list();
	LOG(1, "List loaded size=%d i=%d\n", list.size, list.i);
	/* Special cases when there is nothing to do */
	if (cmd == NULL || strchr(LIST_CMDS, *cmd) == NULL)
		return NULL;
	LOG(1, "Cmd %s is a list command\n", cmd);
	if (strchr("IHJ", *cmd) != NULL) {
		now_next[0] = file;
		now_next[1] = NULL;
		return now_next;
	}
	LOG(1, "No temporary insert\n");
	if (*cmd == 'L') {
		strncpy(next_file, file, LINE_LENGTH);
		inserted_next_file = 1;
		return NULL;
	}
	LOG(1, "No temporary append\n");
	if (inserted_next_file && *cmd == 'n') {
		inserted_next_file = 0;
		now_next[0] = next_file;
		now_next[1] = NULL;
		return now_next;
	}
	LOG(1, "Not playing temporary append\n");
	if (strchr(".hj", *cmd) != NULL)
		return now_next;
	LOG(1, "No audio output switch\n");
	if (*cmd == 'X') {
		unlink(LIST_FILE);
		list.size = 0;
		list.i = -1;
		return NULL;
	}
	LOG(1, "No list delete\n");
	int change_act =
		  *cmd == 'i' ? insert(L_ACT, 0, file)
		: *cmd == 'a' ? insert(L_ACT, 1, file)
		: *cmd == 'A' ? insert(L_END, 0, file)
		: *cmd == 'x' ? delete(L_ACT, 0)
		: *cmd == 'n' ? jump(L_ACT,  1)
		: *cmd == 'N' ? jump(L_ACT, -1)
		: *cmd == 'd' ? jump(L_START, dirs[D_PREV])
		: *cmd == 'D' ? jump(L_START, dirs[D_NEXT])
		:               0;
	LOG(1, "Change=%d size=%d i=%d\n", change_act, list.size, list.i);
	if (change_act) {
		now_next[0] = list.arr_sz[list.i];
		now_next[1] = list.arr_sz[(list.i+1) % list.size];
		return now_next;
	}
	return NULL;
}

static void load_list(void)
{
	list.size = 0;
	FILE *play = fopen(LIST_FILE, "r");
	if (play == NULL)
		return;
	char line[LINE_LENGTH];
	while (fgets(line, LINE_LENGTH, play)) {
		line[strlen(line) - 1] = '\0'; /* Remove trailing linefeed */
		/* Migrate automatically from in-file playlist */
		char *file = strstr(line, ">\t") == line ? line + 2 : line;
		insert(L_END, 0, file);
	}
	fclose(play);
}

static void save_list(void)
{
	if (list.size == 0) {
		unlink(LIST_FILE);
		return;
	}
	FILE *play = fopen(LIST_FILE, "w");
	int i;
	for (i = 0; i < list.size; ++i) {
		fputs(list.arr_sz[i], play);
		fputs("\n", play);
	}
	fclose(play);
}

static int insert(enum list_pos base, int offs, char *file)
{
	int i = get_pos(base, offs, 0);
	if (i < 0)
		return 0;
	int size_new = list.size + 1;
	char **arr_new = realloc(list.arr_sz, size_new * sizeof(char*));
	if (arr_new == NULL) {
		LOG(0, "Unable to realloc playlist\n");
		return 0;
	}
	char *file_new = malloc(strlen(file) + 1);
	if (file_new == NULL) {
		LOG(0, "Unable to malloc filename %s\n", file);
		return 0;
	}
	strcpy(file_new, file);
	list.arr_sz = arr_new;
	memmove(list.arr_sz + i + 1,
	        list.arr_sz + i,
	        (list.size - i) * sizeof(char*));
	list.arr_sz[i] = file_new;
	list.size = size_new;
	if (list.i > i || list.i < 0)
		list.i++;
	save_list();
	return list.i == i;
}

static int delete(enum list_pos base, int offs)
{
	if (base == L_ALL && list.size > 0) {
		int i;
		for (i = 0; i < list.size; ++i)
			free(list.arr_sz[i]);
		free(list.arr_sz);
		list.i = -1;
		list.size =0;
		list.arr_sz = NULL;
		save_list();
		return 1;
	}
	int i = get_pos(base, offs, 0);
	if (i < 0)
		return 0;
	int size_new = list.size - 1;
	free(list.arr_sz[i]);
	memmove(list.arr_sz + i,
	        list.arr_sz + i + 1,
	        (size_new - i) * sizeof(char*));
	list.arr_sz = realloc(list.arr_sz, size_new * sizeof(char*));
	list.size = size_new;
	if (list.i > i)
		list.i--;
	save_list();
	return list.i == i;
}

static int jump(enum list_pos base, int offs)
{
	int i = get_pos(base, offs, 1);
	if (i >= 0) {
		list.i = i;
		return 1;
	}
	return 0;
}

static int get_pos(enum list_pos base, int offs, int wrap)
{
	int i_base =
		  list.i < 0      ? -1
		: base == L_START ? 0
		: base == L_ACT   ? list.i
		: base == L_END   ? list.size
		:                   -1;
	if (i_base == -1)
		return -1;
	int i_new = i_base + offs;
	return
		  wrap                             ? i_new % list.size
		: i_new >= 0 && i_new <= list.size ? i_new
		:                                    -1;
}

static void update_dirs(void)
{
	if (list.size == 0)
		return;
	memset(dirs, 0, sizeof dirs);
	int i;
	for (i = 0; i < list.size; ++i) {
		if (!new_dir(i == 0 ? NULL : list.arr_sz[i-1], list.arr_sz[i]))
			continue;
		dirs[D_LAST] = i;
		if (dirs[D_NEXT] > list.i)
			continue;
		dirs[D_PREV] = dirs[D_ACT];
		dirs[D_ACT]  = dirs[D_NEXT];
		dirs[D_NEXT] = i;
	}
	if (dirs[D_PREV] == dirs[D_ACT])
		dirs[D_PREV] = dirs[D_LAST];
	if (dirs[D_NEXT] == dirs[D_ACT])
		dirs[D_NEXT] = dirs[D_1ST];
}

int new_dir(char *last, char *this)
{
	if (last == NULL)
		return 1;
	int last_size = strrchr(last, '/') - last;
	int this_size = strrchr(this, '/') - this;
	if (this_size != last_size)
		return 1;
	if (strncmp(last, this, this_size) != 0)
		return 1;
	return 0;
}