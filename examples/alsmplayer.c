/**
 * OpenAL streamer using mplayer
 * Copyright (C) 2007 by author.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include <AL/al.h>
#include <AL/alc.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

static ALenum checkALErrors(int linenum)
{
    ALenum err = alGetError();
    if(err != AL_NO_ERROR)
        printf("OpenAL Error: %s (0x%x), @ %d\n", alGetString(err), err, linenum);
    return err;
}
#define checkALErrors() checkALErrors(__LINE__)

int main(int argc, char **argv)
{
    ALCdevice *dev;
    ALCcontext *ctx;
    struct stat statbuf;

    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s <audiofile>\n", argv[0]);
        return 0;
    }

    /* First the standard open-device, create-context, set-context.. */
    dev = alcOpenDevice(NULL);
    if(!dev)
    {
        fprintf(stderr, "Oops\n");
        return 1;
    }
    ctx = alcCreateContext(dev, NULL);
    alcMakeContextCurrent(ctx);
    if(!ctx)
    {
        fprintf(stderr, "Oops2\n");
        return 1;
    }

    {
        /* The number of buffers and bytes-per-buffer for our stream are set
         * here. The number of buffers should be two or more, and the buffer
         * size should be a multiple of the frame size (by default, OpenAL's
         * largest frame size is 4, however extensions that can add more formats
         * may be larger). Slower systems may need more buffers/larger buffer
         * sizes. */
#define NUM_BUFFERS 3
#define BUFFER_SIZE 8192
        /* These are what we'll use for OpenAL playback */
        ALuint source, buffers[NUM_BUFFERS];
        ALuint frequency;
        ALenum format;
        unsigned char *buf;
        /* These are used for interacting with mplayer */
//        int pid, files[2];
        FILE *f;

        /* Generate the buffers and sources */
        alGenBuffers(NUM_BUFFERS, buffers);
        alGenSources(1, &source);
        if(checkALErrors() != AL_NO_ERROR)
        {
            fprintf(stderr, "Error generating :(\n");
            return 1;
        }

        /* Here's where our magic begins. First, we want to call stat on the
         * filename since mplayer will just silently exit if it tries to play a
         * non-existant file **/
        if(stat(argv[1], &statbuf) != 0 || !S_ISREG(statbuf.st_mode))
        {
            fprintf(stderr, "%s doesn't seem to be a regular file :(\n", argv[1]);
            return 1;
        }

        /* fdopen simply creates a FILE* from the given file descriptor. This is
         * generally easier to work with, but there's no reason you couldn't use
         * the lower-level io routines on the descriptor if you wanted */
        f = fopen(argv[1], "rb");

        /* Allocate the buffer, and read the RIFF-WAVE header. We don't actually
         * need to read it, so just ignore what it writes to the buffer. Because
         * this is a file pipe, it is unseekable, so we have to read bytes we
         * want to skip. Also note that because mplayer is writing out the file
         * in real-time, the chunk size information may not be filled out. */
        buf = malloc(BUFFER_SIZE);
        fread(buf, 1, 12, f);

        /* This is the first .wav file chunk. Check the chunk header to make
         * sure it is the format information. The first four bytes is the
         * indentifier (which we check), and the last four is the chunk size
         * (which we ignore) */
        fread(buf, 1, 8, f);
        if(buf[0] != 'f' || buf[1] != 'm' || buf[2] != 't' || buf[3] != ' ')
        {
            /* If this isn't the format info, it probably means it was an
             * unsupported audio format for mplayer, or the file didn't contain
             * an audio track. */
            fprintf(stderr, "Not 'fmt ' :(\n");
            /* Note that closing the file will leave mplayer's write file
             * descriptor without a read counterpart. This will cause mplayer to
             * receive a SIGPIPE signal, which will cause it to abort and exit
             * automatically for us. Alternatively, you can use the pid returned
             * from fork() to send it a signal explicitly. */
            fclose(f);
            return 1;
        }

        int channels, bits;

        /* Read the wave format type, as a 16-bit little-endian integer.
         * There's no reason this shouldn't be 1. */
        fread(buf, 1, 2, f);
        if(buf[1] != 0 || buf[0] != 1)
        {
            fprintf(stderr, "Not PCM :(\n");
            fclose(f);
            return 1;
        }

        /* Get the channel count (16-bit little-endian) */
        fread(buf, 1, 2, f);
        channels  = buf[1]<<8;
        channels |= buf[0];

        /* Get the sample frequency (32-bit little-endian) */
        fread(buf, 1, 4, f);
        frequency  = buf[3]<<24;
        frequency |= buf[2]<<16;
        frequency |= buf[1]<<8;
        frequency |= buf[0];

        /* The next 6 bytes hold the block size and bytes-per-second. We
         * don't need that info, so just read and ignore it. */
        fread(buf, 1, 6, f);

        /* Get the bit depth (16-bit little-endian) */
        fread(buf, 1, 2, f);
        bits  = buf[1]<<8;
        bits |= buf[0];

        /* Now convert the given channel count and bit depth into an OpenAL
         * format. We could use extensions to support more formats (eg.
         * surround sound, floating-point samples), but that is beyond the
         * scope of this tutorial */
        format = 0;
        if(bits == 8)
        {
            if(channels == 1)
                format = AL_FORMAT_MONO8;
            else if(channels == 2)
                format = AL_FORMAT_STEREO8;
        }
        else if(bits == 16)
        {
            if(channels == 1)
                format = AL_FORMAT_MONO16;
            else if(channels == 2)
                format = AL_FORMAT_STEREO16;
        }
        if(!format)
        {
            fprintf(stderr, "Incompatible format (%d, %d) :(\n", channels, bits);
            fclose(f);
            return 1;
        }

        /* Next up is the data chunk, which will hold the decoded sample data */
        fread(buf, 1, 8, f);
        if(buf[0] != 'd' || buf[1] != 'a' || buf[2] != 't' || buf[3] != 'a')
        {
            fclose(f);
            fprintf(stderr, "Not 'data' :(\n");
            return 1;
        }

        /* Now we have everything we need. To read the decoded data, all we have
         * to do is read from the file handle! Note that the .wav format spec
         * has multibyte sample foramts stored as little-endian. If you were on
         * a big-endian machine, you'd have to iterate over the returned data
         * and flip the bytes for those formats before giving it to OpenAL. Also
         * be aware that there is no seeking on the file handle. A slightly more
         * complex setup could be made to send commands back to mplayer to seek
         * on the stream, however that is beyond the scope of this tutorial. */
        {
            int ret;

            /* Fill the data buffer with the amount of bytes-per-buffer, and
             * buffer it into OpenAL. This may read (and return) less than the
             * requested amount when it hits the end of the "stream" */
            ret = fread(buf, 1, BUFFER_SIZE, f);
            alBufferData(buffers[0], format, buf, ret, frequency);
            printf("read0 %d bytes from file...\n", ret);
            /* Once the data's buffered into OpenAL, we're free to modify our
             * data buffer, so reuse it to fill the remaining OpenAL buffers. */
            ret = fread(buf, 1, BUFFER_SIZE, f);
            alBufferData(buffers[1], format, buf, ret, frequency);
            printf("read0 %d bytes from file...\n", ret);

            ret = fread(buf, 1, BUFFER_SIZE, f);
            alBufferData(buffers[2], format, buf, ret, frequency);
            printf("read0 %d bytes from file...\n", ret);

            if(checkALErrors() != AL_NO_ERROR)
            {
                fprintf(stderr, "Error loading :(\n");
                return 1;
            }

            /* Queue the buffers onto the source, and start playback! */
            alSourceQueueBuffers(source, NUM_BUFFERS, buffers);
            alSourcePlay(source);
            if(checkALErrors() != AL_NO_ERROR)
            {
                fprintf(stderr, "Error starting :(\n");
                return 1;
            }

            /* While not at the end of the stream... */
            while(!feof(f))
            {
                ALuint buffer;
                ALint val;

                /* Check if OpenAL is done with any of the queued buffers */
                alGetSourcei(source, AL_BUFFERS_PROCESSED, &val);
                if(val <= 0)
                    continue;

                /* For each processed buffer... */
                while(val--)
                {
                    /* Read the next chunk of decoded data from the stream */
                    memset(buf, 0, BUFFER_SIZE);
                    ret = fread(buf, 1, BUFFER_SIZE, f);
                    printf("read1 %d bytes from file...\n", ret);

                    /* Pop the oldest queued buffer from the source, fill it
                     * with the new data, then requeue it */
                    alSourceUnqueueBuffers(source, 1, &buffer);
                    checkALErrors();

                    alBufferData(buffer, format, buf, ret, frequency);
//                    checkALErrors();

                    alSourceQueueBuffers(source, 1, &buffer);
                    if(checkALErrors() != AL_NO_ERROR)
                    {
                        fprintf(stderr, "Error buffering :(\n");
                        return 1;
                    }
                    printf("-------------%d----------------\n", val);
                }
                /* Make sure the source is still playing, and restart it if
                 * needed. */
                alGetSourcei(source, AL_SOURCE_STATE, &val);
                if(val != AL_PLAYING)
                    alSourcePlay(source);
            }
        }

        /* File's done decoding. We can close the pipe and free the data buffer
         * now. */
        fclose(f);
        free(buf);
        {
            ALint val;
            /* Although mplayer is done giving us data, OpenAL may still be
             * playing the remaining buffers. Wait until it stops. */
            do {
                alGetSourcei(source, AL_SOURCE_STATE, &val);
            } while(val == AL_PLAYING);
        }

        /* Done playing. Delete the source and buffers */
        alDeleteSources(1, &source);
        alDeleteBuffers(NUM_BUFFERS, buffers);
    }

    /* All done. Close OpenAL and exit. */
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(dev);

    return 0;
}
