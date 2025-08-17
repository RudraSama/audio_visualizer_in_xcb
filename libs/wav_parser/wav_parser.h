#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef WAV_PARSER_H
#define WAV_PARSER_H

static uint8_t riff_bytes[] = {0x52, 0x49, 0x46, 0x46};
static uint8_t format_bytes[] = {0x57, 0x41, 0x56, 0x45};
static uint8_t format_block_id[] = {0x66, 0x6D, 0x74, 0x20};
static uint8_t sample_block_id[] = {0x64, 0x61, 0x74, 0x61};

typedef struct {
	uint32_t file_size;
	uint16_t audio_format;
	uint16_t nbr_channels;
	uint32_t frequency;
	uint32_t bytes_per_second;
	uint16_t bits_per_sample;
	uint16_t bytes_per_block;
	uint32_t sample_size;
	uint32_t frame_size;
	uint8_t *buffer;
} WAV;

int load_wav(const char *filename, WAV *wav);
int check_for_block_id(FILE *file);
int parsing_wav(FILE *file, WAV *wav);
int get_mono_samples(WAV *wav, uint16_t **mono_buffer);
void display_info(WAV *wav);

#endif
