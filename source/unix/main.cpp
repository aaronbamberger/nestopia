/*
 * Nestopia UE
 * 
 * Copyright (C) 2007-2008 R. Belmont
 * Copyright (C) 2012-2014 R. Danbrook
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */

#include <iostream>
#include <fstream>
#include <strstream>
#include <sstream>
#include <iomanip>
#include <string.h>
#include <cassert>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <vector>
#include <libgen.h>
#ifdef MINGW
#include <io.h>
#endif

#include <SDL.h>
#include <SDL_endian.h>
#include <SDL_ttf.h>

#include "core/api/NstApiEmulator.hpp"
#include "core/api/NstApiVideo.hpp"
#include "core/api/NstApiSound.hpp"
#include "core/api/NstApiInput.hpp"
#include "core/api/NstApiMachine.hpp"
#include "core/api/NstApiUser.hpp"
#include "core/api/NstApiNsf.hpp"
#include "core/api/NstApiMovie.hpp"
#include "core/api/NstApiFds.hpp"
#include "core/api/NstApiRewinder.hpp"
#include "core/api/NstApiCartridge.hpp"
#include "core/api/NstApiCheats.hpp"
#include "core/NstCrc32.hpp"
#include "core/NstChecksum.hpp"
#include "core/NstXml.hpp"

#include "main.h"
#include "cli.h"
#include "audio.h"
#include "video.h"
#include "input.h"
#include "fileio.h"
#include "cheats.h"
#include "config.h"
#include "cursor.h"
#include "game_select_screen.h"

using namespace Nes::Api;
using namespace LinuxNst;

// base class, all interfaces derives from this
Emulator emulator;

bool playing = false;
bool loaded = false;
bool nst_pal = false;

static int nst_quit = 0, state_save = 0, state_load = 0, movie_save = 0, movie_load = 0, movie_stop = 0;
int schedule_stop = 0;

char nstdir[256], savedir[512];
static char savename[512], gamebasename[512];
char rootname[512], lastarchname[512];

static CheatMgr *sCheatMgr;

static Video::Output *cNstVideo;
static Sound::Output *cNstSound;
static Input::Controllers *cNstPads;
static Cartridge::Database::Entry dbentry;

extern dimensions basesize, rendersize;
extern void	*videobuf;

extern settings conf;

static Machine::FavoredSystem get_favored_system() {
	switch (conf.misc_default_system) {
		case 0:
			return Machine::FAVORED_NES_NTSC;
			break;

		case 1:
			return Machine::FAVORED_NES_PAL;
			break;

		case 2:
			return Machine::FAVORED_FAMICOM;
			break;

		case 3:
			return Machine::FAVORED_DENDY;
			break;
	}

	return Machine::FAVORED_NES_NTSC;
}

// *******************
// emulation callbacks
// *******************

// called right before Nestopia is about to write pixels
static bool NST_CALLBACK VideoLock(void* userData, Video::Output& video) {
	video.pitch = video_lock_screen(video.pixels);
	return true; // true=lock success, false=lock failed (Nestopia will carry on but skip video)
}

// called right after Nestopia has finished writing pixels (not called if previous lock failed)
static void NST_CALLBACK VideoUnlock(void* userData, Video::Output& video) {
	video_unlock_screen(video.pixels);
}

static bool NST_CALLBACK SoundLock(void* userData, Sound::Output& sound) {
	return true;
}

static void NST_CALLBACK SoundUnlock(void* userData, Sound::Output& sound) {
	audio_play();
}

static void nst_unload(void) {
	// Remove the cartridge and shut down the NES
	Machine machine(emulator);
	
	if (!loaded) { return; }
	
	// Power down the NES
	fprintf(stderr, "Powering down the emulated machine\n");
	machine.Power(false);

	// Remove the cartridge
	machine.Unload();

	// erase any cheats
	//sCheatMgr->Unload();
}

void nst_pause() {
	// Pauses the game
	if (playing) {
		fileio_do_movie_stop();

		Machine machine(emulator);
		
		audio_deinit();
	}

	playing = 0;

	cursor_set_default();
}

// generate the filename for quicksave files
std::string StrQuickSaveFile(int isvst) {
	
	std::ostringstream ossFile;
	ossFile << nstdir;
	ossFile << "state";
	
	ossFile << "/" << std::setbase(16) << std::setfill('0') << std::setw(8)
		<< basename(gamebasename) << std::string("_") << isvst << ".nst";
	
	return ossFile.str();
}

void nst_fds_info() {
	Fds fds(emulator);

	char* disk;
	char* side;

	fds.GetCurrentDisk() == 0 ? disk = "1" : disk = "2";
	fds.GetCurrentDiskSide() == 0 ? side = "A" : side = "B";

	fprintf(stderr, "Fds: Disk %s Side %s\n", disk, side);
}

void nst_flip_disk() {
	// Flips the FDS disk
	Fds fds(emulator);

	if (fds.CanChangeDiskSide()) {
		fds.ChangeSide();
		nst_fds_info();
	}
}

void nst_switch_disk() {
	// Switches the FDS disk in multi-disk games
	Fds fds(emulator);
	
	int currentdisk = fds.GetCurrentDisk();
	
	// If it's a multi-disk game, eject and insert the other disk
	if (fds.GetNumDisks() > 1) {
		fds.EjectDisk();
		fds.InsertDisk(!currentdisk, 0);
		nst_fds_info();
	}
}

void nst_state_save(int isvst) {
	// Save State
	std::string strFile = StrQuickSaveFile(isvst);

	Machine machine( emulator );
	std::ofstream os(strFile.c_str());
	// use "NO_COMPRESSION" to make it easier to hack save states
	Nes::Result res = machine.SaveState(os, Nes::Api::Machine::USE_COMPRESSION);
	fprintf(stderr, "State Saved: %s\n", strFile.c_str());
}


void nst_state_load(int isvst) {
	// Load State
	std::string strFile = StrQuickSaveFile(isvst);
	
	struct stat qloadstat;
	if (stat(strFile.c_str(), &qloadstat) == -1) {
		fprintf(stderr, "No State to Load\n");
		return;
	}

	Machine machine( emulator );
	std::ifstream is(strFile.c_str());
	Nes::Result res = machine.LoadState(is);
	fprintf(stderr, "State Loaded: %s\n", strFile.c_str());
}

void nst_play() {
	// initialization
	video_init();
	audio_init();
	SetupInput();

	// apply any cheats into the engine
	//sCheatMgr->Enable();

	cNstVideo = new Video::Output;
	cNstSound = new Sound::Output;
	cNstPads  = new Input::Controllers;
	
	audio_set_params(cNstSound);
	audio_unpause();

	schedule_stop = 0;
	playing = 1;
}

void nst_reset(bool hardreset) {
	// Reset the machine (soft or hard)
	Machine machine(emulator);
	Fds fds(emulator);
	machine.Reset(hardreset);

	// put the disk system back to disk 0 side 0
	fds.EjectDisk();
	fds.InsertDisk(0, 0);
}

void nst_schedule_quit() {
	nst_quit = 1;
}

// logging callback called by the core
static void NST_CALLBACK DoLog(void *userData, const char *string, unsigned long int length)
{
	fprintf(stderr, "%s", string);
}

// for various file operations, usually called during image file load, power on/off and reset
static void NST_CALLBACK DoFileIO(void *userData, User::File& file)
{
	unsigned char *compbuffer;
	int compsize, compoffset;
	char mbstr[512];

	switch (file.GetAction())
	{
		case User::File::LOAD_ROM:
			wcstombs(mbstr, file.GetName(), 511);

			if (fileio_load_archive(lastarchname, &compbuffer, &compsize, &compoffset, (const char *)mbstr))
			{
				file.SetContent((const void*)&compbuffer[compoffset], (unsigned long int)compsize);

				free(compbuffer);
			}				
			break;

		case User::File::LOAD_SAMPLE:
		case User::File::LOAD_SAMPLE_MOERO_PRO_YAKYUU:
		case User::File::LOAD_SAMPLE_MOERO_PRO_YAKYUU_88:
		case User::File::LOAD_SAMPLE_MOERO_PRO_TENNIS:
		case User::File::LOAD_SAMPLE_TERAO_NO_DOSUKOI_OOZUMOU:
		case User::File::LOAD_SAMPLE_AEROBICS_STUDIO:
			wcstombs(mbstr, file.GetName(), 511);

			if (fileio_load_archive(lastarchname, &compbuffer, &compsize, &compoffset, (const char *)mbstr))
			{
				int chan, bits, rate;

				if (!strncmp((const char *)compbuffer, "RIFF", 4))
				{
					chan = compbuffer[20] | compbuffer[21]<<8;
					rate = compbuffer[24] | compbuffer[25]<<8 | compbuffer[26]<<16 | compbuffer[27]<<24;
					bits = compbuffer[34] | compbuffer[35]<<8; 

//					std::cout << "WAV has " << chan << " chan, " << bits << " bits per sample, rate = " << rate << "\n";

					file.SetSampleContent((const void*)&compbuffer[compoffset], (unsigned long int)compsize, (chan == 2) ? true : false, bits, rate);
				}

				free(compbuffer);
			}				
			break;

		case User::File::LOAD_BATTERY: // load in battery data from a file
		case User::File::LOAD_EEPROM: // used by some Bandai games, can be treated the same as battery files
		case User::File::LOAD_TAPE: // for loading Famicom cassette tapes
		case User::File::LOAD_TURBOFILE: // for loading turbofile data
		{
			//int size;
			FILE *f;

			f = fopen(savename, "rb");
			if (!f)
			{
				return;
			}
			fseek(f, 0, SEEK_END);
			//size = ftell(f);
			fclose(f);

			std::ifstream batteryFile( savename, std::ifstream::in|std::ifstream::binary );

			if (batteryFile.is_open())
			{
				file.SetContent( batteryFile );
			}
			break;
		}

		case User::File::SAVE_BATTERY: // save battery data to a file
		case User::File::SAVE_EEPROM: // can be treated the same as battery files
		case User::File::SAVE_TAPE: // for saving Famicom cassette tapes
		case User::File::SAVE_TURBOFILE: // for saving turbofile data
		{
			std::ofstream batteryFile( savename, std::ifstream::out|std::ifstream::binary );
			const void* savedata;
			unsigned long savedatasize;

			file.GetContent( savedata, savedatasize );

			if (batteryFile.is_open())
				batteryFile.write( (const char*) savedata, savedatasize );

			break;
		}

		case User::File::LOAD_FDS: // for loading modified Famicom Disk System files
		{
			char fdsname[512];

			snprintf(fdsname, sizeof(fdsname), "%s.ups", rootname);
			
			std::ifstream batteryFile( fdsname, std::ifstream::in|std::ifstream::binary );

			// no ups, look for ips
			if (!batteryFile.is_open())
			{
				snprintf(fdsname, sizeof(fdsname), "%s.ips", rootname);

				std::ifstream batteryFile( fdsname, std::ifstream::in|std::ifstream::binary );

				if (!batteryFile.is_open())
				{
					return;
				}

				file.SetPatchContent(batteryFile);
				return;
			}

			file.SetPatchContent(batteryFile);
			break;
		}

		case User::File::SAVE_FDS: // for saving modified Famicom Disk System files
		{
			char fdsname[512];

			snprintf(fdsname, sizeof(fdsname), "%s.ups", rootname);

			std::ofstream fdsFile( fdsname, std::ifstream::out|std::ifstream::binary );

			if (fdsFile.is_open())
				file.GetPatchContent( User::File::PATCH_UPS, fdsFile );

			break;
		}
	}
}

void nst_set_dirs() {
	// Set up system directories
#ifdef MINGW
	snprintf(nstdir, sizeof(nstdir), "");
#else
	// create system directory if it doesn't exist
	snprintf(nstdir, sizeof(nstdir), "%s/.nestopia/", getenv("HOME"));
	if (mkdir(nstdir, 0755) && errno != EEXIST) {	
		fprintf(stderr, "Failed to create %s: %d\n", nstdir, errno);
	}
#endif
	// create save and state directories if they don't exist
	char dirstr[256];
	snprintf(dirstr, sizeof(dirstr), "%ssave", nstdir);
#ifdef MINGW	
	if (mkdir(dirstr) && errno != EEXIST) {
#else
	if (mkdir(dirstr, 0755) && errno != EEXIST) {
#endif
		fprintf(stderr, "Failed to create %s: %d\n", dirstr, errno);
	}

	snprintf(dirstr, sizeof(dirstr), "%sstate", nstdir);
#ifdef MINGW	
	if (mkdir(dirstr) && errno != EEXIST) {
#else
	if (mkdir(dirstr, 0755) && errno != EEXIST) {
#endif
		fprintf(stderr, "Failed to create %s: %d\n", dirstr, errno);
	}
}

int main(int argc, char *argv[]) {
	
	static SDL_Event event;
	int i;
	void* userData = (void*) 0xDEADC0DE;
	SDL_Window* game_window;
	MODE current_mode = SELECTING_MODE;

	// Set up directories
	nst_set_dirs();
	
	// read the config file
	config_file_read();
	
	if (argc == 1 && conf.misc_disable_gui) {
		// Show usage and free config 
		cli_show_usage();
		return 0;
	}
	
	cli_handle_command(argc, argv);
	
	playing = 0;
	videobuf = NULL;
	
	// Initialize File input/output routines
	fileio_init();
	
	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
		return 1;
	}
	
	if (TTF_Init() < 0) {
		fprintf(stderr, "Couldn't initialize SDL-TTF %s\n", TTF_GetError());
		return 1;
	}

	// Initialize input and read input config
	input_init();
	input_config_read();
	
	// Set up the video parameters
	video_set_params();
	
	/*
	// Initialize GTK+
	gtk_init(&argc, &argv);
	
	if (conf.misc_disable_gui) {
		// do nothing at this point
	}
	// Don't show a GUI if it has been disabled in the config
	else {
		gtkui_init(argc, argv, rendersize.w, rendersize.h);
	}
	*/

    // Create the game window
    //video_create();

	GameSelectScreen* gs_window = new GameSelectScreen();
	gs_window->init_game_select_screen();
	
	// Set up the callbacks
	Video::Output::lockCallback.Set(VideoLock, userData);
	Video::Output::unlockCallback.Set(VideoUnlock, userData);
	
	Sound::Output::lockCallback.Set(SoundLock, userData);
	Sound::Output::unlockCallback.Set(SoundUnlock, userData);
	
	User::fileIoCallback.Set(DoFileIO, userData);
	User::logCallback.Set(DoLog, userData);

	// Load the FDS BIOS and NstDatabase.xml
	fileio_set_fds_bios();
	fileio_load_db();

	// attempt to load and autostart a file specified on the commandline
	/*
	if (argc > 1) {
		nst_load(argv[argc - 1]);

		if (loaded) { nst_play(); }
		
		else {
			fprintf(stderr, "Fatal: Could not load ROM\n");
			exit(1);
		}
	}*/

	nst_quit = 0;
	char romName[100];
	
	while (!nst_quit) {
		/*
		while (gtk_events_pending())
		{
			gtk_main_iteration();
		}
		*/

	    while (SDL_PollEvent(&event)) {
	        // A quit is a quit, no matter which mode we're in
	        if (event.type == SDL_QUIT) {
	            nst_quit = 1;
	            schedule_stop = 1;
	            break;
	        }

	        switch (current_mode) {
	        case SELECTING_MODE:
		    if ((event.type == SDL_JOYBUTTONDOWN) && (event.jbutton.button == 4)) {
	            nst_quit = 1;
} else {
	            current_mode = gs_window->handle_event(event);
}
	            break;

	        case SELECTED_MODE:
	            snprintf(romName, 100, "../ROMs/%s", gs_window->get_selected_rom());
	            nst_load(romName);
	            current_mode = PLAYING_MODE;
	            delete gs_window;
	            gs_window = NULL;
	            video_create();
	            nst_play();
	            break;

	        case PLAYING_MODE:
	            switch (event.type) {
                case SDL_JOYBUTTONDOWN:
                case SDL_JOYBUTTONUP:
                    if (event.jbutton.button == 4) {
                        // User pressed the back button, so stop the current game
                        // and bring the selection screen back up
                        nst_unload();
                        video_destroy();
                        gs_window = new GameSelectScreen();
                        gs_window->init_game_select_screen();
                        current_mode = SELECTING_MODE;
                        break;
                    }
                    // Fall through intentional
	            case SDL_KEYDOWN:
                case SDL_KEYUP:
                case SDL_JOYHATMOTION:
                case SDL_JOYAXISMOTION:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    input_process(cNstPads, event);
                    break;
	            }
	            break;

	        default:
	            break;
	        }
	    }

	    if (current_mode == PLAYING_MODE) {
	        if (NES_SUCCEEDED(Rewinder(emulator).Enable(true)))
            {
                Rewinder(emulator).EnableSound(true);
            }

            if (timing_check()) {
                emulator.Execute(cNstVideo, cNstSound, cNstPads);
                //emulator.Execute(cNstVideo, NULL, cNstPads);
            }

            if (state_save) {
                fileio_do_state_save();
                state_save = 0;
            }

            if (state_load) {
                fileio_do_state_load();
                state_load = 0;
            }

            if (movie_load) {
                fileio_do_movie_load();
                movie_load = 0;
            }

            if (movie_save) {
                fileio_do_movie_save();
                movie_load = 0;
            }

            if (movie_stop) {
                movie_stop = 0;
                fileio_do_movie_stop();
            }

            if (schedule_stop) {
                nst_pause();
            }
	    } else {
	        // So we don't chew up CPU for no reason
	        SDL_Delay(16);
	    }
		
	    /*
		if (playing) {
			while (SDL_PollEvent(&event))
			{
				switch (event.type) {
					case SDL_QUIT:
						schedule_stop = 1;
						return 0; // FIX
						break;
					
					case SDL_KEYDOWN:
					case SDL_KEYUP:
					case SDL_JOYHATMOTION:
					case SDL_JOYAXISMOTION:
					case SDL_JOYBUTTONDOWN:
					case SDL_JOYBUTTONUP:
					case SDL_MOUSEBUTTONDOWN:
					case SDL_MOUSEBUTTONUP:
						input_process(cNstPads, event);
						break;
				}	
			}
			
			if (NES_SUCCEEDED(Rewinder(emulator).Enable(true)))
			{
				Rewinder(emulator).EnableSound(true);
			}
			
			if (timing_check()) {
				emulator.Execute(cNstVideo, cNstSound, cNstPads);
				//emulator.Execute(cNstVideo, NULL, cNstPads);
			}
			
			if (state_save) {
				fileio_do_state_save();
				state_save = 0;
			}
			
			if (state_load) {
				fileio_do_state_load();
				state_load = 0;
			}
			
			if (movie_load) {
				fileio_do_movie_load();
				movie_load = 0;
			}

			if (movie_save) {
				fileio_do_movie_save();
				movie_load = 0;
			}

			if (movie_stop) {
				movie_stop = 0;
				fileio_do_movie_stop();
			}

			if (schedule_stop) {
				nst_pause();
			}
		}
		else {
			//gtk_main_iteration_do(TRUE);
		}
		*/
	}

	if (gs_window) {
	    delete gs_window;
	}
	TTF_Quit();

	nst_unload();

	fileio_shutdown();
	
	audio_deinit();
	
	input_deinit();
	input_config_write();
	
	config_file_write();

	return 0;
}

void nst_set_region() {
	// Set the region
	Machine machine(emulator);
	Cartridge::Database database(emulator);
	//Cartridge::Profile profile;
	
	if (database.IsLoaded()) {
		//std::ifstream dbfile(filename, std::ios::in|std::ios::binary);
		//Cartridge::ReadInes(dbfile, get_favored_system(), profile);
		//dbentry = database.FindEntry(profile.hash, get_favored_system());
		
		machine.SetMode(machine.GetDesiredMode());
		
		if (machine.GetMode() == Machine::PAL) {
			fprintf(stderr, "Region: PAL\n");
			nst_pal = true;
		}
		else {
			fprintf(stderr, "Region: NTSC\n");
			nst_pal = false;
		}
		//printf("Mapper: %d\n", dbentry.GetMapper());
	}
}

void nst_set_rewind(int direction) {
	// Set the rewinder backward or forward
	switch (direction) {
		case 0:
			Rewinder(emulator).SetDirection(Rewinder::BACKWARD);
			break;
			
		case 1:
			Rewinder(emulator).SetDirection(Rewinder::FORWARD);
			break;
			
		default: break;
	}
}

// initialize input going into the game
void SetupInput()
{
	// connect a standard NES pad onto the first port
	//Input(emulator).ConnectController( 0, Input::PAD1 );
	
	// connect a standard NES pad onto the second port too
	//Input(emulator).ConnectController( 1, Input::PAD2 );
	
	// connect the Zapper to port 2
	//Input(emulator).ConnectController( 1, Input::ZAPPER );
	
	Input(emulator).AutoSelectController(0);
	Input(emulator).AutoSelectController(1);
	
	// Use the crosshair if a Zapper is present
	if (Input(emulator).GetConnectedController(0) == 5 ||
		Input(emulator).GetConnectedController(1) == 5) {
		cursor_set_crosshair();
	}
}

void configure_savename(const char* filename) {
	
	int i = 0;
	
	// Set up the save directory
	snprintf(savedir, sizeof(savedir), "%ssave/", nstdir);
	
	// Copy the full file path to the savename variable
	snprintf(savename, sizeof(savename), "%s", filename);
	
	// strip the . and extention off the filename for saving
	for (i = strlen(savename)-1; i > 0; i--) {
		if (savename[i] == '.') {
			savename[i] = '\0';
			break;
		}
	}
	
	// Get the name of the game minus file path and extension
	snprintf(gamebasename, sizeof(gamebasename), "%s", basename(savename));
	
	// Construct save path
	snprintf(savename, sizeof(savename), "%s%s%s", savedir, gamebasename, ".sav");

	// Construct root path for FDS save patches
	snprintf(rootname, sizeof(rootname), "%s%s", savedir, gamebasename);
}

// try and find a patch for the game being loaded
static int find_patch(char *patchname)
{
	FILE *f;

	// did the user turn off auto softpatching?
	if (!conf.misc_soft_patching)
	{
		return 0;
	}

	snprintf(patchname, 511, "%s.ips", gamebasename);
	if ((f = fopen(patchname, "rb")) != NULL)
	{
		fclose(f);
		return 1;
	}
	else
	{
		snprintf(patchname, 511, "%s.ups", gamebasename);
		if ((f = fopen(patchname, "rb")) != NULL)
		{
			fclose(f);
			return 1;
		}
	}

	return 0;
}

void nst_load(const char *filename) {
	// Load a Game ROM
	Machine machine(emulator);
	Nes::Result result;
	char gamename[512], patchname[512];

	// Pull out any inserted cartridges
	nst_unload();

	// (re)configure savename
	configure_savename(filename);

	// C++ file stream
	std::ifstream file(filename, std::ios::in|std::ios::binary);

	if (find_patch(patchname)) {
		std::ifstream pfile(patchname, std::ios::in|std::ios::binary);

		Machine::Patch patch(pfile, false);

		// Soft Patch
		result = machine.Load(file, get_favored_system(), patch);
	}
	else {
		result = machine.Load(file, get_favored_system());
	}
	
	// Set the region
	nst_set_region();
	
	if (NES_FAILED(result)) {
		switch (result) {
			case Nes::RESULT_ERR_INVALID_FILE:
				std::cout << "Invalid file\n";
				break;

			case Nes::RESULT_ERR_OUT_OF_MEMORY:
				std::cout << "Out of memory\n";
				break;

			case Nes::RESULT_ERR_CORRUPT_FILE:
				std::cout << "Corrupt or missing file\n";
				break;

			case Nes::RESULT_ERR_UNSUPPORTED_MAPPER:
				std::cout << "Unsupported mapper\n";
				break;

			case Nes::RESULT_ERR_MISSING_BIOS:
				fprintf(stderr, "FDS games require the FDS BIOS.\nIt should be located at ~/.nestopia/disksys.rom\n");
				break;

			default:
				std::cout << "Unknown error #" << result << "\n";
				break;
		}

		return;
	}

	if (machine.Is(Machine::DISK)) {
		Fds fds(emulator);
		fds.InsertDisk(0, 0);
		nst_fds_info();
	}
	
	// note that something is loaded
	loaded = 1;

	// power on
	machine.Power(true); // false = power off
}
