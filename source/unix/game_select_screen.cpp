/*
 * game_select_screen.cpp
 *
 *  Created on: Feb 8, 2014
 *      Author: abamberger
 */

#include <dirent.h>
#include <sys/types.h>

#include "game_select_screen.h"

extern settings conf;

GameSelectScreen::GameSelectScreen() : m_gamesList(NULL), m_totalNumGames(0), m_currentSelection(0), m_counterFreq(0), m_lastCounts(0)
{
    uint32_t windowflags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN_DESKTOP;

    m_window = SDL_CreateWindow("Game Select Screen",   // window title
                                SDL_WINDOWPOS_CENTERED,	// initial x position
                                SDL_WINDOWPOS_CENTERED,	// initial y position
                                TEMP_WIDTH,             // width, in pixels
                                TEMP_HEIGHT,            // height, in pixels
                                windowflags);

	if(m_window == NULL) {
		fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
	}

	m_bigFont = TTF_OpenFont("../Fonts/PressStart2P.ttf", 24);
    if (m_bigFont == NULL) {
        fprintf(stderr, "Error loading big font: %s\n", TTF_GetError());
    }

    m_smallFont = TTF_OpenFont("../Fonts/PressStart2P.ttf", 16);
    if (m_smallFont == NULL) {
        fprintf(stderr, "Error loading small font: %s\n", TTF_GetError());
    }

    m_currentWindowWidth = TEMP_WIDTH;
    m_currentWindowHeight = TEMP_HEIGHT;

    m_counterFreq = SDL_GetPerformanceFrequency();
}

GameSelectScreen::~GameSelectScreen()
{
    TTF_CloseFont(m_bigFont);
    TTF_CloseFont(m_smallFont);

    // If we've allocated the games list, free it
    if (m_gamesList) {
        for (int i = 0; i < m_totalNumGames; i++) {
            if (m_gamesList[i]) {
                delete[] m_gamesList[i];
            }
        }

        delete[] m_gamesList;
    }

    SDL_DestroyWindow(m_window);
}

void GameSelectScreen::init_game_select_screen()
{
    populate_games_list();
    clear_screen();
    draw_header(m_currentWindowWidth, m_currentWindowHeight);
    draw_choose_list(m_gamesList, m_totalNumGames, m_currentSelection);
    update_screen();
}

MODE GameSelectScreen::handle_event(SDL_Event& event)
{
    MODE goto_mode = SELECTING_MODE;

    switch (event.type) {
    case SDL_WINDOWEVENT:
        switch (event.window.event) {
        case SDL_WINDOWEVENT_RESIZED:
            m_currentWindowWidth = event.window.data1;
            m_currentWindowHeight = event.window.data2;
            clear_screen();
            draw_header(m_currentWindowWidth, m_currentWindowHeight);
            draw_choose_list(m_gamesList, m_totalNumGames, m_currentSelection);
            update_screen();
            break;
        }
        break;

    case SDL_JOYAXISMOTION:
        printf("Received JoyAxisMotion Event.  Type: %d, Value: %d\n", event.jaxis.axis, event.jaxis.value);
        break;

    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        if (event.jbutton.button == 0) {
            goto_mode = SELECTED_MODE;
        }
        printf("Received JoyButton Event.  Button: %d, State: %d\n", event.jbutton.button, event.jbutton.state);
        break;

    case SDL_JOYHATMOTION:
        printf("Received JoyHat Event. JS: %d,  Hat: %d, Value: %d\n", event.jhat.which, event.jhat.hat, event.jhat.value);
        if (event.jhat.value == SDL_HAT_UP) {
            if (m_currentSelection > 0) {
                m_currentSelection--;
            }
        } else if (event.jhat.value == SDL_HAT_DOWN) {
            if (m_currentSelection < (m_totalNumGames - 1)) {
                m_currentSelection++;
            }
        }

        clear_screen();
        draw_header(m_currentWindowWidth, m_currentWindowHeight);
        draw_choose_list(m_gamesList, m_totalNumGames, m_currentSelection);
        update_screen();
        break;
    }

    return goto_mode;
}

void GameSelectScreen::draw_header(int window_w, int window_h)
{
    m_lastCounts = SDL_GetPerformanceCounter();
    SDL_Surface* screen = SDL_GetWindowSurface(m_window);

    // Draw the header into another surface
    SDL_Color font_color = {255, 255, 255, 0};
    SDL_Surface* text_surface = TTF_RenderText_Solid(m_bigFont, "----------Select A Game----------", font_color);

    // Blit the text onto the screen
    int title_x = (window_w / 2) - (text_surface->w / 2);
    SDL_Rect text_location = {title_x, 20, 0, 0};
    SDL_BlitSurface(text_surface, NULL, screen, &text_location);
    SDL_FreeSurface(text_surface);

    printf("draw_header took %f ms\n", ((double)(SDL_GetPerformanceCounter() - m_lastCounts) / (double)m_counterFreq) * 1000.0);
}

void GameSelectScreen::draw_choose_list(char** selections, int num_selections, int current_selection)
{
    m_lastCounts = SDL_GetPerformanceCounter();
    SDL_Surface* screen = SDL_GetWindowSurface(m_window);

    int row_height = TTF_FontLineSkip(m_smallFont);
    SDL_Color font_color = {255, 255, 255, 0};

    char temp_buffer[50];

    int y_base = 50;
    int x_base = 50;

    for (int i = 0; i < num_selections; i++) {
        if (i == current_selection) {
            snprintf(temp_buffer, 50, ">  %s", selections[i]);
        } else {
            snprintf(temp_buffer, 50, "   %s", selections[i]);
        }

        SDL_Surface* text_surface = TTF_RenderText_Solid(m_smallFont, temp_buffer, font_color);
        SDL_Rect text_location = {x_base, y_base + (row_height * i), 0, 0};
        SDL_BlitSurface(text_surface, NULL, screen, &text_location);
        SDL_FreeSurface(text_surface);
    }
    printf("draw_choose_list took %f ms\n", ((double)(SDL_GetPerformanceCounter() - m_lastCounts) / (double)m_counterFreq) * 1000.0);
}

void GameSelectScreen::clear_screen()
{
    m_lastCounts = SDL_GetPerformanceCounter();
    SDL_Surface* screen = SDL_GetWindowSurface(m_window);

    if (screen) {
        // First, clear the entire screen
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
    }
    printf("clear_screen took %f ms\n", ((double)(SDL_GetPerformanceCounter() - m_lastCounts) / (double)m_counterFreq) * 1000.0);
}

void GameSelectScreen::update_screen()
{
    m_lastCounts = SDL_GetPerformanceCounter();
    SDL_UpdateWindowSurface(m_window);
    printf("update_screen took %f ms\n", ((double)(SDL_GetPerformanceCounter() - m_lastCounts) / (double)m_counterFreq) * 1000.0);
}

void GameSelectScreen::populate_games_list()
{
    m_lastCounts = SDL_GetPerformanceCounter();
    // First, count the number of ROM files in the directory
    DIR* romDir = opendir("../ROMs/");
    struct dirent* file = readdir(romDir);

    int numRoms = 0;
    while(file != NULL) {
        if (file->d_type == DT_REG && !((strcmp(file->d_name, ".") == 0) || (strcmp(file->d_name, "..") == 0))) {
            numRoms++;
        }
        file = readdir(romDir);
    }

    if (numRoms > 0) {
        m_totalNumGames = numRoms;

        // Allocate the array for ROM names
        m_gamesList = new char*[numRoms];
        memset(m_gamesList, 0, numRoms * sizeof(char*));

        // Now, walk the directory again and copy the names in
        rewinddir(romDir);
        file = readdir(romDir);

        int fileNum = 0;
        while(file != NULL) {
            if (file->d_type == DT_REG && !((strcmp(file->d_name, ".") == 0) || (strcmp(file->d_name, "..") == 0))) {
                m_gamesList[fileNum] = new char[50];
                memset(m_gamesList[fileNum], 0, 50 * sizeof(char));
                strncpy(m_gamesList[fileNum], file->d_name, 49);
                fileNum++;
            }
            file = readdir(romDir);
        }
    }

    closedir(romDir);

    printf("populate_games_list took %f ms\n", ((double)(SDL_GetPerformanceCounter() - m_lastCounts) / (double)m_counterFreq) * 1000.0);
}

char* GameSelectScreen::get_selected_rom()
{
    return m_gamesList[m_currentSelection];
}

void GameSelectScreen::show_select_screen()
{
    SDL_ShowWindow(m_window);
}

void GameSelectScreen::hide_select_screen()
{
    SDL_HideWindow(m_window);
}

void GameSelectScreen::raise_select_screen()
{
    SDL_RaiseWindow(m_window);
}
