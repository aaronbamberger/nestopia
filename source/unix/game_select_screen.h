/*
 * game_select_screen.h
 *
 *  Created on: Feb 8, 2014
 *      Author: abamberger
 */

#ifndef GAME_SELECT_SCREEN_H_
#define GAME_SELECT_SCREEN_H_

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>

#include "config.h"
#include "main.h"

// TODO: Figure out how to get these to always match the rest of the render code
#define TEMP_WIDTH 800
#define TEMP_HEIGHT 600

class GameSelectScreen
{
public:
	GameSelectScreen();
	~GameSelectScreen();

	void init_game_select_screen();
	MODE handle_event(SDL_Event& event);
	char* get_selected_rom();
	void show_select_screen();
	void hide_select_screen();
	void raise_select_screen();

private:
	SDL_Window* m_window;
	TTF_Font* m_bigFont;
	TTF_Font* m_smallFont;
	int m_currentSelection;
	int m_currentWindowWidth;
	int m_currentWindowHeight;
	int m_totalNumGames;
	char** m_gamesList;

	void clear_screen();
	void draw_header(int window_w, int window_h);
	void draw_choose_list(char** selections, int num_selections, int current_selection);
	void update_screen();
	void populate_games_list();

};

void create_select_screen();

#endif /* GAME_SELECT_SCREEN_H_ */
