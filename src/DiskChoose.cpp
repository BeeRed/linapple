/*
////////////////////////////////////////////////////////////////////////////
////////////  Choose disk image for given slot number? ////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
//// Adapted for linapple - apple][ emulator for Linux by beom beotiger Nov 2007   /////
///////////////////////////////////////////////////////////////////////////////////////
// Original source from one of Brain Games (http://www.braingames.getput.com)
//  game Super Transball 2.(http://www.braingames.getput.com/stransball2/default.asp)
//
// Brain Games crew creates brilliant retro-remakes! Please visit their site to find out more.
//
*/
#ifdef _WIN32
#include "windows.h"
#else

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "ctype.h"

#endif


#include "stdafx.h"

#include "file_entry.h"

#include "Video.h" // for contention avoidance w/video thread
#include "DiskChoose.h"
#include <errno.h>
#include <pthread.h>

#include <iostream>
#include <vector>

// how many file names we are able to see at once!
#define FILES_IN_SCREEN    21

// delay after key pressed (in milliseconds??)
#define KEY_DELAY    25

#define MAX_FILENAME  80

#ifndef _WIN32

int getstat(char *catalog, char *fname, int *size)
{
  // gets file status and returns: 0 - special or error, 1 - file is a directory, 2 - file is a normal file
  // In: catalog - working directory, fname - file name
  struct stat info;
  char tempname[MAX_PATH];

  snprintf(tempname, MAX_PATH, "%s/%s", catalog, fname);  // get full path for the file
  if (stat(tempname, &info) == -1) {
    return 0;
  }
  if (S_ISDIR(info.st_mode)) {
    return 1;  // seems to be directory
  }
  if (S_ISREG(info.st_mode)) {
    if (size != NULL) {
      *size = (int) (info.st_size / 1024);  // get file size in Kbytes?!
    }
    return 2;  // regular file
  }

  return 0;
}

#endif

int compareNames(const void *const A, const void *const B)
{
    return strcasecmp((*(struct dirent **) A)->d_name, (*(struct dirent **) B)->d_name);
}

// get_sorted_directory
// Read and sort the current directory, subdirectories at top followed by
//  sorted files.
// Entry: incoming_dir path
//        file list
//        size list
// Exit: true == success
bool get_sorted_directory(char *incoming_dir, vector<file_entry_t> &file_list)
{
  DIR *dp;
  int count;
  struct dirent **list;
  struct dirent *entry;

  dp = opendir(incoming_dir);
  if (dp == NULL)
  {
      fprintf(stderr, "cannot open `%s'\n", incoming_dir);
      return false;
  }

  /* First determine the number of entries */
  count = 0;
  while ((entry = readdir(dp)) != NULL)
      ++count;
  /* Allocate enough space */
  list = (struct dirent **) malloc(count * sizeof(*list));
  if (list == NULL)
  {
      closedir(dp);
      fprintf(stderr, "memory exhausted.\n");
      return false;
  }
  /* You don't need to allocate the list elements
   * you can just store pointers to them in the
   * pointer array `list'
   */

  rewinddir(dp); /* reset position */
  // save dirent pointer array in list
  count = 0;
  while ((entry = readdir(dp)) != NULL)
    list[count++] = entry;

  // Sort the list
  qsort(list, count, sizeof(*list), compareNames);

  // Add directories to list first

  for (int index = 0 ; index < count ; ++index) {
    // Selectively add to our displayable list
    int what = getstat(incoming_dir, list[index]->d_name, NULL);
    if ((list[index]->d_name && strlen(list[index]->d_name) > 0) && (list[index]->d_name[0] != '.') &&
        what == 1) { // is directory!

      file_list.push_back({list[index]->d_name, file_entry_t::DIR, 0});
    }
  }

  // Add files to list

  for (int index = 0 ; index < count ; ++index) {
    // Selectively add to our displayable list
    int fsize = 0;
    if (strlen(list[index]->d_name) > 4 && list[index]->d_name[0] != '.' &&
        (getstat(incoming_dir, list[index]->d_name, &fsize) == 2)) { // is normal file!
      // DBG
      // std::cout << list[index]->d_name << std::endl;
      file_list.push_back({ list[index]->d_name, file_entry_t::FILE, (unsigned) fsize*1024});
    }
  }
  closedir(dp);
  free(list);
  return true;
}


// GPH TODO: This entire thing needs to be refactored from a massive spinloop
// to an event-driven state model incorporated into Frame.
// Currently, everyhing here is handled inside an event handler.
bool ChooseAnImage(int sx, int sy, char *incoming_dir, int slot, char **filename, bool *isdir, int *index_file)
{
  /*  Parameters:
   sx, sy - window size,
   incoming_dir - in what dir find files,
   slot - in what slot should an image go (common: #6 for 5.25' 140Kb floppy disks, and #7 for hard-disks).
     slot #5 - for 800Kb floppy disks, but we do not use them in Apple][?
    (They are as a rule with .2mg extension)
   index_file  - from which file we should start cursor (should be static and 0 when changing dir)

   Out:  filename  - chosen file name (or dir name)
    isdir    - if chosen name is a directory
  */
  /* Surface: */

  // DIRECTORY-GETTING STUFF

  std::vector<file_entry_t> file_list;

  int act_file;    // current file
  int first_file;    // from which we output files

  //printf("Diskchoose! We are here: %s\n", incoming_dir);

  /* POSIX specific routines of reading directory structure */

  // Forcibly add ".." as first entry to navigate upward
  // to parent directory.
  if (strcmp(incoming_dir, "/")) {
    file_list.push_back({ "..", file_entry_t::UP, 0});
  }

  // Get sorted list of directories and files in the
  // directory specified by incoming_dir
  if (get_sorted_directory(incoming_dir, file_list)) {
    //  Count out cursor position and file number output
    act_file = *index_file;
    if (act_file >= file_list.size()) {
      act_file = 0;
    }    // cannot be more than files in list
    first_file = act_file - (FILES_IN_SCREEN / 2);
    if (first_file < 0) {
      first_file = 0;  // cannot be negative
    }

    // Show all directories (first) and files then

    // DISPLAY STUFF

    if (font_sfc == NULL) {
      if (!fonts_initialization()) {
        return false;  //if we don't have a font, we just can do none
      }
    }

    // Wait for video refresh and claim ownership
    pthread_mutex_lock(&video_draw_mutex);


    // prepare screen
    double facx = double(g_ScreenWidth) / double(SCREEN_WIDTH);
    double facy = double(g_ScreenHeight) / double(SCREEN_HEIGHT);

    SDL_Surface *tempSurface = NULL;
    if (!g_WindowResized) {
      if (g_nAppMode == MODE_LOGO) {
        tempSurface = g_hLogoBitmap;  // use logobitmap
      } else {
        tempSurface = g_hDeviceBitmap;
      }
    } else
      tempSurface = g_origscreen;

    if (tempSurface == NULL) {
      tempSurface = screen;  // use screen, if none available
    }

    SDL_Surface *my_screen;  // for background
    my_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, tempSurface->w, tempSurface->h, tempSurface->format->BitsPerPixel, 0,
                                     0, 0, 0);
    if (tempSurface->format->palette && my_screen->format->palette) {
      SDL_SetColors(my_screen, tempSurface->format->palette->colors, 0, tempSurface->format->palette->ncolors);
    }

    surface_fader(my_screen, 0.2F, 0.2F, 0.2F, -1, 0);  // fade it out to 20% of normal
    SDL_BlitSurface(tempSurface, NULL, my_screen, NULL);

    while (true) {

      SDL_BlitSurface(my_screen, NULL, screen, NULL);    // show background
      font_print_centered(sx / 2, 5 * facy, incoming_dir, screen, 1.5 * facx, 1.3 * facy);
      if (slot == 6) {
        font_print_centered(sx / 2, 20 * facy, "Choose image for floppy 140KB drive", screen, 1 * facx, 1 * facy);
      } else if (slot == 7) {
        font_print_centered(sx / 2, 20 * facy, "Choose image for Hard Disk", screen, 1 * facx, 1 * facy);
      } else if (slot == 5) {
        font_print_centered(sx / 2, 20 * facy, "Choose image for floppy 800KB drive", screen, 1 * facx, 1 * facy);
      } else if (slot == 1) {
        font_print_centered(sx / 2, 20 * facy, "Select file name for saving snapshot", screen, 1 * facx, 1 * facy);
      } else if (slot == 0) {
        font_print_centered(sx / 2, 20 * facy, "Select snapshot file name for loading", screen, 1 * facx, 1 * facy);
      }
      font_print_centered(sx / 2, 30 * facy, "Press ENTER to choose, or ESC to cancel", screen, 1.0 * facx, 1.0 * facy);

      // show all fetched dirs and files
      // topX of first fiel visible
      int TOPX = int(45 * facy);

      // Now, present the list
      for (size_t j = 0; j < FILES_IN_SCREEN; ++j) {
        const size_t i = first_file + j;
        if (i >= file_list.size()) {
          break;
        }
        const file_entry_t file_entry = file_list[i];
        const string& file_name = file_entry.name;

        {
          if (i == act_file) { // show item under cursor (in inverse mode)
            SDL_Rect r;
            r.x = 2;
            r.y = TOPX + j * 15 * facy - 1;
            if (file_name.size() > MAX_FILENAME) {
              r.w = MAX_FILENAME * FONT_SIZE_X /* 6 */ * 1.0 * facx;
            } else {
              r.w = file_name.size() * FONT_SIZE_X /* 6 */ * 1.0 * facx;  // 6- FONT_SIZE_X
            }
            r.h = 9 * 1.0 * facy;
            SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 64, 128, 190));
          }

          // Print file name
          font_print(4, TOPX + j * 15 * facy, file_name.substr(0, MAX_FILENAME).c_str(), screen, 1.0 * facx, 1.0 * facy); // show name
          font_print(sx - 70 * facx, TOPX + j * 15 * facy, file_entry.type_or_size_as_string().c_str(), screen, 1.0 * facx,
                     1.0 * facy);// show info (dir or size)
        }
      }

      // draw rectangles
      rectangle(screen, 0, TOPX - 5, sx, 320 * facy, SDL_MapRGB(screen->format, 255, 255, 255));
      rectangle(screen, 480 * facx, TOPX - 5, 0, 320 * facy, SDL_MapRGB(screen->format, 255, 255, 255));

      SDL_Flip(screen);  // show the screen


      // Relinquish video ownership
      pthread_mutex_unlock(&video_draw_mutex);


      SDL_Delay(KEY_DELAY);  // wait some time to be not too fast

      // Wait for keypress
      SDL_Event event;  // event
      Uint8 *keyboard;  // key state

      event.type = 0;
      while (event.type != SDL_KEYDOWN) {  // wait for key pressed
        // GPH: Honor quit even if we're in the diskchoose state.
        if (SDL_QUIT == event.type) {
          SDL_FreeSurface(my_screen);
          SDL_PushEvent(&event); // push quit event
          return false;
        }
        SDL_Delay(10);
        SDL_PollEvent(&event);
      }

      // control cursor
      keyboard = SDL_GetKeyState(NULL);  // get current state of pressed (and not pressed) keys

      if (keyboard[SDLK_UP] || keyboard[SDLK_LEFT]) {
        if (act_file > 0)
          act_file--;  // up one position
        if (act_file < first_file)
          first_file = act_file;
      }

      if (keyboard[SDLK_DOWN] || keyboard[SDLK_RIGHT]) {
        if (act_file < (file_list.size() - 1))
          act_file++;
        if (act_file >= (first_file + FILES_IN_SCREEN))
          first_file = act_file - FILES_IN_SCREEN + 1;
      }

      if (keyboard[SDLK_PAGEUP]) {
        act_file -= FILES_IN_SCREEN;
        if (act_file < 0)
          act_file = 0;
        if (act_file < first_file)
          first_file = act_file;
      }

      if (keyboard[SDLK_PAGEDOWN]) {
        act_file += FILES_IN_SCREEN;
        if (act_file >= file_list.size())
          act_file = (file_list.size() - 1);
        if (act_file >= (first_file + FILES_IN_SCREEN))
          first_file = act_file - FILES_IN_SCREEN + 1;
      }

      // choose an item?
      if (keyboard[SDLK_RETURN]) {
        // dup string from selected file name
        const file_entry_t& file_entry = file_list[act_file];
        *filename = strdup(file_entry.name.c_str());
        if (file_entry.is_dir_type()) {
          *isdir = true;
        } else {
          *isdir = false;  // this is directory (catalog in Apple][ terminology)
        }
        *index_file = act_file;  // remember current index
        SDL_FreeSurface(my_screen);
        return true;
      }

      if (keyboard[SDLK_ESCAPE]) {
        SDL_FreeSurface(my_screen);
        return false;    // ESC has been pressed
      }

      if (keyboard[SDLK_HOME]) {
        act_file = 0;
        first_file = 0;
      }

      if (keyboard[SDLK_END]) {
        act_file = file_list.size() - 1;  // go to the last possible file in list
        first_file = act_file - FILES_IN_SCREEN + 1;
        if (first_file < 0)
          first_file = 0;
      }

      // GPH: Check for A-Z, a-z, 0-9 and jump to first file starting therewith
      // (Would be nice to use event-driven keydown, but since we're within
      // an event-handler that's not really feasible without a major restructure.)
      {
        bool char_hit = false;
        char ch;
        int char_range_idx = 0;
        static char char_range[4][2] = {{'A','Z'},{'a','z'},{'0','9'},{0,0}};
        while (!char_hit && char_range[char_range_idx][0]) {
          if (!char_hit) {
            for (ch = char_range[char_range_idx][0]; ch <= char_range[char_range_idx][1]; ch++) {
              if (keyboard[(unsigned int) ch]) {
                char_hit = true;
                break;
              }
            } // for
          } // if
          char_range_idx++;
        } // while

        // If user hit a key in one of the ranges, jump to files beginning
        // with that character...
        if (char_hit) {
          // Make pressed char lowercase
          if (ch >= 'A' && ch <= 'Z') {
            ch |= 0x20;
          }
          // Slow, linear search from top of list...
          for (size_t fidx = 0; fidx < file_list.size(); fidx++ ) {
            char file_char = tolower(file_list[fidx].name[0]);
            if (file_char == ch) {
              // If the current file is ALREADY the one found here, or prior,
              // then keep going
              char candidate_char = tolower(file_list[act_file].name[0]);

              if (act_file < fidx || candidate_char != ch) {
                act_file = fidx;
                first_file = fidx;
                break;
              }
            }
          }
        }
      }
    }
  }
  return false;
}
