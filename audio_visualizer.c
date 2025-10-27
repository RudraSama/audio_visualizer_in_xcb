#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <xcb/shm.h>
#include <xcb/xcb.h>

#define STB_IMAGE_IMPLEMENTATION
#include "libs/fft/fft.h"
#include "libs/stb_image.h"
#include "libs/wav_parser/wav_parser.h"

static const int WIDTH = 1024;
static const int HEIGHT = 600;

static const uint32_t sample_size = 1024;
static const uint16_t smooth_factor = 2;

static const uint16_t total_bars = (sample_size / 2 / smooth_factor);
static const uint16_t bar_width = 2;
static const uint16_t gap = 2;

static const uint32_t interval = 23;  // Milliseconds

static const char *device = "default"; /* playback device */

static const uint8_t gray_threshold = 100;
static const uint8_t invert_image = 0x00;

atomic_int worker_thread_running = 1;
atomic_int need_pixmap_update = 0;

static uint64_t MAX_AMP = 1;


typedef struct {
	uint8_t bpp;
	uint8_t *buffer;
	uint8_t bytes_per_pixel;
	uint32_t bytes_per_row;
	uint32_t total_bytes;
} Pixmap;

typedef struct {
	uint16_t length;
	uint16_t position;
	uint8_t rev;
} Bar;

typedef struct {
	int W;
	int H;
	int frames;
	int comp;
	int delay;
	uint8_t *buffer;
} GIF;

typedef struct {
	uint16_t *mono_buffer;
	Pixmap *pixmap;
	Bar *bar;
	WAV *wav;
	GIF *gif;
	uint32_t audio_frames_in_time;
	uint32_t *offset;
} Arg_Struct;

uint64_t get_milis() {
	struct timespec ts = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &ts);

	uint64_t nsec = ts.tv_nsec / 1000000;
	uint64_t sec = (uint64_t)ts.tv_sec * 1000;
	return nsec + sec;
}


xcb_window_t setup_window(xcb_connection_t *conn, xcb_screen_t *screen) {
	xcb_window_t window = xcb_generate_id(conn);

	uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t values[] = {screen->black_pixel,
	                     XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS};

	xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0,
	                  WIDTH, HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
	                  screen->root_visual, mask, values);

	return window;
}


xcb_gcontext_t setup_gcontext(xcb_connection_t *conn, xcb_screen_t *screen,
                              xcb_window_t window) {
	xcb_gcontext_t gc = xcb_generate_id(conn);

	uint32_t mask_gc = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND;
	uint32_t values_gc[] = {screen->black_pixel, screen->white_pixel};

	xcb_create_gc(conn, gc, window, mask_gc, values_gc);

	return gc;
}


int bpp_by_depth(xcb_connection_t *conn, int depth) {
	const xcb_setup_t *setup = xcb_get_setup(conn);

	xcb_format_iterator_t fmt_iter = xcb_setup_pixmap_formats_iterator(setup);

	for (; fmt_iter.rem; xcb_format_next(&fmt_iter)) {
		if (fmt_iter.data->depth == depth) return fmt_iter.data->bits_per_pixel;
	}

	return 0;
}


uint8_t rgb_to_grayscale(uint8_t R, uint8_t G, uint8_t B) {
	return (uint8_t)(0.299 * R + 0.587 * G + 0.114 * B);
}


int load_gif(const char *filename, GIF *gif) {
	FILE *file;
	stbi__context s;

	if (!(file = stbi__fopen(filename, "rb")))
		printf("can't fopen", "Unable to open file");
	stbi__start_file(&s, file);

	int x = 0;
	int y = 0;
	int z = 0;
	int comp = 0;
	int *delay = 0;

	uint8_t *buffer = stbi__load_gif_main(&s, &delay, &x, &y, &z, &comp, 4);

	uint32_t total_bytes = x * comp * y * z;

	if (buffer == NULL) {
		printf("Could not read GIF file");
		fclose(file);
		return 0;
	}

	if (x > WIDTH || y > HEIGHT) {
		printf(
		    "Width and Height of GIF can't be more than WINDOW's Width and "
		    "Height'");
		return 0;
	}

	gif->W = x;
	gif->H = y;
	gif->frames = z;
	gif->comp = comp;
	gif->delay = *delay;
	gif->buffer = (uint8_t *)malloc(sizeof(uint8_t) * total_bytes);

	if (gif->buffer == NULL) return 0;

	memcpy(gif->buffer, buffer, total_bytes);

	free(buffer);
	free(delay);
	fclose(file);

	return 1;
}


void draw_image(Pixmap *pixmap, GIF *gif, uint8_t frame) {
	uint16_t start_x = (WIDTH / 2) - (gif->W / 2);
	uint16_t start_y = 50;

	if (frame >= gif->frames) frame = 0;

	uint32_t total_frame_bytes = gif->W * gif->comp * gif->H;

	for (uint16_t y = 0; y < HEIGHT; y++) {
		for (uint16_t x = 0; x < WIDTH; x++) {
			uint32_t pixmap_offset =
			    x * pixmap->bytes_per_pixel + y * pixmap->bytes_per_row;

			if ((x >= start_x && x < (gif->W + start_x)) &&
			    (y >= start_y && y < (gif->H + start_y))) {
				uint32_t gif_offset = (x - start_x) * gif->comp +
				                      (y - start_y) * gif->W * gif->comp;

				uint8_t R =
				    (total_frame_bytes * frame + gif->buffer)[gif_offset + 0];
				uint8_t G =
				    (total_frame_bytes * frame + gif->buffer)[gif_offset + 1];
				uint8_t B =
				    (total_frame_bytes * frame + gif->buffer)[gif_offset + 2];

				uint8_t gray = rgb_to_grayscale(R, G, B);

				if (gray > gray_threshold) {
					pixmap->buffer[pixmap_offset + 0] = 0xFF ^ invert_image;
					pixmap->buffer[pixmap_offset + 1] = 0xFF ^ invert_image;
					pixmap->buffer[pixmap_offset + 2] = 0xFF ^ invert_image;
				} else {
					pixmap->buffer[pixmap_offset + 0] = 0x00 | invert_image;
					pixmap->buffer[pixmap_offset + 1] = 0x00 | invert_image;
					pixmap->buffer[pixmap_offset + 2] = 0x00 | invert_image;
				}
			} else {
				pixmap->buffer[pixmap_offset + 0] = 0x00;
				pixmap->buffer[pixmap_offset + 1] = 0x00;
				pixmap->buffer[pixmap_offset + 2] = 0x00;
			}
		}
	}
}


void clear_pixmap(Pixmap *pixmap) {
	for (uint16_t y = 0; y < HEIGHT; y++) {
		for (uint16_t x = 0; x < WIDTH; x++) {
			uint32_t offset =
			    pixmap->bytes_per_pixel * x + pixmap->bytes_per_row * y;
			pixmap->buffer[offset + 0] = 0x00;
			pixmap->buffer[offset + 1] = 0x00;
			pixmap->buffer[offset + 2] = 0x00;
		}
	}
}


void create_bar(Pixmap *pixmap, uint16_t start, uint16_t width, uint16_t length,
                uint32_t color) {
	uint8_t R = 0, G = 0, B = 0;
	B = B | (color >> 16);
	G = G | (color >> 8);
	R = R | color;

	for (uint16_t y = HEIGHT - length; y < HEIGHT; y++) {
		for (uint16_t x = start; x <= start + width; x++) {
			uint32_t offset =
			    pixmap->bytes_per_pixel * x + pixmap->bytes_per_row * y;
			pixmap->buffer[offset + 0] = B;
			pixmap->buffer[offset + 1] = G;
			pixmap->buffer[offset + 2] = R;
		}
	}
}


uint64_t *get_bands(WAV *wav, uint16_t *buffer, uint32_t total_frames,
                    uint32_t offset) {
	double complex *samples =
	    (double complex *)malloc(sizeof(*samples) * sample_size);

	if (samples == NULL) return NULL;

	for (uint32_t i = 0; i < total_frames && i < sample_size; i++)
		samples[i] = (double complex)buffer[offset + i];

	for (uint32_t i = total_frames; i < sample_size; i++) samples[i] = 0;

	double complex *bin = fft(samples, sample_size);

	if (bin == NULL) return NULL;

	uint64_t *output =
	    (uint64_t *)malloc(sizeof(*output) * (sample_size / 2 / smooth_factor));

	if (output == NULL) return NULL;

	uint32_t output_index = 0;
	uint32_t bin_index = 0;
	while (bin_index < sample_size / 2) {
		uint64_t avg_mag = 0;
		if (bin_index < 4) {
			bin_index++;
			continue;
		}

		for (uint32_t j = 0; j < smooth_factor; j++) {
			uint64_t magnitude = (uint64_t)cabs(bin[bin_index]);
			avg_mag = avg_mag + magnitude;
			bin_index++;
		}

		output[output_index] = avg_mag / smooth_factor;
		if (output[output_index] > MAX_AMP) MAX_AMP = output[output_index];
		output_index++;
	}

	free(bin);
	free(samples);
	return output;
}


void *update_pixmap(void *arg) {
	Arg_Struct *arg_struct = (Arg_Struct *)arg;

	uint8_t i = 0;
	uint8_t N = (arg_struct->gif)->frames;
	uint64_t start = get_milis();

	while (atomic_load(&worker_thread_running) > 0) {
		if (atomic_load(&need_pixmap_update) > 0) {
			draw_image(arg_struct->pixmap, arg_struct->gif, i);

			uint64_t end = get_milis();
			if (end - start > (arg_struct->gif)->delay) {
				i = (i % (N - 1)) + 1;
				start = get_milis();
			}
			uint64_t *band = get_bands(arg_struct->wav, arg_struct->mono_buffer,
			                           arg_struct->audio_frames_in_time,
			                           *(arg_struct->offset));

			if (band == NULL) {
				atomic_store(&need_pixmap_update, 0);
				continue;
			}

			for (uint32_t i = 0; i < total_bars; i++) {
				float new_h =
				    (float)((uint64_t)band[i] * (uint64_t)HEIGHT / 2 / MAX_AMP);
				create_bar(arg_struct->pixmap, (arg_struct->bar)[i].position,
				           bar_width, new_h, 0xFFFFFF);
			}

			atomic_store(&need_pixmap_update, 0);
		}
	}
	return NULL;
}


int main(int argc, char *argv[]) {
	if (argc < 2) {
		printf("Provide WAV and GIF file\n");
		return 0;
	}

	WAV wav = {0};

	if (load_wav(argv[1], &wav) == 0) return 0;

	uint16_t *mono_buffer = NULL;

	if (get_mono_samples(&wav, &mono_buffer) == 0) return 0;

	GIF gif = {0};
	if (load_gif(argv[2], &gif) == 0) return 0;

	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	xcb_window_t window = setup_window(conn, screen);

	xcb_map_window(conn, window);

	xcb_gcontext_t gc = setup_gcontext(conn, screen, window);

	xcb_pixmap_t pixmap = xcb_generate_id(conn);
	xcb_create_pixmap(conn, screen->root_depth, pixmap, window, WIDTH, HEIGHT);

	uint8_t bpp = bpp_by_depth(conn, screen->root_depth);
	uint32_t bytes_per_row = WIDTH * (bpp / 8);
	uint32_t total_bytes = bytes_per_row * HEIGHT;

	Pixmap pix = {.bpp = bpp,
	              .bytes_per_pixel = bpp / 8,
	              .bytes_per_row = bytes_per_row,
	              .total_bytes = bytes_per_row * HEIGHT,
	              .buffer = (uint8_t *)malloc(sizeof(uint8_t) * total_bytes)};

	/* Checking If Bars fit perfectly in WINDOW or not */
	if (total_bars * bar_width + gap * total_bars > WIDTH) {
		printf("Too Many bars for this Window");
		return 0;
	}

	/* Initializing Bars */
	Bar bar[total_bars];
	for (uint16_t i = 0; i < total_bars; i++) {
		Bar b;
		b.length = 1;
		b.position = i * (bar_width + gap);
		b.rev = 0;
		bar[i] = b;
	}

	/* Initializing ALSA SOUND */
	int err;
	snd_pcm_t *handle;
	snd_pcm_sframes_t frames;

	if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}

	if ((err = snd_pcm_set_params(
	         handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
	         wav.nbr_channels, wav.frequency, 1, 5000)) < 0) { /* 0.5sec */
		printf("Playback open error: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}

	uint32_t audio_frames_per_second = wav.bytes_per_second / wav.frame_size;
	uint32_t total_audio_frames = wav.sample_size / wav.frame_size;
	uint32_t audio_frames_in_time =
	    (float)audio_frames_per_second / (float)1000 * (float)interval;
	uint32_t offset = 0;

	Arg_Struct arg_struct = {.pixmap = &pix,
	                         .mono_buffer = mono_buffer,
	                         .bar = &bar[0],
	                         .gif = &gif,
	                         .audio_frames_in_time = audio_frames_in_time,
	                         .offset = &offset};

	pthread_t worker_thread;
	pthread_create(&worker_thread, NULL, &update_pixmap, (void *)&arg_struct);

	int quit = 0;
	while (!quit) {
		xcb_generic_event_t *event = xcb_poll_for_event(conn);

		if (event) {
			switch (event->response_type & ~0x80) {
				case XCB_EXPOSE:
					xcb_copy_area(conn, pixmap, window, gc, 0, 0, 0, 0, WIDTH,
					              HEIGHT);
					xcb_flush(conn);
					break;
				case XCB_KEY_PRESS:
					xcb_key_press_event_t *env = (xcb_key_press_event_t *)event;
					if (env->detail == 9) quit = 1;
					break;
			}
		}

		atomic_store(&need_pixmap_update, 1);

		frames = snd_pcm_writei(handle, wav.buffer + offset * wav.frame_size,
		                        audio_frames_in_time);
		if (frames < 0) frames = snd_pcm_recover(handle, frames, 0);

		offset = offset + audio_frames_in_time;

		if (offset > total_audio_frames) quit = 1;

		if (atomic_load(&need_pixmap_update) == 1) continue;

		xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, gc, WIDTH,
		              HEIGHT, 0, 0, 0, screen->root_depth, pix.total_bytes,
		              pix.buffer);
		xcb_copy_area(conn, pixmap, window, gc, 0, 0, 0, 0, WIDTH, HEIGHT);
		xcb_flush(conn);
	}

	/* Closing ALSA SOUND */
	err = snd_pcm_drain(handle);
	if (err < 0) printf("snd_pcm_drain failed: %s\n", snd_strerror(err));
	snd_pcm_close(handle);

	atomic_store(&worker_thread_running, 0);
	pthread_join(worker_thread, NULL);

	free(pix.buffer);
	free(wav.buffer);
	free(gif.buffer);
	free(mono_buffer);
	xcb_free_pixmap(conn, pixmap);
	xcb_free_gc(conn, gc);
	xcb_disconnect(conn);
	return 0;
}
