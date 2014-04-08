/*
 *      Copyright (C) 2014 Arne Morten Kvarving
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "xbmc/libXBMC_addon.h"
#include "vbam/gba/GBA.h"
#include "vbam/gba/Sound.h"
#include "psflib.h"

extern "C" {
#include <stdio.h>
#include <stdint.h>

#include "xbmc/xbmc_audiodec_dll.h"

ADDON::CHelper_libXBMC_addon *XBMC           = NULL;

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  return ADDON_STATUS_OK;
}

//-- Stop ---------------------------------------------------------------------
// This dll must cease all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Stop()
{
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  XBMC=NULL;
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
bool ADDON_HasSettings()
{
  return false;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

void ADDON_FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  return ADDON_STATUS_OK;
}

//-- Announce -----------------------------------------------------------------
// Receive announcements from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}

struct gsf_loader_state
{
  int entry_set;
  uint32_t entry;
  uint8_t * data;
  size_t data_size;
  gsf_loader_state() : entry_set( 0 ), data( 0 ), data_size( 0 ) { }
  ~gsf_loader_state() { free( data ); }
};

struct gsf_sound_out : public GBASoundOut
{
  gsf_sound_out() : head(0), bytes_in_buffer(0) {}
  size_t head;
  size_t bytes_in_buffer;
  std::vector<uint8_t> sample_buffer;
  virtual ~gsf_sound_out() { }
  // Receives signed 16-bit stereo audio and a byte count
  virtual void write(const void * samples, unsigned long bytes)
  {
    sample_buffer.resize(bytes_in_buffer + bytes);
    memcpy( &sample_buffer[ bytes_in_buffer ], samples, bytes);
    bytes_in_buffer += bytes;
  }
};

struct GSFContext
{
  gsf_loader_state state;
  GBASystem system;
  gsf_sound_out output;
  int64_t len;
  int sample_rate;
  int64_t pos;
  std::string title;
  std::string artist;
};


inline unsigned get_le32( void const* p )
{
    return (unsigned) ((unsigned char const*) p) [3] << 24 |
            (unsigned) ((unsigned char const*) p) [2] << 16 |
            (unsigned) ((unsigned char const*) p) [1] << 8 |
            (unsigned) ((unsigned char const*) p) [0];
}

int gsf_loader(void * context, const uint8_t * exe, size_t exe_size,
               const uint8_t * reserved, size_t reserved_size)
{
  if (exe_size < 12)
    return -1;

  struct gsf_loader_state * state = ( struct gsf_loader_state * ) context;

  unsigned char *iptr;
  unsigned isize;
  unsigned char *xptr;
  unsigned xentry = get_le32(exe + 0);
  unsigned xsize = get_le32(exe + 8);
  unsigned xofs = get_le32(exe + 4) & 0x1ffffff;
  if (xsize < exe_size - 12)
    return -1;
  if (!state->entry_set)
  {
    state->entry = xentry;
    state->entry_set = 1;
  }
  {
    iptr = state->data;
    isize = state->data_size;
    state->data = 0;
    state->data_size = 0;
  }
  if (!iptr)
  {
    unsigned rsize = xofs + xsize;
    {
      rsize -= 1;
      rsize |= rsize >> 1;
      rsize |= rsize >> 2;
      rsize |= rsize >> 4;
      rsize |= rsize >> 8;
      rsize |= rsize >> 16;
      rsize += 1;
    }
    iptr = (unsigned char *) malloc(rsize + 10);
    if (!iptr)
      return -1;
    memset(iptr, 0, rsize + 10);
    isize = rsize;
  }
  else if (isize < xofs + xsize)
  {
    unsigned rsize = xofs + xsize;
    {
      rsize -= 1;
      rsize |= rsize >> 1;
      rsize |= rsize >> 2;
      rsize |= rsize >> 4;
      rsize |= rsize >> 8;
      rsize |= rsize >> 16;
      rsize += 1;
    }
    xptr = (unsigned char *) realloc(iptr, xofs + rsize + 10);
    if (!xptr)
    {
      free(iptr);
      return -1;
    }
    iptr = xptr;
    isize = rsize;
  }
  memcpy(iptr + xofs, exe + 12, xsize);
  {
    state->data = iptr;
    state->data_size = isize;
  }
  return 0;
}

static void * psf_file_fopen( const char * uri )
{
  return  XBMC->OpenFile(uri, 0);
}

static size_t psf_file_fread( void * buffer, size_t size, size_t count, void * handle )
{
  return XBMC->ReadFile(handle, buffer, size*count);
}

static int psf_file_fseek( void * handle, int64_t offset, int whence )
{
  return XBMC->SeekFile(handle, offset, whence) > -1?0:-1;
}

static int psf_file_fclose( void * handle )
{
  XBMC->CloseFile(handle);
}

static long psf_file_ftell( void * handle )
{
  return XBMC->GetFilePosition(handle);
}

const psf_file_callbacks psf_file_system =
{
  "\\/",
  psf_file_fopen,
  psf_file_fread,
  psf_file_fseek,
  psf_file_fclose,
  psf_file_ftell
};

#define BORK_TIME 0xC0CAC01A
static unsigned long parse_time_crap(const char *input)
{
  if (!input) return BORK_TIME;
  int len = strlen(input);
  if (!len) return BORK_TIME;
  int value = 0;
  {
    int i;
    for (i = len - 1; i >= 0; i--)
    {
      if ((input[i] < '0' || input[i] > '9') && input[i] != ':' && input[i] != ',' && input[i] != '.')
      {
        return BORK_TIME;
      }
    }
  }
  std::string foo = input;
  char *bar = (char *) &foo[0];
  char *strs = bar + foo.size() - 1;
  while (strs > bar && (*strs >= '0' && *strs <= '9'))
  {
    strs--;
  }
  if (*strs == '.' || *strs == ',')
  {
    // fraction of a second
    strs++;
    if (strlen(strs) > 3) strs[3] = 0;
    value = atoi(strs);
    switch (strlen(strs))
    {
      case 1:
        value *= 100;
        break;
      case 2:
        value *= 10;
        break;
    }
    strs--;
    *strs = 0;
    strs--;
  }
  while (strs > bar && (*strs >= '0' && *strs <= '9'))
  {
    strs--;
  }
  // seconds
  if (*strs < '0' || *strs > '9') strs++;
  value += atoi(strs) * 1000;
  if (strs > bar)
  {
    strs--;
    *strs = 0;
    strs--;
    while (strs > bar && (*strs >= '0' && *strs <= '9'))
    {
      strs--;
    }
    if (*strs < '0' || *strs > '9') strs++;
    value += atoi(strs) * 60000;
    if (strs > bar)
    {
      strs--;
      *strs = 0;
      strs--;
      while (strs > bar && (*strs >= '0' && *strs <= '9'))
      {
        strs--;
      }
      value += atoi(strs) * 3600000;
    }
  }
  return value;
}

static int psf_info_meta(void* context,
                         const char* name, const char* value)
{
  GSFContext* gsf = (GSFContext*)context;
  if (!strcasecmp(name, "length"))
    gsf->len = parse_time_crap(value);
  if (!strcasecmp(name, "title"))
    gsf->title = value;
  if (!strcasecmp(name, "artist"))
    gsf->artist = value;
}

void* Init(const char* strFile, unsigned int filecache, int* channels,
           int* samplerate, int* bitspersample, int64_t* totaltime,
           int* bitrate, AEDataFormat* format, const AEChannel** channelinfo)
{
  GSFContext* result = new GSFContext;
  result->pos = 0;

  if (psf_load(strFile, &psf_file_system, 0x22, 0, 0, psf_info_meta, result, 0) <= 0)
  {
    delete result;
    return NULL;
  }
  if (psf_load(strFile, &psf_file_system, 0x22, gsf_loader, &result->state, 0, 0, 0) < 0)
  {
    delete result;
    return NULL;
  }
  result->system.cpuIsMultiBoot = (result->state.entry >> 24 == 2);
  CPULoadRom(&result->system, result->state.data, result->state.data_size); 
  soundInit(&result->system, &result->output);
  soundReset(&result->system);
  CPUInit(&result->system);
  CPUReset(&result->system);
  
  *totaltime = result->len;
  static enum AEChannel map[3] = {
    AE_CH_FL, AE_CH_FR, AE_CH_NULL
  };
  *format = AE_FMT_S16NE;
  *channelinfo = map;
  *channels = 2;
  *bitspersample = 16;
  *bitrate = 0.0;
  *samplerate = result->sample_rate = 44100;
  result->len = result->sample_rate*4*(*totaltime)/1000;

  return result;
}

int ReadPCM(void* context, uint8_t* pBuffer, int size, int *actualsize)
{
  GSFContext* gsf = (GSFContext*)context;
  if (gsf->pos >= gsf->len)
    return 1;
  if (gsf->output.bytes_in_buffer == 0)
  {
    gsf->output.head = 0;
    CPULoop(&gsf->system, 250000);
  }

  int tocopy = std::min(size, (int)gsf->output.bytes_in_buffer);
  memcpy(pBuffer, &gsf->output.sample_buffer[gsf->output.head], tocopy);
  gsf->pos += tocopy;
  gsf->output.bytes_in_buffer -= tocopy;
  gsf->output.head += tocopy;
  *actualsize = tocopy;
  return 0;
}

int64_t Seek(void* context, int64_t time)
{
  GSFContext* gsf = (GSFContext*)context;
  if (time*gsf->sample_rate*4/1000 < gsf->pos)
  {
    CPUReset(&gsf->system);
    gsf->pos = 0;
  }
  
  int64_t left = time*gsf->sample_rate*4/1000-gsf->pos;
  while (left > 1024)
  {
    CPULoop(&gsf->system, 250000);
    gsf->pos += gsf->output.bytes_in_buffer;
    left -= gsf->output.bytes_in_buffer;
    gsf->output.head = gsf->output.bytes_in_buffer = 0;
  }

  return gsf->pos/(gsf->sample_rate*4)*1000;
}

bool DeInit(void* context)
{
  GSFContext* gsf = (GSFContext*)context;
  soundShutdown(&gsf->system);
  CPUCleanUp(&gsf->system);
  delete gsf;
}

bool ReadTag(const char* strFile, char* title, char* artist, int* length)
{
  GSFContext* gsf = new GSFContext;

  if (psf_load(strFile, &psf_file_system, 0x22, 0, 0, psf_info_meta, gsf, 0) <= 0)
  {
    delete gsf;
    return false;
  }

  strcpy(title, gsf->title.c_str());
  strcpy(artist, gsf->artist.c_str());
  *length = gsf->len/1000;

  delete gsf;
  return true;
}

int TrackCount(const char* strFile)
{
  return 1;
}
}
