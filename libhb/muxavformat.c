/* muxavformat.c

   Copyright (c) 2003-2025 HandBrake Team
   This file is part of the HandBrake source code
   Homepage: <http://handbrake.fr/>.
   It may be used under the terms of the GNU General Public License v2.
   For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

#include <time.h>
#include "libavcodec/bsf.h"
#include "libavformat/avformat.h"
#include "libavutil/avstring.h"

#include "handbrake/handbrake.h"
#include "handbrake/lang.h"
#include "handbrake/hbffmpeg.h"

struct hb_mux_data_s
{
    enum
    {
        MUX_TYPE_VIDEO,
        MUX_TYPE_AUDIO,
        MUX_TYPE_SUBTITLE
    } type;

    AVFormatContext * oc;
    AVStream        * st;

    int64_t  duration;

    hb_buffer_t * delay_buf;

    int64_t  prev_chapter_tc;
    int16_t  current_chapter;

    AVBSFContext            * bitstream_context;
};

struct hb_mux_object_s
{
    HB_MUX_COMMON;

    hb_job_t          * job;

    AVFormatContext   * oc;
    AVRational          time_base;
    AVPacket          * pkt;
    AVPacket          * empty_pkt;

    int                 ntracks;
    hb_mux_data_t    ** tracks;
};

enum
{
    META_HB,
    META_MUX_MP4,
    META_MUX_MOV,
    META_MUX_MKV,
    META_MUX_WEBM,
    META_MUX_LAST
};

const char *metadata_keys[][META_MUX_LAST] =
{
    {"Name",                "title",                  "com.apple.quicktime.displayname",      "TITLE"},
    {"Artist",              "artist",                 "com.apple.quicktime.artist",           "ARTIST"},
    {"AlbumArtist",         "album_artist",           NULL,                                   NULL},
    {"Album",               "album",                  "com.apple.quicktime.album",            NULL},
    {"Genre",               "genre",                  "com.apple.quicktime.genre",            "GENRE"},
    {"ReleaseDate",         "date",                   "com.apple.quicktime.creationdate",     "DATE_RELEASED"},
    {"CreationTime",        "creation_time",          NULL,                                   NULL},
    {"Track",               "track",                  "com.apple.quicktime.track",            NULL},
    {"Show",                "show",                   NULL,                                   NULL},
    {"Network",             "network",                NULL,                                   NULL},
    {"Episode ID",          "episode_id",             NULL,                                   NULL},
    {"Episode Sort",        "episode_sort",           NULL,                                   NULL},
    {"Season Number",       "season_number",          NULL,                                   NULL},
    {"Rating",              "rating",                 NULL,                                   NULL},
    {"Media Type",          "media_type",             NULL,                                   NULL},
    {"HD Video",            "hd_video",               NULL,                                   NULL},
    {"Description",         "description",            "com.apple.quicktime.description",      "DESCRIPTION"},
    {"LongDescription",     "synopsis",               "com.apple.quicktime.information",      "SYNOPSIS"},
    {"SeriesDescription",   "series_description",     NULL,                                   NULL},
    {"Comment",             "comment",                "com.apple.quicktime.comment",          "SUMMARY"},
    {"Grouping",            "grouping",               NULL,                                   NULL},
    {"Compilation",         "compilation",            NULL,                                   NULL},
    {"Tempo",               "tmpo",                   NULL,                                   "BPM"},

    {"Subtitle",            "subtitle",               NULL,                                   "SUBTITLE"},
    {"SongDescription",     "song_description",       NULL,                                   NULL},
    {"Director",            "director",               "com.apple.quicktime.director",         "DIRECTOR"},
    {"ArtDirector",         "art_director",           NULL,                                   "DIRECTOR_OF_PHOTOGRAPHY"},
    {"Composer",            "composer",               "com.apple.quicktime.composer",         "COMPOSER"},
    {"Arranger",            "arranger",               "com.apple.quicktime.arranger",         "ARRANGER"},
    {"Author",              "author",                 "com.apple.quicktime.author",           "WRITTEN_BY"},
    {"Acknowledgement",     "acknowledgement",        NULL,                                   NULL},
    {"Conductor",           "conductor",              NULL,                                   "CONDUCTOR"},
    {"Lyrics",              "lyrics",                 NULL,                                   "LYRICS"},
    {"Keywords",            "keywords",               "com.apple.quicktime.keywords",         "KEYWORDS"},
    {"Copyright",           "copyright",              "com.apple.quicktime.copyright",        "COPYRIGHT"},

    {"WorkName",            "work_name",              NULL,                                   NULL},
    {"MovementName",        "movement_name",          NULL,                                   NULL},
    {"MovementNumber",      "movement_number",        NULL,                                   NULL},
    {"MovementShow",        "movement_count",         NULL,                                   NULL},
    {"ShowWorkAndMovement", "show_work_and_movement", NULL,                                   NULL},

    {"LinearNotes",         "linear_notes",           NULL,                                   NULL},
    {"RecordCompany",       "make",                   "com.apple.quicktime.make",             NULL},
    {"OriginalArtist",      "original_artist",        "com.apple.quicktime.originalartist",   NULL},
    {"PhonogramRights",     "phonogram_rights",       "com.apple.quicktime.phonogramrights",  NULL},
    {"Producer",            "producer",               "com.apple.quicktime.producer",         "PRODUCER"},
    {"Performers",          "performers",             "com.apple.quicktime.performer",        NULL},
    {"Publisher",           "publisher",              "com.apple.quicktime.publisher",        "PUBLISHER"},
    {"SoundEngineer",       "sound_engineer",         NULL,                                   "SOUND_ENGINEER"},
    {"Soloist",             "soloist",                NULL,                                   "LEAD_PERFORMER"},
    {"Credits",             "original_source",        "com.apple.quicktime.credits",          NULL},
    {"Thanks",              "thanks",                 NULL,                                   "THANKS_TO"},
    {"OnlineExtra",         "URL",                    NULL,                                   NULL},
    {"ExecutiveProducer",   "executive_producer",     NULL,                                   "EXECUTIVE_PRODUCER"},

    {"iTunesU",             "itunes_u",               NULL,                                   NULL},
    {"Podcast",             "podcast",                NULL,                                   NULL},
    {"Category",            "category",               NULL,                                   NULL},

    {"ContentID",           "content_id",             NULL,                                   NULL},
    {"ArtistID",            "artist_id",              NULL,                                   NULL},
    {"PlaylistID",          "playlist_id",            NULL,                                   NULL},
    {"GenreID",             "genre_id",               NULL,                                   NULL},
    {"ComposerID",          "composer_id",            NULL,                                   NULL},
    {"XID",                 "xid",                    NULL,                                   NULL},

    {"iTunEXTC",            "iTunEXTC",               NULL,                                   NULL},
    {"iTunMOVI",            "iTunMOVI",               NULL,                                   NULL},
    {"Location",            "location",               "com.apple.quicktime.location.ISO6709", NULL},
    {NULL}
};

static const char * lookup_meta_mux_key(int meta_mux, const char * hb_key)
{
    int ii;

    for (ii = 0; metadata_keys[ii][META_HB] != NULL; ii++)
    {
        if (!strcmp(hb_key, metadata_keys[ii][META_HB]))
        {
            return metadata_keys[ii][meta_mux];
        }
    }
    return NULL;
}

const char * hb_lookup_meta_key(const char * mux_key)
{
    int ii, jj;

    for (ii = 0; metadata_keys[ii][META_HB] != NULL; ii++)
    {
        for (jj = 0; jj < META_MUX_LAST; jj++)
        {
            if (metadata_keys[ii][jj] != NULL &&
                !strcmp(mux_key, metadata_keys[ii][jj]))
            {
                return metadata_keys[ii][META_HB];
            }
        }
    }
    return NULL;
}

static char* lookup_lang_code(int mux, char *iso639_2)
{
    iso639_lang_t   *lang;
    char *out = NULL;

    switch (mux)
    {
        case HB_MUX_AV_MP4:
            out = iso639_2;
            break;
        case HB_MUX_AV_MKV:
        case HB_MUX_AV_WEBM: // webm is a subset of mkv
            // MKV lang codes should be ISO-639-2B if it exists,
            // else ISO-639-2
            lang =  lang_for_code2( iso639_2 );
            out = lang->iso639_2b && *lang->iso639_2b ? lang->iso639_2b :
                                                        lang->iso639_2;
            break;
        default:
            break;
    }
    return out;
}

const char * get_subtitle_muxer(int source)
{
    switch (source)
    {
        case CC608SUB:
        case CC708SUB:
        case SSASUB:
        case IMPORTSSA:
            return "ass";
        case UTF8SUB:
        case TX3GSUB:
        case IMPORTSRT:
            return "srt";
        case PGSSUB:
            return "sup";
    }

    return NULL;
}

static int set_extradata(hb_data_t *extradata, uint8_t **priv_data, int *priv_size)
{
    if (*priv_data)
    {
        av_freep(priv_data);
        *priv_size = 0;
    }

    if (extradata && extradata->size > 0)
    {
        // libavformat can over-read the buffer by up to 8 bytes
        // when it fills it's get_bits cache.
        //
        // So allocate extra bytes
        *priv_size = extradata->size;
        *priv_data = av_mallocz(extradata->size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (priv_data == NULL)
        {
            hb_error("extradata: malloc failure");
            return 1;
        }
        memcpy(*priv_data, extradata->bytes, extradata->size);
    }
    return 0;
}

/**********************************************************************
 * avformatInit
 **********************************************************************
 * Allocates hb_mux_data_t structures, create file and write headers
 *********************************************************************/
static int avformatInit( hb_mux_object_t * m )
{
    hb_job_t   * job   = m->job;
    hb_audio_t    * audio;
    hb_mux_data_t * track;
    int meta_mux;
    int max_tracks;
    int ii, jj, ret;

    int clock_min, clock_max, clock;
    hb_video_framerate_get_limits(&clock_min, &clock_max, &clock);

    const char *muxer_name = NULL;

    uint8_t         default_track_flag = 1;
    uint8_t         need_fonts = 0;
    char *lang;

    m->pkt = av_packet_alloc();
    m->empty_pkt = av_packet_alloc();

    if (m->pkt == NULL || m->empty_pkt == NULL)
    {
        hb_error("muxavformat: av_packet_alloc failed");
        goto error;
    }

    max_tracks = 1 + hb_list_count( job->list_audio ) +
                     hb_list_count( job->list_subtitle );
    m->tracks = calloc(max_tracks, sizeof(hb_mux_data_t*));

    AVDictionary * av_opts = NULL;
    switch (job->mux)
    {
        case HB_MUX_AV_MP4:
            m->time_base.num = 1;
            m->time_base.den = 90000;
            if( job->ipod_atom )
                muxer_name = "ipod";
            else
                muxer_name = "mp4";
            meta_mux = META_MUX_MP4;

            av_dict_set(&av_opts, "brand", "mp42", 0);
            av_dict_set(&av_opts, "strict", "experimental", 0);
            if (job->optimize)
                av_dict_set(&av_opts, "movflags", "faststart+disable_chpl+write_colr", 0);
            else
                av_dict_set(&av_opts, "movflags", "+disable_chpl+write_colr", 0);
            break;

        case HB_MUX_AV_MKV:
            // libavformat is essentially hard coded such that it only
            // works with a timebase of 1/1000
            m->time_base.num = 1;
            m->time_base.den = 1000;
            muxer_name = "matroska";
            meta_mux = META_MUX_MKV;
            av_dict_set(&av_opts, "default_mode", "passthrough", 0);
            break;

        case HB_MUX_AV_WEBM:
            // libavformat is essentially hard coded such that it only
            // works with a timebase of 1/1000
            m->time_base.num = 1;
            m->time_base.den = 1000;
            muxer_name = "webm";
            meta_mux = META_MUX_WEBM;
            av_dict_set(&av_opts, "default_mode", "passthrough", 0);
            break;

        default:
        {
            hb_error("Invalid Mux %x", job->mux);
            goto error;
        }
    }

    ret = avformat_alloc_output_context2(&m->oc, NULL, muxer_name, job->file);
    if (ret < 0)
    {
        hb_error( "Could not initialize avformat context." );
        goto error;
    }

    ret = avio_open2(&m->oc->pb, job->file, AVIO_FLAG_WRITE,
                     &m->oc->interrupt_callback, NULL);
    if (ret < 0)
    {
        if (ret == -2)
        {
            hb_error("avio_open2 failed, errno -2: Could not write to indicated output file. Please check destination path and file permissions");
        }
        else
        {
            hb_error("avio_open2 failed, errno %d", ret);
        }
        goto error;
    }

    /* Video track */
    track = m->tracks[m->ntracks++] = calloc(1, sizeof( hb_mux_data_t ) );
    job->mux_data = track;

    track->type = MUX_TYPE_VIDEO;
    track->prev_chapter_tc = AV_NOPTS_VALUE;
    track->st = avformat_new_stream(m->oc, NULL);
    if (track->st == NULL)
    {
        hb_error("Could not initialize video stream");
        goto error;
    }

    track->st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

    switch (job->vcodec)
    {
        case HB_VCODEC_X264_8BIT:
        case HB_VCODEC_X264_10BIT:
        case HB_VCODEC_VT_H264:
        case HB_VCODEC_FFMPEG_VCE_H264:
        case HB_VCODEC_FFMPEG_NVENC_H264:
        case HB_VCODEC_FFMPEG_QSV_H264:
        case HB_VCODEC_FFMPEG_MF_H264:
            track->st->codecpar->codec_id = AV_CODEC_ID_H264;
            if (job->mux == HB_MUX_AV_MP4 && job->inline_parameter_sets)
            {
                track->st->codecpar->codec_tag = MKTAG('a','v','c','3');
            }
            else
            {
                track->st->codecpar->codec_tag = MKTAG('a','v','c','1');
            }
            break;

        case HB_VCODEC_FFMPEG_MPEG4:
            track->st->codecpar->codec_id = AV_CODEC_ID_MPEG4;
            break;

        case HB_VCODEC_FFMPEG_MPEG2:
            track->st->codecpar->codec_id = AV_CODEC_ID_MPEG2VIDEO;
            break;

        case HB_VCODEC_FFMPEG_VP8:
            track->st->codecpar->codec_id = AV_CODEC_ID_VP8;
            break;

        case HB_VCODEC_FFMPEG_VP9:
        case HB_VCODEC_FFMPEG_VP9_10BIT:
            track->st->codecpar->codec_id = AV_CODEC_ID_VP9;
            break;

        case HB_VCODEC_SVT_AV1:
        case HB_VCODEC_SVT_AV1_10BIT:
        case HB_VCODEC_FFMPEG_NVENC_AV1:
        case HB_VCODEC_FFMPEG_NVENC_AV1_10BIT:
        case HB_VCODEC_FFMPEG_VCE_AV1:
        case HB_VCODEC_FFMPEG_MF_AV1:
            track->st->codecpar->codec_id = AV_CODEC_ID_AV1;
            break;

        case HB_VCODEC_FFMPEG_QSV_AV1_10BIT:
        case HB_VCODEC_FFMPEG_QSV_AV1:
        {
            const AVBitStreamFilter  *bsf;
            AVBSFContext             *ctx;
            int                       ret;

            track->st->codecpar->codec_id = AV_CODEC_ID_AV1;

            bsf = av_bsf_get_by_name("extract_extradata");
            ret = av_bsf_alloc(bsf, &ctx);
            if (ret < 0)
            {
                hb_error("AV1 bitstream filter: alloc failure");
                goto error;
            }

            track->bitstream_context = ctx;
            if (track->bitstream_context != NULL)
            {
                avcodec_parameters_copy(track->bitstream_context->par_in,
                                       track->st->codecpar);
                ret = av_bsf_init(track->bitstream_context);
                if (ret < 0)
                {
                    hb_error("AV1 bitstream filter: init failure");
                    goto error;
                }
            }
        } break;

        case HB_VCODEC_THEORA:
            track->st->codecpar->codec_id = AV_CODEC_ID_THEORA;
            break;

        case HB_VCODEC_X265_8BIT:
        case HB_VCODEC_X265_10BIT:
        case HB_VCODEC_X265_12BIT:
        case HB_VCODEC_X265_16BIT:
        case HB_VCODEC_VT_H265:
        case HB_VCODEC_VT_H265_10BIT:
        case HB_VCODEC_FFMPEG_VCE_H265:
        case HB_VCODEC_FFMPEG_VCE_H265_10BIT:
        case HB_VCODEC_FFMPEG_NVENC_H265:
        case HB_VCODEC_FFMPEG_NVENC_H265_10BIT:
        case HB_VCODEC_FFMPEG_QSV_H265:
        case HB_VCODEC_FFMPEG_QSV_H265_10BIT:
        case HB_VCODEC_FFMPEG_MF_H265:
            track->st->codecpar->codec_id  = AV_CODEC_ID_HEVC;
            if (job->mux == HB_MUX_AV_MP4 && job->inline_parameter_sets)
            {
                track->st->codecpar->codec_tag = MKTAG('h','e','v','1');
            }
            else
            {
                track->st->codecpar->codec_tag = MKTAG('h','v','c','1');
            }
            break;

        case HB_VCODEC_FFMPEG_FFV1:
            track->st->codecpar->codec_id = AV_CODEC_ID_FFV1;
            break;

        default:
            hb_error("muxavformat: Unknown video codec: %x", job->vcodec);
            goto error;
    }

    if (set_extradata(job->extradata, &track->st->codecpar->extradata, &track->st->codecpar->extradata_size))
    {
        goto error;
    }

    track->st->sample_aspect_ratio.num        = job->par.num;
    track->st->sample_aspect_ratio.den        = job->par.den;
    track->st->codecpar->sample_aspect_ratio.num = job->par.num;
    track->st->codecpar->sample_aspect_ratio.den = job->par.den;
    track->st->codecpar->width                   = job->width;
    track->st->codecpar->height                  = job->height;
    track->st->codecpar->format                  = job->output_pix_fmt;
    track->st->disposition |= AV_DISPOSITION_DEFAULT;

    track->st->codecpar->color_primaries = hb_output_color_prim(job);
    track->st->codecpar->color_trc       = hb_output_color_transfer(job);
    track->st->codecpar->color_space     = hb_output_color_matrix(job);
    track->st->codecpar->color_range     = job->color_range;
    track->st->codecpar->chroma_location = job->chroma_location;

    if (job->color_transfer == HB_COLR_TRA_SMPTEST2084)
    {
        if (job->mastering.has_primaries || job->mastering.has_luminance)
        {
            AVMasteringDisplayMetadata mastering = hb_mastering_hb_to_ff(job->mastering);

            uint8_t *mastering_data = av_malloc(sizeof(AVMasteringDisplayMetadata));
            memcpy(mastering_data, &mastering, sizeof(AVMasteringDisplayMetadata));

            av_packet_side_data_add(&track->st->codecpar->coded_side_data,
                                    &track->st->codecpar->nb_coded_side_data,
                                    AV_PKT_DATA_MASTERING_DISPLAY_METADATA,
                                    mastering_data,
                                    sizeof(AVMasteringDisplayMetadata), 0);
        }

        if (job->coll.max_cll && job->coll.max_fall)
        {
            AVContentLightMetadata coll;
            coll.MaxCLL = job->coll.max_cll;
            coll.MaxFALL = job->coll.max_fall;

            uint8_t *coll_data = av_malloc(sizeof(AVContentLightMetadata));
            memcpy(coll_data, &coll, sizeof(AVContentLightMetadata));

            av_packet_side_data_add(&track->st->codecpar->coded_side_data,
                                    &track->st->codecpar->nb_coded_side_data,
                                    AV_PKT_DATA_CONTENT_LIGHT_LEVEL,
                                    coll_data,
                                    sizeof(AVContentLightMetadata), 0);
        }
    }

    if (job->ambient.ambient_illuminance.num && job->ambient.ambient_illuminance.den)
    {
        AVAmbientViewingEnvironment ambient = hb_ambient_hb_to_ff(job->ambient);

        uint8_t *ambient_data = av_malloc(sizeof(AVAmbientViewingEnvironment));
        memcpy(ambient_data, &ambient, sizeof(AVAmbientViewingEnvironment));

        av_packet_side_data_add(&track->st->codecpar->coded_side_data,
                                &track->st->codecpar->nb_coded_side_data,
                                AV_PKT_DATA_AMBIENT_VIEWING_ENVIRONMENT,
                                ambient_data,
                                sizeof(AVAmbientViewingEnvironment), 0);
    }

    if (job->passthru_dynamic_hdr_metadata & HB_HDR_DYNAMIC_METADATA_DOVI)
    {
        if (job->dovi.dv_profile == 5 && job->mux == HB_MUX_AV_MP4)
        {
            if (track->st->codecpar->codec_id == AV_CODEC_ID_HEVC)
            {
                track->st->codecpar->codec_tag = MKTAG('d','v','h','1');
            }
        }
        AVDOVIDecoderConfigurationRecord dovi = hb_dovi_hb_to_ff(job->dovi);

        uint8_t *dovi_data = av_malloc(sizeof(AVDOVIDecoderConfigurationRecord));
        memcpy(dovi_data, &dovi, sizeof(AVDOVIDecoderConfigurationRecord));

        av_packet_side_data_add(&track->st->codecpar->coded_side_data,
                                &track->st->codecpar->nb_coded_side_data,
                                AV_PKT_DATA_DOVI_CONF,
                                dovi_data,
                                sizeof(AVDOVIDecoderConfigurationRecord), 0);

        m->oc->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
    }

    hb_rational_t vrate = job->vrate;
    hb_rational_t clock_vrate = { clock, av_rescale(vrate.den, clock, vrate.num)};
    int standard_rate = 0;

    // Check if the vrate is similar to a standard rate that we have in our hb_video_rates table.
    // Because of rounding errors and approximations made while
    // measuring framerate, the actual value may not be exact.  So
    // we look for rates that are "close"
    const hb_rate_t *video_framerate = NULL;
    while ((video_framerate = hb_video_framerate_get_next(video_framerate)) != NULL)
    {
        if (abs(clock_vrate.den - video_framerate->rate) < 10)
        {
            vrate.num = clock;
            vrate.den = video_framerate->rate;
            standard_rate = 1;
            break;
        }
    }

    hb_reduce(&vrate.num, &vrate.den, vrate.num, vrate.den);

    if (job->mux == HB_MUX_AV_MP4 && standard_rate &&
        job->cfr == 1 && vrate.den * 90000L % vrate.num)
    {
        // Set the the correct video time base to avoid
        // timestamps jitter when using NTSC framerates
        track->st->time_base.num = vrate.den;
        track->st->time_base.den = vrate.num;
    }
    else
    {
        track->st->time_base = m->time_base;
    }

    track->st->avg_frame_rate.num = vrate.num;
    track->st->avg_frame_rate.den = vrate.den;

    /* add the audio tracks */
    for(ii = 0; ii < hb_list_count( job->list_audio ); ii++ )
    {
        audio = hb_list_item( job->list_audio, ii );
        track = m->tracks[m->ntracks++] = calloc(1, sizeof( hb_mux_data_t ) );
        audio->priv.mux_data = track;

        track->type = MUX_TYPE_AUDIO;

        track->st = avformat_new_stream(m->oc, NULL);
        if (track->st == NULL)
        {
            hb_error("Could not initialize audio stream");
            goto error;
        }

        track->st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        track->st->codecpar->initial_padding = audio->priv.init_delay *
                                        audio->config.out.samplerate / 90000;
        track->st->codecpar->frame_size = audio->config.out.samples_per_frame;
        if (job->mux == HB_MUX_AV_MP4)
        {
            track->st->time_base.num = 1;
            track->st->time_base.den = audio->config.out.samplerate;
        }
        else
        {
            track->st->time_base = m->time_base;
        }

        // Some containers don't need metadata for some audio formats,
        // and for some formats extradata will be generated automatically,
        // set only what's needed
        int need_extradata = 0;
        switch (audio->config.out.codec & HB_ACODEC_MASK)
        {
            case HB_ACODEC_DCA:
            case HB_ACODEC_DCA_HD:
                track->st->codecpar->codec_id = AV_CODEC_ID_DTS;
                break;
            case HB_ACODEC_AC3:
                track->st->codecpar->codec_id = AV_CODEC_ID_AC3;
                break;
            case HB_ACODEC_FFEAC3:
                track->st->codecpar->codec_id = AV_CODEC_ID_EAC3;
                break;
            case HB_ACODEC_FFTRUEHD:
                track->st->codecpar->codec_id = AV_CODEC_ID_TRUEHD;
                break;
            case HB_ACODEC_MP2:
                track->st->codecpar->codec_id = AV_CODEC_ID_MP2;
                break;
            case HB_ACODEC_LAME:
            case HB_ACODEC_MP3:
                track->st->codecpar->codec_id = AV_CODEC_ID_MP3;
                break;
            case HB_ACODEC_VORBIS:
                track->st->codecpar->codec_id = AV_CODEC_ID_VORBIS;
                need_extradata = 1;
                break;
            case HB_ACODEC_OPUS:
                track->st->codecpar->codec_id = AV_CODEC_ID_OPUS;
                need_extradata = 1;
                break;
            case HB_ACODEC_FFALAC:
            case HB_ACODEC_FFALAC24:
                track->st->codecpar->codec_id = AV_CODEC_ID_ALAC;
                need_extradata = 1;
                break;
            case HB_ACODEC_FFFLAC:
            case HB_ACODEC_FFFLAC24:
                track->st->codecpar->codec_id = AV_CODEC_ID_FLAC;
                need_extradata = 1;
                break;
            case HB_ACODEC_FFAAC:
            case HB_ACODEC_CA_AAC:
            case HB_ACODEC_CA_HAAC:
            case HB_ACODEC_FDK_AAC:
            case HB_ACODEC_FDK_HAAC:
                track->st->codecpar->codec_id = AV_CODEC_ID_AAC;
                need_extradata = 1;

                // AAC from pass-through source may be ADTS.
                // Therefore inserting "aac_adtstoasc" bitstream filter is
                // preferred.
                // The filter does nothing for non-ADTS bitstream.
                if (audio->config.out.codec == HB_ACODEC_AAC_PASS)
                {
                    const AVBitStreamFilter  * bsf;
                    AVBSFContext             * ctx;
                    int                        ret;

                    bsf = av_bsf_get_by_name("aac_adtstoasc");
                    ret = av_bsf_alloc(bsf, &ctx);
                    if (ret < 0)
                    {
                        hb_error("AAC bitstream filter: alloc failure");
                        goto error;
                    }
                    ctx->time_base_in.num = 1;
                    ctx->time_base_in.den = audio->config.out.samplerate;
                    track->bitstream_context = ctx;
                }
                break;
            default:
                hb_error("muxavformat: Unknown audio codec: %x",
                         audio->config.out.codec);
                goto error;
        }

        if (need_extradata)
        {
            if (set_extradata(audio->priv.extradata,
                              &track->st->codecpar->extradata,
                              &track->st->codecpar->extradata_size))
            {
                goto error;
            }
        }

        if (track->bitstream_context != NULL)
        {
            int ret;

            avcodec_parameters_copy(track->bitstream_context->par_in,
                                    track->st->codecpar);
            ret = av_bsf_init(track->bitstream_context);
            if (ret < 0)
            {
                hb_error("bitstream filter: init failure");
                goto error;
            }
        }

        if( default_track_flag )
        {
            track->st->disposition |= AV_DISPOSITION_DEFAULT;
            default_track_flag = 0;
        }

        lang = lookup_lang_code(job->mux, audio->config.lang.iso639_2 );
        if (lang != NULL)
        {
            av_dict_set(&track->st->metadata, "language", lang, 0);
        }
        track->st->codecpar->sample_rate = audio->config.out.samplerate;
        if (audio->config.out.codec & HB_ACODEC_PASS_FLAG)
        {
            AVChannelLayout ch_layout = {0};
            av_channel_layout_from_mask(&ch_layout, audio->config.in.channel_layout);
            track->st->codecpar->ch_layout = ch_layout;
        }
        else
        {
            AVChannelLayout ch_layout = {0};
            av_channel_layout_from_mask(&ch_layout, hb_ff_mixdown_xlat(audio->config.out.mixdown, NULL));
            track->st->codecpar->ch_layout = ch_layout;
        }

        // Set audio track title
        const char *name = audio->config.out.name;
        if (name != NULL && name[0] != 0)
        {
            av_dict_set(&track->st->metadata, "title", name, 0);
            if (job->mux == HB_MUX_AV_MP4)
            {
                // Some software (MPC, mediainfo) use hdlr description
                // for track title
                av_dict_set(&track->st->metadata, "handler_name", name, 0);
            }
        }
    }

    // Check for audio track associations
    for (ii = 0; ii < hb_list_count(job->list_audio); ii++)
    {
        audio = hb_list_item(job->list_audio, ii);
        switch (audio->config.out.codec & HB_ACODEC_MASK)
        {
            case HB_ACODEC_FFAAC:
            case HB_ACODEC_CA_AAC:
            case HB_ACODEC_CA_HAAC:
            case HB_ACODEC_FDK_AAC:
            case HB_ACODEC_FDK_HAAC:
                break;

            default:
            {
                // Mark associated fallback audio tracks for any non-aac track
                for(jj = 0; jj < hb_list_count( job->list_audio ); jj++ )
                {
                    hb_audio_t    * fallback;
                    int             codec;

                    if (ii == jj) continue;

                    fallback = hb_list_item( job->list_audio, jj );
                    codec = fallback->config.out.codec & HB_ACODEC_MASK;
                    if (fallback->config.index == audio->config.index &&
                        (codec == HB_ACODEC_FFAAC ||
                         codec == HB_ACODEC_CA_AAC ||
                         codec == HB_ACODEC_CA_HAAC ||
                         codec == HB_ACODEC_FDK_AAC ||
                         codec == HB_ACODEC_FDK_HAAC))
                    {
                        hb_mux_data_t * fallback_track;
                        AVPacketSideData *sd;

                        track = audio->priv.mux_data;
                        fallback_track = fallback->priv.mux_data;
                        sd = av_packet_side_data_new(&track->st->codecpar->coded_side_data,
                                                     &track->st->codecpar->nb_coded_side_data,
                                                     AV_PKT_DATA_FALLBACK_TRACK,
                                                     sizeof(int), 0);

                        if (sd != NULL)
                        {
                            int *data = (int *)sd->data;
                            *data = fallback_track->st->index;
                        }
                    }
                }
            } break;
        }
    }

    int subtitle_default = -1;
    for( ii = 0; ii < hb_list_count( job->list_subtitle ); ii++ )
    {
        hb_subtitle_t *subtitle = hb_list_item( job->list_subtitle, ii );

        if( subtitle->config.dest == PASSTHRUSUB )
        {
            if ( subtitle->config.default_track )
                subtitle_default = ii;
        }
    }
    // Quicktime requires that at least one subtitle is enabled,
    // else it doesn't show any of the subtitles.
    // So check to see if any of the subtitles are flagged to be
    // the default.  The default will be the enabled track, else
    // enable the first track.
    if (job->mux == HB_MUX_AV_MP4 && subtitle_default == -1)
    {
        subtitle_default = 0;
    }

    for( ii = 0; ii < hb_list_count( job->list_subtitle ); ii++ )
    {
        hb_subtitle_t   * subtitle;
        AVFormatContext * oc;
        const char      * subtitle_muxer_name = NULL;

        subtitle = hb_list_item( job->list_subtitle, ii );
        if (subtitle->config.dest != PASSTHRUSUB)
            continue;

        track = m->tracks[m->ntracks++] = calloc(1, sizeof( hb_mux_data_t ) );
        subtitle->mux_data = track;

        if (subtitle->config.external_filename != NULL)
        {
            subtitle_muxer_name = get_subtitle_muxer(subtitle->source);
            if (subtitle_muxer_name == NULL)
            {
                hb_error( "No muxer for subtitle source %d", subtitle->source );
                goto error;
            }
            ret = avformat_alloc_output_context2(&track->oc, NULL,
                subtitle_muxer_name, subtitle->config.external_filename);
            if (ret < 0)
            {
                hb_error( "Could not initialize subtitle avformat context." );
                goto error;
            }

            oc = track->oc;
            ret = avio_open2(&oc->pb, subtitle->config.external_filename,
                             AVIO_FLAG_WRITE, &track->oc->interrupt_callback,
                             NULL);
            if( ret < 0 )
            {
                hb_error( "subtitle avio_open2 failed, errno %d", ret);
                goto error;
            }
        }
        else
        {
            oc = m->oc;
        }

        track->type = MUX_TYPE_SUBTITLE;
        track->st = avformat_new_stream(oc, NULL);
        if (track->st == NULL)
        {
            hb_error("Could not initialize subtitle stream");
            goto error;
        }

        track->st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
        track->st->time_base = m->time_base;
        track->st->codecpar->width = subtitle->width;
        track->st->codecpar->height = subtitle->height;

        int need_extradata = 0;
        switch (subtitle->source)
        {
            case VOBSUB:
            {
                track->st->codecpar->codec_id = AV_CODEC_ID_DVD_SUBTITLE;
                need_extradata = 1;
            } break;

            case PGSSUB:
            {
                track->st->codecpar->codec_id = AV_CODEC_ID_HDMV_PGS_SUBTITLE;
            } break;

            case DVBSUB:
            {
                track->st->codecpar->codec_id = AV_CODEC_ID_DVB_SUBTITLE;
                need_extradata = 1;
            } break;

            case CC608SUB:
            case CC708SUB:
            case SSASUB:
            case IMPORTSSA:
            {
                if (job->mux == HB_MUX_AV_MP4 &&
                    subtitle->config.external_filename == NULL)
                {
                    track->st->codecpar->codec_id = AV_CODEC_ID_MOV_TEXT;
                }
                else
                {
                    track->st->codecpar->codec_id = AV_CODEC_ID_ASS;
                    need_fonts = 1;
                }
                need_extradata = 1;
            } break;

            case TX3GSUB:
            case UTF8SUB:
            case IMPORTSRT:
            {
                if (job->mux == HB_MUX_AV_MP4 &&
                    subtitle->config.external_filename == NULL)
                {
                    track->st->codecpar->codec_id = AV_CODEC_ID_MOV_TEXT;
                }
                else
                {
                    track->st->codecpar->codec_id = AV_CODEC_ID_SUBRIP;
                }
                need_extradata = 1;
            } break;

            default:
                continue;
        }

        if (need_extradata)
        {
            if (set_extradata(subtitle->extradata,
                              &track->st->codecpar->extradata,
                              &track->st->codecpar->extradata_size))
            {
                goto error;
            }
        }

        if (ii == subtitle_default)
        {
            track->st->disposition |= AV_DISPOSITION_DEFAULT;
        }
        if (subtitle->config.default_track)
        {
            track->st->disposition |= AV_DISPOSITION_FORCED;
        }

        lang = lookup_lang_code(job->mux, subtitle->iso639_2 );
        if (lang != NULL)
        {
            av_dict_set(&track->st->metadata, "language", lang, 0);
        }
        if (subtitle->config.name != NULL && subtitle->config.name[0] != 0)
        {
            // Set subtitle track title
            av_dict_set(&track->st->metadata, "title",
                        subtitle->config.name, 0);
            if (job->mux == HB_MUX_AV_MP4)
            {
                // Some software (MPC, mediainfo) use hdlr description
                // for track title
                av_dict_set(&track->st->metadata, "handler_name",
                            subtitle->config.name, 0);
            }
        }
    }

    if (need_fonts)
    {
        hb_list_t * list_attachment = job->list_attachment;
        int i;
        for ( i = 0; i < hb_list_count(list_attachment); i++ )
        {
            hb_attachment_t * attachment = hb_list_item( list_attachment, i );

            if ((attachment->type == FONT_TTF_ATTACH || attachment->type == FONT_OTF_ATTACH) &&
                attachment->size > 0)
            {
                AVStream *st = avformat_new_stream(m->oc, NULL);
                if (st == NULL)
                {
                    hb_error("Could not initialize attachment stream");
                    goto error;
                }

                st->codecpar->codec_type = AVMEDIA_TYPE_ATTACHMENT;
                if (attachment->type == FONT_TTF_ATTACH)
                {
                    st->codecpar->codec_id = AV_CODEC_ID_TTF;
                }
                else if (attachment->type == FONT_OTF_ATTACH)
                {
                    st->codecpar->codec_id = MKBETAG( 0 ,'O','T','F');
                    av_dict_set(&st->metadata, "mimetype", "application/vnd.ms-opentype", 0);
                }

                size_t   priv_size = attachment->size;
                uint8_t *priv_data = av_malloc(priv_size + AV_INPUT_BUFFER_PADDING_SIZE);
                if (priv_data == NULL)
                {
                    hb_error("Font extradata: malloc failure");
                    goto error;
                }
                memcpy(priv_data, attachment->data, priv_size);

                st->codecpar->extradata = priv_data;
                st->codecpar->extradata_size = priv_size;

                av_dict_set(&st->metadata, "filename", attachment->name, 0);
            }
        }
    }

    if (job->metadata)
    {
        hb_deep_log(2, "Writing Metadata to output file...");

        if (job->metadata->dict)
        {
            hb_dict_iter_t iter = hb_dict_iter_init(job->metadata->dict);

            while (iter != HB_DICT_ITER_DONE)
            {
                const char *key;
                hb_value_t *val;

                hb_dict_iter_next_ex(job->metadata->dict, &iter, &key, &val);
                if (key != NULL && val != NULL)
                {
                    const char *str = hb_value_get_string(val);

                    if (str != NULL)
                    {
                        const char *mux_key = lookup_meta_mux_key(meta_mux, key);

                        if (mux_key != NULL)
                        {
                            av_dict_set(&m->oc->metadata, mux_key, str, 0);
                        }
                    }
                }
            }

            if (job->mux == HB_MUX_AV_MP4)
            {
                // Set the location tag language to undefined,
                // Apple software seems to require it
                hb_value_t *location = hb_dict_get(job->metadata->dict, "Location");
                if (location)
                {
                    char *str = hb_value_get_string_xform(location);
                    av_dict_set(&m->oc->metadata, "location-und", str, 0);
                    free(str);
                }
            }
        }

        if (job->metadata->list_coverart)
        {
            hb_list_t *list_coverart = job->metadata->list_coverart;
            for (int ii = 0; ii < hb_list_count(list_coverart); ii++)
            {
                hb_coverart_t *art = hb_list_item(list_coverart, ii);

                enum AVCodecID codec_id = AV_CODEC_ID_NONE;
                const char *mimetype;
                const char *filename;

                switch (art->type)
                {
                    case HB_ART_PNG:
                        codec_id = AV_CODEC_ID_PNG;
                        mimetype = "image/png";
                        filename = art->name ? art->name : "cover.png";
                        break;
                    case HB_ART_JPEG:
                        codec_id = AV_CODEC_ID_MJPEG;
                        mimetype = "image/jpeg";
                        filename = art->name ? art->name : "cover.jpg";
                        break;
                    default:
                        break;
                }

                if (codec_id != AV_CODEC_ID_NONE)
                {
                    AVStream *st = avformat_new_stream(m->oc, NULL);
                    if (st == NULL)
                    {
                        hb_error("Could not initialize cover art stream");
                        goto error;
                    }

                    if (job->mux == HB_MUX_AV_MP4)
                    {
                        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                        st->codecpar->codec_id = codec_id;
                        st->codecpar->width  = 640;
                        st->codecpar->height = 360;
                        st->disposition = AV_DISPOSITION_ATTACHED_PIC;
                    }
                    else
                    {
                        st->codecpar->codec_type = AVMEDIA_TYPE_ATTACHMENT;
                        st->codecpar->codec_id = codec_id;

                        av_dict_set(&st->metadata, "mimetype", mimetype, 0);
                        av_dict_set(&st->metadata, "filename", filename, 0);

                        size_t   priv_size = art->size;
                        uint8_t *priv_data = av_malloc(priv_size + AV_INPUT_BUFFER_PADDING_SIZE);
                        if (priv_data == NULL)
                        {
                            hb_error("Cover art extradata: malloc failure");
                            goto error;
                        }
                        memcpy(priv_data, art->data, priv_size);

                        st->codecpar->extradata = priv_data;
                        st->codecpar->extradata_size = priv_size;
                    }
                }
            }
        }
    }

    if (av_dict_get(m->oc->metadata, "creation_time", NULL, 0) == NULL)
    {
        time_t now = time(NULL);
        struct tm *now_utc = gmtime(&now);
        char now_8601[24];
        strftime(now_8601, sizeof(now_8601), "%Y-%m-%dT%H:%M:%SZ", now_utc);
        av_dict_set(&m->oc->metadata, "creation_time", now_8601, 0);
    }

    char tool_string[80];
    snprintf(tool_string, sizeof(tool_string), "HandBrake %s %i",
             HB_PROJECT_VERSION, HB_PROJECT_BUILD);
    av_dict_set(&m->oc->metadata, "encoding_tool", tool_string, 0);

    ret = avformat_write_header(m->oc, &av_opts);
    if( ret < 0 )
    {
        hb_error( "muxavformat: avformat_write_header failed!");
        goto error;
    }

    AVDictionaryEntry *t = NULL;
    while( ( t = av_dict_get( av_opts, "", t, AV_DICT_IGNORE_SUFFIX ) ) )
    {
        hb_log( "muxavformat: Unknown option %s", t->key );
    }
    av_dict_free( &av_opts );

    for (ii = 0; ii < m->ntracks; ii++)
    {
        if (m->tracks[ii]->oc != NULL)
        {
            ret = avformat_write_header(m->tracks[ii]->oc, NULL);
            if( ret < 0 )
            {
                hb_error( "muxavformat: avformat_write_header external track failed!");
                goto error;
            }
        }
    }

    return 0;

error:
    for (ii = 0; ii < m->ntracks; ii++)
    {
        if (m->tracks[ii]->oc != NULL)
        {
            avformat_free_context(m->tracks[ii]->oc);
        }
    }
    av_dict_free(&av_opts);
    free(job->mux_data);
    job->mux_data = NULL;
    avformat_free_context(m->oc);
    *job->done_error = HB_ERROR_INIT;
    *job->die = 1;
    return -1;
}

static int add_chapter(hb_mux_object_t *m, int64_t start, int64_t end, char * title)
{
    AVChapter *chap;
    AVChapter **chapters;
    int nchap = m->oc->nb_chapters;

    nchap++;
    chapters = av_realloc(m->oc->chapters, nchap * sizeof(AVChapter*));
    if (chapters == NULL)
    {
        hb_error("chapter array: malloc failure");
        return -1;
    }

    chap = av_mallocz(sizeof(AVChapter));
    if (chap == NULL)
    {
        hb_error("chapter: malloc failure");
        return -1;
    }

    m->oc->chapters = chapters;
    m->oc->chapters[nchap-1] = chap;
    m->oc->nb_chapters = nchap;

    chap->id = nchap;
    chap->time_base = m->tracks[0]->st->time_base;
    // libav does not currently have a good way to deal with chapters and
    // delayed stream timestamps.  It makes no corrections to the chapter
    // track.  A patch to libav would touch a lot of things, so for now,
    // work around the issue here.
    chap->start = start;
    chap->end = end;
    av_dict_set(&chap->metadata, "title", title, 0);

    return 0;
}

static int avformatMux(hb_mux_object_t *m, hb_mux_data_t *track, hb_buffer_t *buf)
{
    int64_t           dts, pts, duration = AV_NOPTS_VALUE;
    hb_job_t        * job     = m->job;
    uint8_t         * sub_out = NULL;
    AVFormatContext * oc;

    oc = track->oc != NULL ? track->oc : m->oc;
    if (track->type == MUX_TYPE_VIDEO && (job->mux & HB_MUX_MASK_MP4))
    {
        // compute dts duration for MP4 files
        hb_buffer_t * tmp;

        // delay by one frame so that we can compute duration properly.
        tmp = track->delay_buf;
        track->delay_buf = buf;
        buf = tmp;
    }
    if (buf == NULL)
    {
        if (job->mux == HB_MUX_AV_MP4 &&
            track->oc == NULL && track->type == MUX_TYPE_SUBTITLE)
        {
            // Write a final "empty" subtitle to terminate the last
            // subtitle that was written
            if (track->duration > 0)
            {
                uint8_t empty[2] = {0,0};

                m->empty_pkt->data = empty;
                m->empty_pkt->size = 2;
                m->empty_pkt->dts = track->duration;
                m->empty_pkt->pts = track->duration;
                m->empty_pkt->duration = 90;
                m->empty_pkt->stream_index = track->st->index;
                av_interleaved_write_frame(m->oc, m->empty_pkt);
                av_packet_unref(m->empty_pkt);
            }
        }
        return 0;
    }

    if (track->type == MUX_TYPE_VIDEO &&
        (job->mux & (HB_MUX_MASK_MKV | HB_MUX_MASK_WEBM)) &&
        buf->s.renderOffset < 0)
    {
        // libav matroska muxer doesn't write dts to the output, but
        // if it sees a negative dts, it applies an offset to both pts
        // and dts to make it positive.  This offset breaks chapter
        // start times and A/V sync.  libav also requires that dts is
        // "monotonically increasing", which means it last_dts <= next_dts.
        // It also uses dts to determine track interleaving, so we need
        // to provide some reasonable dts value.
        // So when renderOffset < 0, set to 0 for mkv.
        buf->s.renderOffset = 0;
        // Note: for MP4, libav allows negative dts and creates an edts
        // (edit list) entry in this case.
    }
    if (buf->s.renderOffset == AV_NOPTS_VALUE)
    {
        dts = av_rescale_q(buf->s.start, (AVRational){1,90000},
                           track->st->time_base);
    }
    else
    {
        dts = av_rescale_q(buf->s.renderOffset, (AVRational){1,90000},
                           track->st->time_base);
    }

    pts = av_rescale_q(buf->s.start, (AVRational){1,90000},
                       track->st->time_base);

    if (track->type == MUX_TYPE_VIDEO && track->delay_buf != NULL)
    {
        int64_t delayed_dts;
        delayed_dts = av_rescale_q(track->delay_buf->s.renderOffset,
                                   (AVRational){1,90000},
                                   track->st->time_base);
        duration = delayed_dts - dts;
    }
    if (duration < 0 && buf->s.duration > 0)
    {
        duration = av_rescale_q(buf->s.duration, (AVRational){1,90000},
                                track->st->time_base);
    }
    if (duration < 0)
    {
        // There is a possibility that some subtitles get through the pipeline
        // without ever discovering their true duration.  Make the duration
        // 10 seconds in this case. Unless they are PGS subs which should
        // have zero duration.
        if (track->type == MUX_TYPE_SUBTITLE &&
            track->st->codecpar->codec_id != AV_CODEC_ID_HDMV_PGS_SUBTITLE)
        {
            duration = av_rescale_q(10, (AVRational){1,1},
                                    track->st->time_base);
        }
        else if (track->type == MUX_TYPE_VIDEO)
        {
            duration = av_rescale_q(
                            (int64_t)job->vrate.den * 90000 / job->vrate.num,
                            (AVRational){1,90000}, track->st->time_base);
        }
        else
        {
            duration = 0;
        }
    }

    m->pkt->data = buf->data;
    m->pkt->size = buf->size;
    m->pkt->dts = dts;
    m->pkt->pts = pts;
    m->pkt->duration = duration;

    if (track->type == MUX_TYPE_VIDEO)
    {
        if ((buf->s.frametype == HB_FRAME_IDR) ||
            (buf->s.flags & HB_FLAG_FRAMETYPE_KEY))
        {
            m->pkt->flags |= AV_PKT_FLAG_KEY;
        }
        if (!(buf->s.flags & HB_FLAG_FRAMETYPE_REF))
        {
            m->pkt->flags |= AV_PKT_FLAG_DISPOSABLE;
        }
    }
    else if (buf->s.frametype & HB_FRAME_MASK_KEY)
    {
        m->pkt->flags |= AV_PKT_FLAG_KEY;
    }

    switch (track->type)
    {
        case MUX_TYPE_VIDEO:
        {
            if (job->chapter_markers && buf->s.new_chap)
            {
                if (track->current_chapter > 0)
                {
                    hb_chapter_t *chapter;

                    // reached chapter N, write marker for chapter N-1
                    // we don't know the end time of chapter N-1 till we receive
                    // chapter N.  So we are always writing the previous chapter
                    // mark.
                    // chapter numbers start at 1, but the list starts at 0
                    chapter = hb_list_item(job->list_chapter,
                                           track->current_chapter - 1);

                    // make sure we're not writing a chapter that has 0 length
                    if (chapter != NULL &&
                        track->prev_chapter_tc != AV_NOPTS_VALUE &&
                        track->prev_chapter_tc < m->pkt->pts)
                    {
                        char title[1024];
                        if (chapter->title != NULL)
                        {
                            snprintf(title, 1023, "%s", chapter->title);
                        }
                        else
                        {
                            snprintf(title, 1023, "Chapter %d",
                                     track->current_chapter);
                        }
                        add_chapter(m, track->prev_chapter_tc, m->pkt->pts, title);
                    }
                }
                track->current_chapter = buf->s.new_chap;
                track->prev_chapter_tc = m->pkt->pts;
            }
        } break;

        case MUX_TYPE_SUBTITLE:
        {
            if (job->mux == HB_MUX_AV_MP4 && track->oc == NULL)
            {
                /* Write an empty sample */
                if ( track->duration < pts )
                {
                    uint8_t empty[2] = {0,0};

                    m->empty_pkt->data = empty;
                    m->empty_pkt->size = 2;
                    m->empty_pkt->dts = track->duration;
                    m->empty_pkt->pts = track->duration;
                    m->empty_pkt->duration = pts - track->duration;
                    m->empty_pkt->stream_index = track->st->index;
                    int ret = av_interleaved_write_frame(m->oc, m->empty_pkt);
                    av_packet_unref(m->empty_pkt);
                    if (ret < 0)
                    {
                        char errstr[64];
                        av_strerror(ret, errstr, sizeof(errstr));
                        hb_error("avformatMux: track %d, av_interleaved_write_frame failed with error '%s' (empty_pkt)",
                                 track->st->index, errstr);
                        *job->done_error = HB_ERROR_UNKNOWN;
                        *job->die = 1;
                        return -1;
                    }
                }
            }
            if (m->pkt->data == NULL)
            {
                // Memory allocation failure!
                hb_error("avformatMux: subtitle memory allocation failure");
                *job->done_error = HB_ERROR_UNKNOWN;
                *job->die = 1;
                return -1;
            }
        } break;
        case MUX_TYPE_AUDIO:
        default:
            break;
    }
    track->duration = pts + m->pkt->duration;

    if (track->bitstream_context)
    {
        int ret;
        ret = av_bsf_send_packet(track->bitstream_context, m->pkt);
        if (ret < 0)
        {
            hb_error("avformatMux: track %d av_bsf_send_packet failed",
                     track->st->index);
            return ret;
        }
        ret = av_bsf_receive_packet(track->bitstream_context, m->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return 0;
        }
        else if (ret < 0)
        {
            hb_error("avformatMux: track %d av_bsf_receive_packet failed",
                     track->st->index);
            return ret;
        }
    }

    m->pkt->stream_index = track->st->index;
    int ret = av_interleaved_write_frame(oc, m->pkt);
    av_packet_unref(m->pkt);
    if (sub_out != NULL)
    {
        free(sub_out);
    }
    // Many avformat muxer functions do not check the error status
    // of the AVIOContext.  So we need to check it ourselves to detect
    // write errors (like disk full condition).
    if (ret < 0 || oc->pb->error != 0)
    {
        char errstr[64];
        av_strerror(ret < 0 ? ret : oc->pb->error, errstr, sizeof(errstr));
        hb_error("avformatMux: track %d, av_interleaved_write_frame failed with error '%s'",
                 track->st->index, errstr);
        *job->done_error = HB_ERROR_UNKNOWN;
        *job->die = 1;
        return -1;
    }

    hb_buffer_close( &buf );
    return 0;
}

static int avformatEnd(hb_mux_object_t *m)
{
    hb_job_t *job           = m->job;
    hb_mux_data_t *track = job->mux_data;

    if( !job->mux_data )
    {
        /*
         * We must have failed to create the file in the first place.
         */
        return 0;
    }

    // Flush any delayed frames
    int ii;
    for (ii = 0; ii < m->ntracks; ii++)
    {
        avformatMux(m, m->tracks[ii], NULL);

        if (m->tracks[ii]->bitstream_context)
        {
            av_bsf_free(&m->tracks[ii]->bitstream_context);
        }
    }

    if (job->chapter_markers)
    {
        hb_chapter_t *chapter;

        // get the last chapter
        chapter = hb_list_item(job->list_chapter, track->current_chapter - 1);

        // only write the last chapter marker if it lasts at least 1.5 second
        if (chapter != NULL && chapter->duration > 135000LL)
        {
            char title[1024];
            if (chapter->title != NULL)
            {
                snprintf(title, 1023, "%s", chapter->title);
            }
            else
            {
                snprintf(title, 1023, "Chapter %d", track->current_chapter);
            }
            add_chapter(m, track->prev_chapter_tc, track->duration, title);
        }
    }

    // Update and track private data that can change during
    // encode.
    for(ii = 0; ii < hb_list_count( job->list_audio ); ii++)
    {
        AVStream *st;
        hb_audio_t    * audio;

        audio = hb_list_item(job->list_audio, ii);
        st = audio->priv.mux_data->st;

        switch (audio->config.out.codec & HB_ACODEC_MASK)
        {
            case HB_ACODEC_FFFLAC:
            case HB_ACODEC_FFFLAC24:
                set_extradata(audio->priv.extradata, &st->codecpar->extradata, &st->codecpar->extradata_size);
                break;
            default:
                break;
        }
    }

    // Write MP4 cover art
    if (job->mux == HB_MUX_AV_MP4 && job->metadata)
    {
        hb_list_t *list_coverart = job->metadata->list_coverart;
        for (int ii = 0; ii < hb_list_count(list_coverart); ii++)
        {
            hb_coverart_t *art = hb_list_item(list_coverart, ii);
            m->pkt->data = art->data;
            m->pkt->size = art->size;
            m->pkt->stream_index = m->ntracks + ii;
            av_interleaved_write_frame(m->oc, m->pkt);
            av_packet_unref(m->pkt);
        }
    }

    av_write_trailer(m->oc);
    avio_close(m->oc->pb);
    avformat_free_context(m->oc);
    av_packet_free(&m->pkt);
    av_packet_free(&m->empty_pkt);
    m->oc = NULL;

    for (ii = 0; ii < m->ntracks; ii++)
    {
        if (m->tracks[ii]->oc != NULL)
        {
            av_write_trailer(m->tracks[ii]->oc);
            avio_close(m->tracks[ii]->oc->pb);
            avformat_free_context(m->tracks[ii]->oc);
            m->tracks[ii]->oc = NULL;
        }
    }

    for (int i = 0; i < m->ntracks; i++)
    {
        hb_mux_data_t *track = m->tracks[i];
        m->tracks[i] = NULL;
        free(track);
    }

    free(m->tracks);

    return 0;
}

hb_mux_object_t * hb_mux_avformat_init( hb_job_t * job )
{
    hb_mux_object_t * m = calloc( sizeof( hb_mux_object_t ), 1 );
    m->init      = avformatInit;
    m->mux       = avformatMux;
    m->end       = avformatEnd;
    m->job       = job;
    return m;
}
