#include "wav_parser.h"

int load_wav(const char *filename, WAV *wav) {
	FILE *file;

	if ((file = fopen(filename, "rb")) == NULL) {
		printf("Opening Wav file failed\n");
		return 0;
	}

	if (parsing_wav(file, wav) == 0) {
		printf("Parsing Wav file failed\n");
		fclose(file);
		return 0;
	}

	fclose(file);
	return 1;
}

int check_for_block_id(FILE *file) {
	int i = 1;
	int byte;

	while (i < sizeof(sample_block_id) / sizeof(uint8_t)) {
		byte = fgetc(file);
		if (byte == EOF || sample_block_id[i] != (uint8_t)byte) return 0;
		i++;
	}

	return 1;
}

int parsing_wav(FILE *file, WAV *wav) {
	int byte;
	int i = 0;

	/*[Master RIFF chunk] */
	/* Checking RIFF bytes */
	while (i < sizeof(riff_bytes) / sizeof(uint8_t)) {
		byte = fgetc(file);
		if (byte == EOF || riff_bytes[i] != (uint8_t)byte) {
			printf("Error in RIFF code");
			return 0;
		};
		i++;
	}

	/* Reading file size */
	i = 0;
	wav->file_size = 0;
	while (i < 4) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		wav->file_size = wav->file_size | (((uint32_t)byte) << 8 * i);
		i++;
	}
	/* File size in WAV is minus 8, so we need to add to get original file
	 * size*/
	wav->file_size += 8;

	/* Checking Format bytes */
	i = 0;
	while (i < sizeof(format_bytes) / sizeof(uint8_t)) {
		byte = fgetc(file);
		if (byte == EOF || format_bytes[i] != (uint8_t)byte) {
			printf("Error in Format Bytes code");
			return 0;
		};
		i++;
	}

	/*[Chunk describing the data format]*/
	i = 0;
	while (i < sizeof(format_block_id) / sizeof(uint8_t)) {
		byte = fgetc(file);
		if (byte == EOF || format_block_id[i] != (uint8_t)byte) {
			printf("Error in Format Block Id code");
			return 0;
		};
		i++;
	}

	/* Skippin Chunk Size 4 bytes*/
	fgetc(file);
	fgetc(file);
	fgetc(file);
	fgetc(file);

	/* Reading Audio format */
	wav->audio_format = 0x0000;
	i = 0;
	while (i < 2) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		wav->audio_format = wav->audio_format | ((uint32_t)byte << 8 * i);
		i++;
	}

	/* Reading Number of channels*/
	wav->nbr_channels = 0x0000;
	i = 0;
	while (i < 2) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		wav->nbr_channels = wav->nbr_channels | ((uint32_t)byte << 8 * i);
		i++;
	}

	/* Reading Frequency */
	wav->frequency = 0x000000;
	i = 0;
	while (i < 4) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		wav->frequency = wav->frequency | (((uint32_t)byte) << 8 * i);
		i++;
	}

	/* Reading Bytes Per Second */
	wav->bytes_per_second = 0;
	i = 0;
	while (i < 4) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		wav->bytes_per_second =
		    wav->bytes_per_second | (((uint32_t)byte) << 8 * i);
		i++;
	}

	/* Reading Bytes Per Block */
	wav->bytes_per_block = 0;
	i = 0;
	while (i < 2) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		wav->bytes_per_block =
		    wav->bytes_per_block | (((uint32_t)byte) << 8 * i);
		i++;
	}

	/* Reading Bits Per Sample */
	wav->bits_per_sample = 0;
	i = 0;
	while (i < 2) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		wav->bits_per_sample =
		    wav->bits_per_sample | (((uint32_t)byte) << 8 * i);
		i++;
	}

	/* Checking Sample Block ID */
	while (1) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		if (byte == sample_block_id[0] && check_for_block_id(file)) break;
	}

	/* Reading Sample Data Size */
	wav->sample_size = 0;
	i = 0;
	while (i < 4) {
		byte = fgetc(file);
		if (byte == EOF) return 0;
		wav->sample_size = wav->sample_size | (((uint32_t)byte) << 8 * i);
		i++;
	}

	wav->buffer = (uint8_t *)calloc(1, wav->sample_size);

	i = 0;
	while (i < wav->sample_size) {
		wav->buffer[i] = (uint8_t)fgetc(file);
		i++;
	}

	wav->frame_size = (wav->bits_per_sample / 8) * wav->nbr_channels;

	return 1;
}

int get_mono_samples(WAV *wav, uint16_t **mono_buffer) {
	uint32_t total_mono_bytes = wav->sample_size / (wav->frame_size);

	*mono_buffer = (uint16_t *)malloc(sizeof(*mono_buffer) * total_mono_bytes);

	uint32_t i = 0;

	uint32_t k = 0;
	while (i < wav->sample_size) {
		uint8_t lo = wav->buffer[i];
		i++;
		uint8_t hi = wav->buffer[i];
		i++;

		int16_t left = ((int16_t)hi << 8) | lo;

		lo = wav->buffer[i];
		i++;
		hi = wav->buffer[i];
		i++;

		int16_t right = ((int16_t)hi << 8) | lo;
		(*mono_buffer)[k] = (uint16_t)abs(left);
		k++;
	}

	return 1;
}

void display_info(WAV *wav) {
	printf("File Size %d\n", wav->file_size);
	printf("Audio format %d\n", wav->audio_format);
	printf("Channel %d\n", wav->nbr_channels);
	printf("Frequency %d\n", wav->frequency);
	printf("Bytes per second %d\n", wav->bytes_per_second);
	printf("Bits per Sample %d\n", wav->bits_per_sample);
	printf("Bytes per block %d\n", wav->bytes_per_block);
	printf("Sample Size %d\n", wav->sample_size);
	printf("Frame Size %d\n", wav->frame_size);
}
