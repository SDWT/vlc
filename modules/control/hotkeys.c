/*****************************************************************************
 * hotkeys.c: Hotkey handling for vlc
 *****************************************************************************
 * Copyright (C) 2004 VideoLAN
 * $Id$
 *
 * Authors: Sigmund Augdal <sigmunau@idi.ntnu.no>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>                                      /* malloc(), free() */

#include <vlc/vlc.h>
#include <vlc/intf.h>
#include <vlc/input.h>
#include <vlc/vout.h>
#include <vlc/aout.h>
#include <osd.h>

#include "vlc_keys.h"

#define BUFFER_SIZE 10

#define CHANNELS_NUMBER 4
#define VOLUME_TEXT_CHAN     p_intf->p_sys->p_channels[ 0 ]
#define VOLUME_WIDGET_CHAN   p_intf->p_sys->p_channels[ 1 ]
#define POSITION_TEXT_CHAN   p_intf->p_sys->p_channels[ 2 ]
#define POSITION_WIDGET_CHAN p_intf->p_sys->p_channels[ 3 ]
/*****************************************************************************
 * intf_sys_t: description and status of FB interface
 *****************************************************************************/
struct intf_sys_t
{
    vlc_mutex_t         change_lock;  /* mutex to keep the callback
                                       * and the main loop from
                                       * stepping on each others
                                       * toes */
    int                 p_keys[ BUFFER_SIZE ]; /* buffer that contains
                                                * keyevents */
    int                 i_size;        /* number of events in buffer */
    int                 p_channels[ CHANNELS_NUMBER ]; /* contains registered
                                                        * channel IDs */
    input_thread_t *    p_input;       /* pointer to input */
    vout_thread_t *     p_vout;        /* pointer to vout object */
};

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  Open    ( vlc_object_t * );
static void Close   ( vlc_object_t * );
static void Run     ( intf_thread_t * );
static int  GetKey  ( intf_thread_t *);
static int  KeyEvent( vlc_object_t *, char const *,
                      vlc_value_t, vlc_value_t, void * );
static int  ActionKeyCB( vlc_object_t *, char const *,
                         vlc_value_t, vlc_value_t, void * );
static void PlayBookmark( intf_thread_t *, int );
static void SetBookmark ( intf_thread_t *, int );
static void DisplayPosition( intf_thread_t *, vout_thread_t *, input_thread_t * );
static void DisplayVolume  ( intf_thread_t *, vout_thread_t *, audio_volume_t );
static void ClearChannels  ( intf_thread_t *, vout_thread_t * );

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
#define BOOKMARK1_TEXT N_("Playlist bookmark 1")
#define BOOKMARK2_TEXT N_("Playlist bookmark 2")
#define BOOKMARK3_TEXT N_("Playlist bookmark 3")
#define BOOKMARK4_TEXT N_("Playlist bookmark 4")
#define BOOKMARK5_TEXT N_("Playlist bookmark 5")
#define BOOKMARK6_TEXT N_("Playlist bookmark 6")
#define BOOKMARK7_TEXT N_("Playlist bookmark 7")
#define BOOKMARK8_TEXT N_("Playlist bookmark 8")
#define BOOKMARK9_TEXT N_("Playlist bookmark 9")
#define BOOKMARK10_TEXT N_("Playlist bookmark 10")
#define BOOKMARK_LONGTEXT N_( \
    "This option allows you to define playlist bookmarks.")

vlc_module_begin();
    set_description( _("Hotkeys management interface") );
    add_string( "bookmark1", NULL, NULL,
                BOOKMARK1_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark2", NULL, NULL,
                BOOKMARK2_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark3", NULL, NULL,
                BOOKMARK3_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark4", NULL, NULL,
                BOOKMARK4_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark5", NULL, NULL,
                BOOKMARK5_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark6", NULL, NULL,
                BOOKMARK6_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark7", NULL, NULL,
                BOOKMARK7_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark8", NULL, NULL,
                BOOKMARK8_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark9", NULL, NULL,
                BOOKMARK9_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );
    add_string( "bookmark10", NULL, NULL,
                BOOKMARK10_TEXT, BOOKMARK_LONGTEXT, VLC_FALSE );

    set_capability( "interface", 0 );
    set_callbacks( Open, Close );
vlc_module_end();

/*****************************************************************************
 * Open: initialize interface
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    intf_thread_t *p_intf = (intf_thread_t *)p_this;

    /* Allocate instance and initialize some members */
    p_intf->p_sys = malloc( sizeof( intf_sys_t ) );
    if( p_intf->p_sys == NULL )
    {
        msg_Err( p_intf, "out of memory" );
        return 1;
    }
    vlc_mutex_init( p_intf, &p_intf->p_sys->change_lock );
    p_intf->p_sys->i_size = 0;
    p_intf->pf_run = Run;

    p_intf->p_sys->p_input = NULL;
    p_intf->p_sys->p_vout = NULL;

    var_AddCallback( p_intf->p_vlc, "key-pressed", KeyEvent, p_intf );
    return 0;
}

/*****************************************************************************
 * Close: destroy interface
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    intf_thread_t *p_intf = (intf_thread_t *)p_this;

    if( p_intf->p_sys->p_input )
    {
        vlc_object_release( p_intf->p_sys->p_input );
    }
    if( p_intf->p_sys->p_vout )
    {
        vlc_object_release( p_intf->p_sys->p_vout );
    }
    /* Destroy structure */
    free( p_intf->p_sys );
}

/*****************************************************************************
 * Run: main loop
 *****************************************************************************/
static void Run( intf_thread_t *p_intf )
{
    playlist_t *p_playlist;
    input_thread_t *p_input;
    vout_thread_t *p_vout = NULL;
    vout_thread_t *p_last_vout;
    struct hotkey *p_hotkeys = p_intf->p_vlc->p_hotkeys;
    vlc_value_t val;
    int i;

    /* Initialize hotkey structure */
    for( i = 0; p_hotkeys[i].psz_action != NULL; i++ )
    {
        var_Create( p_intf->p_vlc, p_hotkeys[i].psz_action,
                    VLC_VAR_HOTKEY | VLC_VAR_DOINHERIT );

        var_AddCallback( p_intf->p_vlc, p_hotkeys[i].psz_action,
                         ActionKeyCB, NULL );
        var_Get( p_intf->p_vlc, p_hotkeys[i].psz_action, &val );
        var_Set( p_intf->p_vlc, p_hotkeys[i].psz_action, val );
    }

    while( !p_intf->b_die )
    {
        int i_key, i_action;

        /* Sleep a bit */
        msleep( INTF_IDLE_SLEEP );

        /* Update the input */
        if( p_intf->p_sys->p_input == NULL )
        {
            p_intf->p_sys->p_input = vlc_object_find( p_intf, VLC_OBJECT_INPUT,
                                                      FIND_ANYWHERE );
        }
        else if( p_intf->p_sys->p_input->b_dead )
        {
            vlc_object_release( p_intf->p_sys->p_input );
            p_intf->p_sys->p_input = NULL;
        }
        p_input = p_intf->p_sys->p_input;

        /* Update the vout */
        p_last_vout = p_intf->p_sys->p_vout;
        if( p_vout == NULL )
        {
            p_vout = vlc_object_find( p_intf, VLC_OBJECT_VOUT, FIND_ANYWHERE );
            p_intf->p_sys->p_vout = p_vout;
        }
        else if( p_vout->b_die )
        {
            vlc_object_release( p_vout );
            p_vout = NULL;
            p_intf->p_sys->p_vout = NULL;
        }

        /* Register OSD channels */
        if( p_vout && p_vout != p_last_vout )
        {
            for( i = 0; i < CHANNELS_NUMBER; i++ )
            {
                p_intf->p_sys->p_channels[ i ] =
                    vout_RegisterOSDChannel( p_vout );
            }
        }

        /* Find action triggered by hotkey */
        i_action = 0;
        i_key = GetKey( p_intf );
        for( i = 0; p_hotkeys[i].psz_action != NULL; i++ )
        {
            if( p_hotkeys[i].i_key == i_key )
            {
                 i_action = p_hotkeys[i].i_action;
            }
        }

        if( !i_action )
        {
            /* No key pressed, sleep a bit more */
            msleep( INTF_IDLE_SLEEP );
            continue;
        }

        if( i_action == ACTIONID_QUIT )
        {
            p_intf->p_vlc->b_die = VLC_TRUE;
            ClearChannels( p_intf, p_vout );
            vout_OSDMessage( p_intf, DEFAULT_CHAN, _( "Quit" ) );
            continue;
        }
        else if( i_action == ACTIONID_VOL_UP )
        {
            audio_volume_t i_newvol;
            aout_VolumeUp( p_intf, 1, &i_newvol );
            DisplayVolume( p_intf, p_vout, i_newvol );
        }
        else if( i_action == ACTIONID_VOL_DOWN )
        {
            audio_volume_t i_newvol;
            aout_VolumeDown( p_intf, 1, &i_newvol );
            DisplayVolume( p_intf, p_vout, i_newvol );
        }
        else if( i_action == ACTIONID_VOL_MUTE )
        {
            audio_volume_t i_newvol = -1;
            aout_VolumeMute( p_intf, &i_newvol );
            if( p_vout )
            {
                if( i_newvol == 0 )
                {
                    ClearChannels( p_intf, p_vout );
                    vout_OSDIcon( VLC_OBJECT( p_intf ), DEFAULT_CHAN,
                                  OSD_MUTE_ICON );
                }
                else
                {
                    DisplayVolume( p_intf, p_vout, i_newvol );
                }
            }
        }

        else if( i_action == ACTIONID_SUBDELAY_DOWN )
        {
            int64_t i_delay = var_GetTime( p_input, "spu-delay" );

            i_delay -= 10000;    /* 10 ms */

            var_SetTime( p_input, "spu-delay", i_delay );
            ClearChannels( p_intf, p_vout );
            vout_OSDMessage( p_intf, DEFAULT_CHAN, "Subtitle delay %i ms",
                                 (int)(i_delay/1000) );
        }
        else if( i_action == ACTIONID_SUBDELAY_UP )
        {
            int64_t i_delay = var_GetTime( p_input, "spu-delay" );

            i_delay += 10000;    /* 10 ms */

            var_SetTime( p_input, "spu-delay", i_delay );
            ClearChannels( p_intf, p_vout );
            vout_OSDMessage( p_intf, DEFAULT_CHAN, "Subtitle delay %i ms",
                                 (int)(i_delay/1000) );
        }
        else if( i_action == ACTIONID_FULLSCREEN && p_vout )
        {
            var_Get( p_vout, "fullscreen", &val );
            var_Set( p_vout, "fullscreen", (vlc_value_t)!val.b_bool );
        }
        else if( i_action == ACTIONID_PLAY )
        {
            p_playlist = vlc_object_find( p_intf, VLC_OBJECT_PLAYLIST,
                                          FIND_ANYWHERE );
            if( p_playlist )
            {
                playlist_Play( p_playlist );
                vlc_object_release( p_playlist );
            }
        }
        else if( i_action == ACTIONID_PLAY_PAUSE )
        {
            val.i_int = PLAYING_S;
            if( p_input )
            {
                var_Get( p_input, "state", &val );
            }
            if( p_input && val.i_int != PAUSE_S )
            {
                ClearChannels( p_intf, p_vout );
                vout_OSDIcon( VLC_OBJECT( p_intf ), DEFAULT_CHAN,
                              OSD_PAUSE_ICON );
                val.i_int = PAUSE_S;
                var_Set( p_input, "state", val );
            }
            else
            {
                p_playlist = vlc_object_find( p_intf, VLC_OBJECT_PLAYLIST,
                                              FIND_ANYWHERE );
                if( p_playlist )
                {
                    ClearChannels( p_intf, p_vout );
                    vout_OSDIcon( VLC_OBJECT( p_intf ), DEFAULT_CHAN,
                                  OSD_PLAY_ICON );
                    playlist_Play( p_playlist );
                    vlc_object_release( p_playlist );
                }
            }
        }
        else if( p_input )
        {
            /* FIXME --fenrir
             * How to get a valid value ?
             * That's not that easy with some special stream
             */
            vlc_bool_t b_seekable = VLC_TRUE;

            if( i_action == ACTIONID_PAUSE )
            {
                ClearChannels( p_intf, p_vout );
                vout_OSDIcon( VLC_OBJECT( p_intf ), DEFAULT_CHAN,
                              OSD_PAUSE_ICON );
                val.i_int = PAUSE_S;
                var_Set( p_input, "state", val );
            }
            else if( i_action == ACTIONID_JUMP_BACKWARD_10SEC && b_seekable )
            {
                val.i_time = -10000000;
                var_Set( p_input, "time-offset", val );
                DisplayPosition( p_intf, p_vout, p_input );
            }
            else if( i_action == ACTIONID_JUMP_FORWARD_10SEC && b_seekable )
            {
                val.i_time = 10000000;
                var_Set( p_input, "time-offset", val );
                DisplayPosition( p_intf, p_vout, p_input );
            }
            else if( i_action == ACTIONID_JUMP_BACKWARD_1MIN && b_seekable )
            {
                val.i_time = -60000000;
                var_Set( p_input, "time-offset", val );
                DisplayPosition( p_intf, p_vout, p_input );
            }
            else if( i_action == ACTIONID_JUMP_FORWARD_1MIN && b_seekable )
            {
                val.i_time = 60000000;
                var_Set( p_input, "time-offset", val );
                DisplayPosition( p_intf, p_vout, p_input );
            }
            else if( i_action == ACTIONID_JUMP_BACKWARD_5MIN && b_seekable )
            {
                val.i_time = -300000000;
                var_Set( p_input, "time-offset", val );
                DisplayPosition( p_intf, p_vout, p_input );
            }
            else if( i_action == ACTIONID_JUMP_FORWARD_5MIN && b_seekable )
            {
                val.i_time = 300000000;
                var_Set( p_input, "time-offset", val );
                DisplayPosition( p_intf, p_vout, p_input );
            }
            else if( i_action == ACTIONID_NEXT )
            {
                p_playlist = vlc_object_find( p_intf, VLC_OBJECT_PLAYLIST,
                                              FIND_ANYWHERE );
                if( p_playlist )
                {
                    playlist_Next( p_playlist );
                    vlc_object_release( p_playlist );
                }
            }
            else if( i_action == ACTIONID_PREV )
            {
                p_playlist = vlc_object_find( p_intf, VLC_OBJECT_PLAYLIST,
                                              FIND_ANYWHERE );
                if( p_playlist )
                {
                    playlist_Prev( p_playlist );
                    vlc_object_release( p_playlist );
                }
            }
            else if( i_action == ACTIONID_STOP )
            {
                p_playlist = vlc_object_find( p_intf, VLC_OBJECT_PLAYLIST,
                                              FIND_ANYWHERE );
                if( p_playlist )
                {
                    playlist_Stop( p_playlist );
                    vlc_object_release( p_playlist );
                }
            }
            else if( i_action == ACTIONID_FASTER )
            {
                vlc_value_t val; val.b_bool = VLC_TRUE;
                var_Set( p_input, "rate-faster", val );
            }
            else if( i_action == ACTIONID_SLOWER )
            {
                vlc_value_t val; val.b_bool = VLC_TRUE;
                var_Set( p_input, "rate-slower", val );
            }
            else if( i_action == ACTIONID_POSITION && b_seekable )
            {
                DisplayPosition( p_intf, p_vout, p_input );
            }
            else if( i_action >= ACTIONID_PLAY_BOOKMARK1 &&
                     i_action <= ACTIONID_PLAY_BOOKMARK10 )
            {
                PlayBookmark( p_intf, i_action - ACTIONID_PLAY_BOOKMARK1 + 1 );
            }
            else if( i_action >= ACTIONID_SET_BOOKMARK1 &&
                     i_action <= ACTIONID_SET_BOOKMARK10 )
            {
                SetBookmark( p_intf, i_action - ACTIONID_SET_BOOKMARK1 + 1 );
            }
        }
    }
}

static int GetKey( intf_thread_t *p_intf)
{
    vlc_mutex_lock( &p_intf->p_sys->change_lock );
    if ( p_intf->p_sys->i_size == 0 )
    {
        vlc_mutex_unlock( &p_intf->p_sys->change_lock );
        return -1;
    }
    else
    {
        int i_return = p_intf->p_sys->p_keys[ 0 ];
        int i;
        p_intf->p_sys->i_size--;
        for ( i = 0; i < BUFFER_SIZE - 1; i++)
        {
            p_intf->p_sys->p_keys[ i ] = p_intf->p_sys->p_keys[ i + 1 ];
        }
        vlc_mutex_unlock( &p_intf->p_sys->change_lock );
        return i_return;
    }
}

/*****************************************************************************
 * KeyEvent: callback for keyboard events
 *****************************************************************************/
static int KeyEvent( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    intf_thread_t *p_intf = (intf_thread_t *)p_data;
    vlc_mutex_lock( &p_intf->p_sys->change_lock );
    if ( p_intf->p_sys->i_size == BUFFER_SIZE )
    {
        msg_Warn( p_intf, "event buffer full, dropping keypress" );
        vlc_mutex_unlock( &p_intf->p_sys->change_lock );
        return VLC_EGENERIC;
    }
    else
    {
        p_intf->p_sys->p_keys[ p_intf->p_sys->i_size ] = newval.i_int;
        p_intf->p_sys->i_size++;
    }
    vlc_mutex_unlock( &p_intf->p_sys->change_lock );

    return VLC_SUCCESS;
}

static int ActionKeyCB( vlc_object_t *p_this, char const *psz_var,
                        vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    vlc_t *p_vlc = (vlc_t *)p_this;
    struct hotkey *p_hotkeys = p_vlc->p_hotkeys;
    int i;

    for( i = 0; p_hotkeys[i].psz_action != NULL; i++ )
    {
        if( !strcmp( p_hotkeys[i].psz_action, psz_var ) )
        {
            p_hotkeys[i].i_key = newval.i_int;
        }
    }

    return VLC_SUCCESS;
}

static void PlayBookmark( intf_thread_t *p_intf, int i_num )
{
    vlc_value_t val;
    int i_position;
    char psz_bookmark_name[11];
    playlist_t *p_playlist =
        vlc_object_find( p_intf, VLC_OBJECT_PLAYLIST, FIND_ANYWHERE );

    sprintf( psz_bookmark_name, "bookmark%i", i_num );
    var_Create( p_intf, psz_bookmark_name, VLC_VAR_STRING|VLC_VAR_DOINHERIT );
    var_Get( p_intf, psz_bookmark_name, &val );

    if( p_playlist )
    {
        char *psz_bookmark = strdup( val.psz_string );
        for( i_position = 0; i_position < p_playlist->i_size; i_position++)
        {
            if( !strcmp( psz_bookmark,
                         p_playlist->pp_items[i_position]->input.psz_uri ) )
            {
                playlist_Goto( p_playlist, i_position );
                break;
            }
        }
        vlc_object_release( p_playlist );
    }
}

static void SetBookmark( intf_thread_t *p_intf, int i_num )
{
    vlc_value_t val;
    playlist_t *p_playlist =
        vlc_object_find( p_intf, VLC_OBJECT_PLAYLIST, FIND_ANYWHERE );
    if( p_playlist )
    {
        char psz_bookmark_name[11];
        sprintf( psz_bookmark_name, "bookmark%i", i_num );
        var_Create( p_intf, psz_bookmark_name,
                    VLC_VAR_STRING|VLC_VAR_DOINHERIT );
        val.psz_string = strdup( p_playlist->pp_items[p_playlist->i_index]->input.psz_uri );
        var_Set( p_intf, psz_bookmark_name, val );
        msg_Info( p_intf, "setting playlist bookmark %i to %s", i_num,
                  val.psz_string );
        vlc_object_release( p_playlist );
    }
}

static void DisplayPosition( intf_thread_t *p_intf, vout_thread_t *p_vout,
                             input_thread_t *p_input )
{
    char psz_duration[MSTRTIME_MAX_SIZE];
    char psz_time[MSTRTIME_MAX_SIZE];
    vlc_value_t time, pos;
    mtime_t i_seconds;

    if( p_vout == NULL )
    {
        return;
    }
    ClearChannels( p_intf, p_vout );

    var_Get( p_input, "time", &time );
    i_seconds = time.i_time / 1000000;
    secstotimestr ( psz_time, i_seconds );

    var_Get( p_input, "length", &time );
    if( time.i_time > 0 )
    {
        secstotimestr( psz_duration, time.i_time / 1000000 );
        vout_OSDMessage( p_input, POSITION_TEXT_CHAN, "%s / %s",
                         psz_time, psz_duration );
    }
    else if( i_seconds > 0 )
    {
        vout_OSDMessage( p_input, POSITION_TEXT_CHAN, psz_time );
    }

    if( !p_vout->p_parent_intf || p_vout->b_fullscreen )
    {
        var_Get( p_input, "position", &pos );
        vout_OSDSlider( VLC_OBJECT( p_input ), POSITION_WIDGET_CHAN,
                        pos.f_float * 100, OSD_HOR_SLIDER );
    }
}

static void DisplayVolume( intf_thread_t *p_intf, vout_thread_t *p_vout,
                           audio_volume_t i_vol )
{
    if( p_vout == NULL )
    {
        return;
    }
    ClearChannels( p_intf, p_vout );

    if( !p_vout->p_parent_intf || p_vout->b_fullscreen )
    {
        vout_OSDSlider( VLC_OBJECT( p_vout ), VOLUME_WIDGET_CHAN,
            i_vol*100/AOUT_VOLUME_MAX, OSD_VERT_SLIDER );
    }
    else
    {
        vout_OSDMessage( p_vout, VOLUME_TEXT_CHAN, "Vol %d%%",
                         2*i_vol*100/AOUT_VOLUME_MAX );
    }
}

static void ClearChannels( intf_thread_t *p_intf, vout_thread_t *p_vout )
{
    int i;

    vout_ClearOSDChannel( p_vout, DEFAULT_CHAN );
    for( i = 0; i < CHANNELS_NUMBER; i++ )
    {
        vout_ClearOSDChannel( p_vout, p_intf->p_sys->p_channels[ i ] );
    }
}
