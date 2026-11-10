/* CoreAudio output backend for classic Doom sound effects. */
#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>

/* The legacy engine declares true/false as enum members. */
#undef true
#undef false

#include "i_sound.h"
#include "i_system.h"
#include "w_wad.h"
#include "z_zone.h"

#define DOOM_AUDIO_RATE 11025.0
#define DOOM_AUDIO_CHANNELS 8
#define DOOM_AUDIO_FRAMES 512

typedef struct
{
    const uint8_t *samples;
    uint32_t length;
    double position;
    double step;
    int leftVolume;
    int rightVolume;
    int handle;
    int active;
} macos_channel_t;

static AudioQueueRef audioQueue;
static pthread_mutex_t audioMutex = PTHREAD_MUTEX_INITIALIZER;
static macos_channel_t channels[DOOM_AUDIO_CHANNELS];
static uint32_t soundLengths[NUMSFX];
static int nextHandle = 1;
static int audioReady;
static AUGraph musicGraph;
static AudioUnit musicSynth;
static int musicVolume = 127;
static int musicInitialized;

typedef struct
{
    uint8_t *score;
    size_t length;
    size_t position;
    uint8_t velocity[16];
    double nextEventTime;
    int looping;
    int playing;
    int paused;
} mus_song_t;

static mus_song_t *registeredSongs[16];
static void UpdateMusic(void);

static uint16_t ReadLE16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t ClampSample(int value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static void AudioOutputCallback(void *userData, AudioQueueRef queue,
                                AudioQueueBufferRef buffer)
{
    (void)userData;
    int16_t *output = (int16_t *)buffer->mAudioData;
    const UInt32 frames = DOOM_AUDIO_FRAMES;
    pthread_mutex_lock(&audioMutex);
    for (UInt32 frame = 0; frame < frames; ++frame)
    {
        int left = 0;
        int right = 0;
        for (int c = 0; c < DOOM_AUDIO_CHANNELS; ++c)
        {
            macos_channel_t *channel = &channels[c];
            if (!channel->active)
                continue;
            uint32_t index = (uint32_t)channel->position;
            if (index >= channel->length)
            {
                channel->active = 0;
                continue;
            }
            int sample = (int)channel->samples[index] - 128;
            left += sample * channel->leftVolume * 256 / 127;
            right += sample * channel->rightVolume * 256 / 127;
            channel->position += channel->step;
        }
        output[frame * 2] = ClampSample(left);
        output[frame * 2 + 1] = ClampSample(right);
    }
    pthread_mutex_unlock(&audioMutex);
    buffer->mAudioDataByteSize = frames * 2 * sizeof(int16_t);
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

static void CacheSound(int id)
{
    sfxinfo_t *sfx = &S_sfx[id];
    if (sfx->link)
    {
        CacheSound((int)(sfx->link - S_sfx));
        sfx->data = sfx->link->data;
        soundLengths[id] = soundLengths[sfx->link - S_sfx];
        return;
    }

    char name[9];
    snprintf(name, sizeof(name), "ds%s", sfx->name);
    int lump = W_CheckNumForName(name);
    if (lump < 0)
        lump = W_GetNumForName("dspistol");
    int lumpLength = W_LumpLength(lump);
    if (lumpLength < 8)
        I_Error("Invalid sound lump: %s", name);

    const uint8_t *lumpData = (const uint8_t *)W_CacheLumpNum(lump, PU_STATIC);
    uint32_t sampleCount = (uint32_t)lumpData[4]
                         | ((uint32_t)lumpData[5] << 8)
                         | ((uint32_t)lumpData[6] << 16)
                         | ((uint32_t)lumpData[7] << 24);
    if (sampleCount > (uint32_t)(lumpLength - 8))
        sampleCount = (uint32_t)(lumpLength - 8);
    uint8_t *samples = (uint8_t *)malloc(sampleCount ? sampleCount : 1);
    if (!samples)
        I_Error("Could not allocate sound lump: %s", name);
    memcpy(samples, lumpData + 8, sampleCount);
    Z_Free((void *)lumpData);
    sfx->data = samples;
    soundLengths[id] = sampleCount;
}

void I_InitSound(void)
{
    AudioStreamBasicDescription format;
    memset(&format, 0, sizeof(format));
    format.mSampleRate = DOOM_AUDIO_RATE;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = 4;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 4;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 16;

    OSStatus status = AudioQueueNewOutput(&format, AudioOutputCallback, NULL,
                                          NULL, NULL, 0, &audioQueue);
    if (status != noErr)
    {
        fprintf(stderr, "CoreAudio unavailable (AudioQueueNewOutput: %d); sound disabled.\n", (int)status);
        I_InitMusic();
        return;
    }

    for (int i = 1; i < NUMSFX; ++i)
        CacheSound(i);

    for (int i = 0; i < 3; ++i)
    {
        AudioQueueBufferRef buffer;
        status = AudioQueueAllocateBuffer(audioQueue,
            DOOM_AUDIO_FRAMES * 2 * sizeof(int16_t), &buffer);
        if (status != noErr)
            I_Error("Could not allocate CoreAudio buffer (%d)", (int)status);
        AudioOutputCallback(NULL, audioQueue, buffer);
    }
    status = AudioQueueStart(audioQueue, NULL);
    if (status != noErr)
        I_Error("Could not start CoreAudio output (%d)", (int)status);
    audioReady = 1;
    fprintf(stderr, "I_InitSound: CoreAudio sound effects ready (11025 Hz).\n");
    I_InitMusic();
}

void I_UpdateSound(void) { UpdateMusic(); }
void I_SubmitSound(void) {}
void I_SetChannels(void) {}
int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    char name[9];
    snprintf(name, sizeof(name), "ds%s", sfxinfo->name);
    return W_GetNumForName(name);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    (void)priority;
    if (!audioReady || id < 1 || id >= NUMSFX || !S_sfx[id].data)
        return 0;

    int left = vol - ((vol * sep * sep) >> 16);
    int offset = sep - 257;
    int right = vol - ((vol * offset * offset) >> 16);
    if (left < 0) left = 0; if (left > 127) left = 127;
    if (right < 0) right = 0; if (right > 127) right = 127;

    double frequencyScale = (double)(pitch > 0 ? pitch : 128) / 128.0;
    pthread_mutex_lock(&audioMutex);
    int slot = 0;
    for (int i = 0; i < DOOM_AUDIO_CHANNELS; ++i)
        if (!channels[i].active) { slot = i; break; }
    int handle = nextHandle++;
    if (nextHandle <= 0) nextHandle = 1;
    channels[slot].samples = (const uint8_t *)S_sfx[id].data;
    channels[slot].length = soundLengths[id];
    channels[slot].position = 0;
    channels[slot].step = frequencyScale;
    channels[slot].leftVolume = left;
    channels[slot].rightVolume = right;
    channels[slot].handle = handle;
    channels[slot].active = channels[slot].length != 0;
    pthread_mutex_unlock(&audioMutex);
    return handle;
}

void I_StopSound(int handle)
{
    pthread_mutex_lock(&audioMutex);
    for (int i = 0; i < DOOM_AUDIO_CHANNELS; ++i)
        if (channels[i].handle == handle) channels[i].active = 0;
    pthread_mutex_unlock(&audioMutex);
}

int I_SoundIsPlaying(int handle)
{
    int playing = 0;
    pthread_mutex_lock(&audioMutex);
    for (int i = 0; i < DOOM_AUDIO_CHANNELS; ++i)
        if (channels[i].handle == handle && channels[i].active) playing = 1;
    pthread_mutex_unlock(&audioMutex);
    return playing;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    int left = vol - ((vol * sep * sep) >> 16);
    int offset = sep - 257;
    int right = vol - ((vol * offset * offset) >> 16);
    if (left < 0) left = 0; if (left > 127) left = 127;
    if (right < 0) right = 0; if (right > 127) right = 127;
    pthread_mutex_lock(&audioMutex);
    for (int i = 0; i < DOOM_AUDIO_CHANNELS; ++i)
        if (channels[i].handle == handle && channels[i].active)
        {
            channels[i].leftVolume = left;
            channels[i].rightVolume = right;
            channels[i].step = (double)(pitch > 0 ? pitch : 128) / 128.0;
        }
    pthread_mutex_unlock(&audioMutex);
}

void I_ShutdownSound(void)
{
    if (audioQueue)
    {
        AudioQueueStop(audioQueue, true);
        AudioQueueDispose(audioQueue, true);
        audioQueue = NULL;
    }
    for (int i = 1; i < NUMSFX; ++i)
    {
        if (S_sfx[i].data && (!S_sfx[i].link || S_sfx[i].data != S_sfx[i].link->data))
            free(S_sfx[i].data);
        S_sfx[i].data = NULL;
        soundLengths[i] = 0;
    }
    audioReady = 0;
}

static void SendMIDI(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (musicSynth)
        MusicDeviceMIDIEvent(musicSynth, status, data1, data2, 0);
}

static void ResetSongPosition(mus_song_t *song)
{
    song->position = 0;
    memset(song->velocity, 100, sizeof(song->velocity));
}

void I_InitMusic(void)
{
    if (musicInitialized)
        return;

    OSStatus status;
    AUNode synthNode, outputNode;
    AudioComponentDescription synthDescription = {
        .componentType = kAudioUnitType_MusicDevice,
        .componentSubType = kAudioUnitSubType_DLSSynth,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0
    };
    AudioComponentDescription outputDescription = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0
    };

    status = NewAUGraph(&musicGraph);
    if (status == noErr) status = AUGraphAddNode(musicGraph, &synthDescription, &synthNode);
    if (status == noErr) status = AUGraphAddNode(musicGraph, &outputDescription, &outputNode);
    if (status == noErr) status = AUGraphConnectNodeInput(musicGraph, synthNode, 0, outputNode, 0);
    if (status == noErr) status = AUGraphOpen(musicGraph);
    if (status == noErr) status = AUGraphNodeInfo(musicGraph, synthNode, NULL, &musicSynth);
    if (status == noErr) status = AUGraphInitialize(musicGraph);
    if (status == noErr) status = AUGraphStart(musicGraph);
    if (status != noErr)
    {
        fprintf(stderr, "CoreAudio MIDI music unavailable (AudioUnit: %d).\n", (int)status);
        if (musicGraph) DisposeAUGraph(musicGraph);
        musicGraph = NULL;
        musicSynth = NULL;
        return;
    }
    musicInitialized = 1;
    I_SetMusicVolume(musicVolume);
    fprintf(stderr, "I_InitMusic: CoreAudio DLS synthesizer ready.\n");
}

void I_ShutdownMusic(void)
{
    for (int i = 0; i < 16; ++i)
    {
        free(registeredSongs[i]);
        registeredSongs[i] = NULL;
    }
    if (musicGraph)
    {
        AUGraphStop(musicGraph);
        DisposeAUGraph(musicGraph);
        musicGraph = NULL;
        musicSynth = NULL;
    }
    musicInitialized = 0;
}

void I_SetMusicVolume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    else if (volume <= 15) volume = volume * 127 / 15;
    musicVolume = volume;
    if (musicSynth)
        AudioUnitSetParameter(musicSynth, kMusicDeviceParam_Volume,
                              kAudioUnitScope_Global, 0, (Float32)volume / 127.0f, 0);
}

void I_PauseSong(int handle)
{
    if (handle >= 1 && handle <= 16 && registeredSongs[handle - 1])
        registeredSongs[handle - 1]->paused = 1;
}

void I_ResumeSong(int handle)
{
    if (handle >= 1 && handle <= 16 && registeredSongs[handle - 1])
    {
        mus_song_t *song = registeredSongs[handle - 1];
        song->paused = 0;
        song->nextEventTime = CFAbsoluteTimeGetCurrent();
    }
}

int I_RegisterSong(void *data)
{
    const uint8_t *bytes = (const uint8_t *)data;
    if (!bytes || memcmp(bytes, "MUS\x1a", 4) != 0)
    {
        fprintf(stderr, "I_RegisterSong: invalid MUS data.\n");
        return 0;
    }
    uint16_t scoreLength = ReadLE16(bytes + 4);
    uint16_t scoreStart = ReadLE16(bytes + 6);
    if (scoreStart < 16 || scoreLength == 0)
        return 0;
    int lumpLength = -1;
    for (int i = 0; i < NUMMUSIC; ++i)
        if (S_music[i].data == data && S_music[i].lumpnum >= 0)
        {
            lumpLength = W_LumpLength(S_music[i].lumpnum);
            break;
        }
    if (lumpLength < 0 || (uint32_t)scoreStart + scoreLength > (uint32_t)lumpLength)
    {
        fprintf(stderr, "I_RegisterSong: MUS score is outside its WAD lump.\n");
        return 0;
    }

    int slot = -1;
    for (int i = 0; i < 16; ++i)
        if (!registeredSongs[i]) { slot = i; break; }
    if (slot < 0)
        return 0;

    mus_song_t *song = (mus_song_t *)calloc(1, sizeof(*song));
    if (!song) return 0;
    song->score = (uint8_t *)malloc(scoreLength);
    if (!song->score) { free(song); return 0; }
    memcpy(song->score, bytes + scoreStart, scoreLength);
    song->length = scoreLength;
    ResetSongPosition(song);
    registeredSongs[slot] = song;
    return slot + 1;
}

void I_PlaySong(int handle, int looping)
{
    if (handle < 1 || handle > 16 || !registeredSongs[handle - 1] || !musicSynth)
        return;
    mus_song_t *song = registeredSongs[handle - 1];
    ResetSongPosition(song);
    song->looping = looping;
    song->playing = 1;
    song->paused = 0;
    song->nextEventTime = CFAbsoluteTimeGetCurrent();
}

void I_StopSong(int handle)
{
    if (handle < 1 || handle > 16 || !registeredSongs[handle - 1])
        return;
    registeredSongs[handle - 1]->playing = 0;
    for (int channel = 0; channel < 16; ++channel)
        SendMIDI((uint8_t)(0xb0 | channel), 123, 0);
}

void I_UnRegisterSong(int handle)
{
    if (handle < 1 || handle > 16 || !registeredSongs[handle - 1])
        return;
    free(registeredSongs[handle - 1]->score);
    free(registeredSongs[handle - 1]);
    registeredSongs[handle - 1] = NULL;
}

static int ReadMUSEvent(mus_song_t *song, uint32_t *delay)
{
    if (song->position >= song->length)
        return 0;
    uint8_t descriptor = song->score[song->position++];
    int type = (descriptor >> 4) & 7;
    int channel = descriptor & 15;
    int midiChannel = channel == 15 ? 9 : (channel > 9 ? channel + 1 : channel);
    uint8_t a = 0, b = 0;
    switch (type)
    {
        case 0:
            if (song->position >= song->length) return 0;
            SendMIDI((uint8_t)(0x80 | midiChannel), song->score[song->position++] & 0x7f, 0);
            break;
        case 1:
            if (song->position >= song->length) return 0;
            a = song->score[song->position++];
            if (a & 0x80)
            {
                if (song->position >= song->length) return 0;
                song->velocity[channel] = song->score[song->position++] & 0x7f;
            }
            SendMIDI((uint8_t)(0x90 | midiChannel), a & 0x7f, song->velocity[channel]);
            break;
        case 2:
            if (song->position >= song->length) return 0;
            a = song->score[song->position++];
            SendMIDI((uint8_t)(0xe0 | midiChannel), (uint8_t)((a << 6) & 0x7f), (uint8_t)(a >> 1));
            break;
        case 3:
        case 4:
            if (song->position >= song->length) return 0;
            a = song->score[song->position++];
            if (type == 4)
            {
                if (song->position >= song->length) return 0;
                b = song->score[song->position++] & 0x7f;
                if (a == 0) SendMIDI((uint8_t)(0xc0 | midiChannel), b, 0);
                else
                {
                    static const uint8_t cc[15] = {0,0,1,7,10,11,91,93,64,67,120,123,126,127,121};
                    if (a < 15) SendMIDI((uint8_t)(0xb0 | midiChannel), cc[a], b);
                }
            }
            else if (a >= 10 && a <= 14)
            {
                static const uint8_t cc[15] = {0,0,1,7,10,11,91,93,64,67,120,123,126,127,121};
                SendMIDI((uint8_t)(0xb0 | midiChannel), cc[a], 0);
            }
            break;
        case 6:
            return -1;
        default:
            break;
    }

    *delay = 0;
    if (descriptor & 0x80)
    {
        uint8_t value;
        do
        {
            if (song->position >= song->length) return 0;
            value = song->score[song->position++];
            *delay = (*delay << 7) | (value & 0x7f);
        } while (value & 0x80);
    }
    return 1;
}

static void UpdateMusic(void)
{
    if (!musicSynth)
        return;
    double now = CFAbsoluteTimeGetCurrent();
    for (int slot = 0; slot < 16; ++slot)
    {
        mus_song_t *song = registeredSongs[slot];
        if (!song || !song->playing || song->paused)
            continue;
        for (int count = 0; count < 4096 && now >= song->nextEventTime; ++count)
        {
            uint32_t delay = 0;
            int result = ReadMUSEvent(song, &delay);
            if (result < 0)
            {
                if (!song->looping) { song->playing = 0; break; }
                ResetSongPosition(song);
                song->nextEventTime += 0.001;
                continue;
            }
            if (!result) { song->playing = 0; break; }
            song->nextEventTime += (double)delay / 140.0;
        }
    }
}
