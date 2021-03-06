#ifndef _MAIN_H_
#define _MAIN_H_

#define VERSION "1.46-WIP"

enum MODE { SELECTING_MODE, SELECTED_MODE, PLAYING_MODE };

void nst_load(const char *filename);
void nst_play();
void nst_pause();
void nst_reset(bool hardreset);
void nst_schedule_quit();
void nst_set_dirs();
void nst_set_region();
void nst_set_rewind(int direction);

void nst_state_save(int isvst);
void nst_state_load(int isvst);

void nst_fds_info();
void nst_flip_disk();
void nst_switch_disk();

void SetupInput();

#endif
