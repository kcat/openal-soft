
#include "config.h"

#include "ambdec.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <vector>

#include "compat.h"


static char *lstrip(char *line)
{
    while(isspace(line[0]))
        line++;
    return line;
}

static char *rstrip(char *line)
{
    size_t len = strlen(line);
    while(len > 0 && isspace(line[len-1]))
        len--;
    line[len] = 0;
    return line;
}

static int readline(FILE *f, std::vector<char> &output)
{
    int c;
    while((c=fgetc(f)) != EOF && (c == '\r' || c == '\n')) {
    }
    if(c == EOF)
        return 0;

    output.clear();
    do {
        output.emplace_back(c);
    } while((c=fgetc(f)) != EOF && c != '\r' && c != '\n');
    output.emplace_back((c=0));

    return 1;
}


/* Custom strtok_r, since we can't rely on it existing. */
static char *my_strtok_r(char *str, const char *delim, char **saveptr)
{
    /* Sanity check and update internal pointer. */
    if(!saveptr || !delim) return nullptr;
    if(str) *saveptr = str;
    str = *saveptr;

    /* Nothing more to do with this string. */
    if(!str) return nullptr;

    /* Find the first non-delimiter character. */
    while(*str != '\0' && strchr(delim, *str) != nullptr)
        str++;
    if(*str == '\0')
    {
        /* End of string. */
        *saveptr = nullptr;
        return nullptr;
    }

    /* Find the next delimiter character. */
    *saveptr = strpbrk(str, delim);
    if(*saveptr) *((*saveptr)++) = '\0';

    return str;
}

static char *read_int(ALint *num, const char *line, int base)
{
    char *end;
    *num = strtol(line, &end, base);
    if(end && *end != '\0')
        end = lstrip(end);
    return end;
}

static char *read_uint(ALuint *num, const char *line, int base)
{
    char *end;
    *num = strtoul(line, &end, base);
    if(end && *end != '\0')
        end = lstrip(end);
    return end;
}

static char *read_float(ALfloat *num, const char *line)
{
    char *end;
#ifdef HAVE_STRTOF
    *num = strtof(line, &end);
#else
    *num = (ALfloat)strtod(line, &end);
#endif
    if(end && *end != '\0')
        end = lstrip(end);
    return end;
}


char *read_clipped_line(FILE *f, std::vector<char> &buffer)
{
    while(readline(f, buffer))
    {
        char *line, *comment;

        line = lstrip(buffer.data());
        comment = strchr(line, '#');
        if(comment) *(comment++) = 0;

        line = rstrip(line);
        if(line[0]) return line;
    }
    return nullptr;
}

static int load_ambdec_speakers(AmbDecConf *conf, FILE *f, std::vector<char> &buffer, char **saveptr)
{
    ALsizei cur = 0;
    while(cur < conf->NumSpeakers)
    {
        const char *cmd = my_strtok_r(nullptr, " \t", saveptr);
        if(!cmd)
        {
            char *line = read_clipped_line(f, buffer);
            if(!line)
            {
                ERR("Unexpected end of file\n");
                return 0;
            }
            cmd = my_strtok_r(line, " \t", saveptr);
        }

        if(strcmp(cmd, "add_spkr") == 0)
        {
            const char *name = my_strtok_r(nullptr, " \t", saveptr);
            const char *dist = my_strtok_r(nullptr, " \t", saveptr);
            const char *az = my_strtok_r(nullptr, " \t", saveptr);
            const char *elev = my_strtok_r(nullptr, " \t", saveptr);
            const char *conn = my_strtok_r(nullptr, " \t", saveptr);

            if(!name) WARN("Name not specified for speaker %u\n", cur+1);
            else conf->Speakers[cur].Name = name;
            if(!dist) WARN("Distance not specified for speaker %u\n", cur+1);
            else read_float(&conf->Speakers[cur].Distance, dist);
            if(!az) WARN("Azimuth not specified for speaker %u\n", cur+1);
            else read_float(&conf->Speakers[cur].Azimuth, az);
            if(!elev) WARN("Elevation not specified for speaker %u\n", cur+1);
            else read_float(&conf->Speakers[cur].Elevation, elev);
            if(!conn) TRACE("Connection not specified for speaker %u\n", cur+1);
            else conf->Speakers[cur].Connection = conn;

            cur++;
        }
        else
        {
            ERR("Unexpected speakers command: %s\n", cmd);
            return 0;
        }

        cmd = my_strtok_r(nullptr, " \t", saveptr);
        if(cmd)
        {
            ERR("Unexpected junk on line: %s\n", cmd);
            return 0;
        }
    }

    return 1;
}

static int load_ambdec_matrix(ALfloat *gains, ALfloat (*matrix)[MAX_AMBI_COEFFS], ALsizei maxrow, FILE *f, std::vector<char> &buffer, char **saveptr)
{
    int gotgains = 0;
    ALsizei cur = 0;
    while(cur < maxrow)
    {
        const char *cmd = my_strtok_r(nullptr, " \t", saveptr);
        if(!cmd)
        {
            char *line = read_clipped_line(f, buffer);
            if(!line)
            {
                ERR("Unexpected end of file\n");
                return 0;
            }
            cmd = my_strtok_r(line, " \t", saveptr);
        }

        if(strcmp(cmd, "order_gain") == 0)
        {
            ALuint curgain = 0;
            char *line;
            while((line=my_strtok_r(nullptr, " \t", saveptr)) != nullptr)
            {
                ALfloat value;
                line = read_float(&value, line);
                if(line && *line != '\0')
                {
                    ERR("Extra junk on gain %u: %s\n", curgain+1, line);
                    return 0;
                }
                if(curgain < MAX_AMBI_ORDER+1)
                    gains[curgain] = value;
                curgain++;
            }
            while(curgain < MAX_AMBI_ORDER+1)
                gains[curgain++] = 0.0f;
            gotgains = 1;
        }
        else if(strcmp(cmd, "add_row") == 0)
        {
            ALuint curidx = 0;
            char *line;
            while((line=my_strtok_r(nullptr, " \t", saveptr)) != nullptr)
            {
                ALfloat value;
                line = read_float(&value, line);
                if(line && *line != '\0')
                {
                    ERR("Extra junk on matrix element %ux%u: %s\n", cur, curidx, line);
                    return 0;
                }
                if(curidx < MAX_AMBI_COEFFS)
                    matrix[cur][curidx] = value;
                curidx++;
            }
            while(curidx < MAX_AMBI_COEFFS)
                matrix[cur][curidx++] = 0.0f;
            cur++;
        }
        else
        {
            ERR("Unexpected speakers command: %s\n", cmd);
            return 0;
        }

        cmd = my_strtok_r(nullptr, " \t", saveptr);
        if(cmd)
        {
            ERR("Unexpected junk on line: %s\n", cmd);
            return 0;
        }
    }

    if(!gotgains)
    {
        ERR("Matrix order_gain not specified\n");
        return 0;
    }

    return 1;
}

int AmbDecConf::load(const char *fname)
{
    std::vector<char> buffer;
    char *line;
    FILE *f;

    f = al_fopen(fname, "r");
    if(!f)
    {
        ERR("Failed to open: %s\n", fname);
        return 0;
    }

    while((line=read_clipped_line(f, buffer)) != nullptr)
    {
        char *saveptr;
        char *command;

        command = my_strtok_r(line, "/ \t", &saveptr);
        if(!command)
        {
            ERR("Malformed line: %s\n", line);
            goto fail;
        }

        if(strcmp(command, "description") == 0)
        {
            char *value = my_strtok_r(nullptr, "", &saveptr);
            Description = lstrip(value);
        }
        else if(strcmp(command, "version") == 0)
        {
            line = my_strtok_r(nullptr, "", &saveptr);
            line = read_uint(&Version, line, 10);
            if(line && *line != '\0')
            {
                ERR("Extra junk after version: %s\n", line);
                goto fail;
            }
            if(Version != 3)
            {
                ERR("Unsupported version: %u\n", Version);
                goto fail;
            }
        }
        else if(strcmp(command, "dec") == 0)
        {
            const char *dec = my_strtok_r(nullptr, "/ \t", &saveptr);
            if(strcmp(dec, "chan_mask") == 0)
            {
                line = my_strtok_r(nullptr, "", &saveptr);
                line = read_uint(&ChanMask, line, 16);
                if(line && *line != '\0')
                {
                    ERR("Extra junk after mask: %s\n", line);
                    goto fail;
                }
            }
            else if(strcmp(dec, "freq_bands") == 0)
            {
                line = my_strtok_r(nullptr, "", &saveptr);
                line = read_uint(&FreqBands, line, 10);
                if(line && *line != '\0')
                {
                    ERR("Extra junk after freq_bands: %s\n", line);
                    goto fail;
                }
                if(FreqBands != 1 && FreqBands != 2)
                {
                    ERR("Invalid freq_bands value: %u\n", FreqBands);
                    goto fail;
                }
            }
            else if(strcmp(dec, "speakers") == 0)
            {
                line = my_strtok_r(nullptr, "", &saveptr);
                line = read_int(&NumSpeakers, line, 10);
                if(line && *line != '\0')
                {
                    ERR("Extra junk after speakers: %s\n", line);
                    goto fail;
                }
                if(NumSpeakers > MAX_OUTPUT_CHANNELS)
                {
                    ERR("Unsupported speaker count: %u\n", NumSpeakers);
                    goto fail;
                }
            }
            else if(strcmp(dec, "coeff_scale") == 0)
            {
                line = my_strtok_r(nullptr, " \t", &saveptr);
                if(strcmp(line, "n3d") == 0)
                    CoeffScale = AmbDecScale::N3D;
                else if(strcmp(line, "sn3d") == 0)
                    CoeffScale = AmbDecScale::SN3D;
                else if(strcmp(line, "fuma") == 0)
                    CoeffScale = AmbDecScale::FuMa;
                else
                {
                    ERR("Unsupported coeff scale: %s\n", line);
                    goto fail;
                }
            }
            else
            {
                ERR("Unexpected /dec option: %s\n", dec);
                goto fail;
            }
        }
        else if(strcmp(command, "opt") == 0)
        {
            const char *opt = my_strtok_r(nullptr, "/ \t", &saveptr);
            if(strcmp(opt, "xover_freq") == 0)
            {
                line = my_strtok_r(nullptr, "", &saveptr);
                line = read_float(&XOverFreq, line);
                if(line && *line != '\0')
                {
                    ERR("Extra junk after xover_freq: %s\n", line);
                    goto fail;
                }
            }
            else if(strcmp(opt, "xover_ratio") == 0)
            {
                line = my_strtok_r(nullptr, "", &saveptr);
                line = read_float(&XOverRatio, line);
                if(line && *line != '\0')
                {
                    ERR("Extra junk after xover_ratio: %s\n", line);
                    goto fail;
                }
            }
            else if(strcmp(opt, "input_scale") == 0 || strcmp(opt, "nfeff_comp") == 0 ||
                    strcmp(opt, "delay_comp") == 0 || strcmp(opt, "level_comp") == 0)
            {
                /* Unused */
                my_strtok_r(nullptr, " \t", &saveptr);
            }
            else
            {
                ERR("Unexpected /opt option: %s\n", opt);
                goto fail;
            }
        }
        else if(strcmp(command, "speakers") == 0)
        {
            const char *value = my_strtok_r(nullptr, "/ \t", &saveptr);
            if(strcmp(value, "{") != 0)
            {
                ERR("Expected { after %s command, got %s\n", command, value);
                goto fail;
            }
            if(!load_ambdec_speakers(this, f, buffer, &saveptr))
                goto fail;
            value = my_strtok_r(nullptr, "/ \t", &saveptr);
            if(!value)
            {
                line = read_clipped_line(f, buffer);
                if(!line)
                {
                    ERR("Unexpected end of file\n");
                    goto fail;
                }
                value = my_strtok_r(line, "/ \t", &saveptr);
            }
            if(strcmp(value, "}") != 0)
            {
                ERR("Expected } after speaker definitions, got %s\n", value);
                goto fail;
            }
        }
        else if(strcmp(command, "lfmatrix") == 0 || strcmp(command, "hfmatrix") == 0 ||
                strcmp(command, "matrix") == 0)
        {
            const char *value = my_strtok_r(nullptr, "/ \t", &saveptr);
            if(strcmp(value, "{") != 0)
            {
                ERR("Expected { after %s command, got %s\n", command, value);
                goto fail;
            }
            if(FreqBands == 1)
            {
                if(strcmp(command, "matrix") != 0)
                {
                    ERR("Unexpected \"%s\" type for a single-band decoder\n", command);
                    goto fail;
                }
                if(!load_ambdec_matrix(HFOrderGain, HFMatrix, NumSpeakers, f, buffer, &saveptr))
                    goto fail;
            }
            else
            {
                if(strcmp(command, "lfmatrix") == 0)
                {
                    if(!load_ambdec_matrix(LFOrderGain, LFMatrix, NumSpeakers, f, buffer,
                                           &saveptr))
                        goto fail;
                }
                else if(strcmp(command, "hfmatrix") == 0)
                {
                    if(!load_ambdec_matrix(HFOrderGain, HFMatrix, NumSpeakers, f, buffer,
                                           &saveptr))
                        goto fail;
                }
                else
                {
                    ERR("Unexpected \"%s\" type for a dual-band decoder\n", command);
                    goto fail;
                }
            }
            value = my_strtok_r(nullptr, "/ \t", &saveptr);
            if(!value)
            {
                line = read_clipped_line(f, buffer);
                if(!line)
                {
                    ERR("Unexpected end of file\n");
                    goto fail;
                }
                value = my_strtok_r(line, "/ \t", &saveptr);
            }
            if(strcmp(value, "}") != 0)
            {
                ERR("Expected } after matrix definitions, got %s\n", value);
                goto fail;
            }
        }
        else if(strcmp(command, "end") == 0)
        {
            line = my_strtok_r(nullptr, "/ \t", &saveptr);
            if(line)
            {
                ERR("Unexpected junk on end: %s\n", line);
                goto fail;
            }

            fclose(f);
            return 1;
        }
        else
        {
            ERR("Unexpected command: %s\n", command);
            goto fail;
        }

        line = my_strtok_r(nullptr, "/ \t", &saveptr);
        if(line)
        {
            ERR("Unexpected junk on line: %s\n", line);
            goto fail;
        }
    }
    ERR("Unexpected end of file\n");

fail:
    fclose(f);
    return 0;
}
