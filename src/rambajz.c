#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "analyser.h"
#include "buffer.h"
#include "jack.h"
#include "scale.h"
#include "sdl.h"
#include "util.h"

const size_t BUFSIZE = (1 << 20);

// Assumption: nframes is constant throughout execution
int record(jack_nframes_t nframes, void *arg)
{
	struct buffer *buf = arg;
	jack_default_audio_sample_t *in, *out;
	in = jack_port_get_buffer(jack_state.in_port, nframes);
	out = jack_port_get_buffer(jack_state.out_port, nframes);
	double in_double[nframes];
	for (int i = 0; i < nframes; i++)
		in_double[i] = in[i];

	buffer_push(buf, in_double, nframes);

	for (int i = 0; i < nframes; i++)
		out[i] = 0;

	return 0;
}

void draw(const struct analysis_data *, const struct analysis_params *);
void draw_plot(size_t n, const struct point *data, double from, double to);

bool process(struct buffer *buf)
{
	const double min_freq = 20;
	const double max_freq = jack_state.sample_rate / 2;
	struct analysis_params params = {0};
	static struct viewport_t {
		double A;
		double B;
	} viewport = {NAN, NAN}, limits = {NAN, NAN};
	if (isnan(viewport.A)) {
		limits.A = viewport.A = log(min_freq);
		limits.B = viewport.B = log(max_freq);
	}

	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT)
			return false;

		if (ev.type == SDL_WINDOWEVENT) {
			if (ev.window.event == SDL_WINDOWEVENT_RESIZED || ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				sdl_state.w = ev.window.data1, sdl_state.h = ev.window.data2;
		}

		const int blacklist = KMOD_MODE | KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI;
		const int modstate = SDL_GetModState(); // FIXME: Hack
		double center = (viewport.A + viewport.B) / 2;
		double width = viewport.B - viewport.A;
		if (ev.type == SDL_KEYDOWN && (modstate & blacklist) == 0) {
			static const double zoom = 2;
			static const double shift = 1/4.;
			SDL_Scancode key = ev.key.keysym.scancode;
			switch (key) {
			case SDL_SCANCODE_J:
				width /= zoom;
				break;
			case SDL_SCANCODE_K:
				width *= zoom;
				break;
			case SDL_SCANCODE_H:
				center -= width * shift;
				break;
			case SDL_SCANCODE_L:
				center += width * shift;
				break;
			default:
				break;
			}
		}

		if (ev.type == SDL_MOUSEWHEEL) {
			static const double xf = 1/32.;
			static const double yf = 1/8.;
			double x = -ev.wheel.x * xf;
			double y = -ev.wheel.y * yf;
			center += width * x;
			width *= exp(y);
		}

		width = fmin(width, limits.B - limits.A);
		center = fmax(fmin(center, limits.B - width / 2), limits.A + width / 2);
		viewport.A = center - width / 2;
		viewport.B = center + width / 2;
	}

	params.min_freq = exp(viewport.A);
	params.max_freq = exp(viewport.B);

	struct analysis_data data;
	if (!analyse(&data, buf, &params))
		return true;

	printf("%lf %s %lf\n", data.guessed_frequency, tone_name(data.guessed_tone), data.guessed_tone.cents);
	draw(&data, &params);

	analysis_free(data);
	return true;
}

void plot_interval(double x0, double x1, double y) {
	x0 = fmin(1, fmax(0, x0));
	x1 = fmin(1, fmax(0, x1));
	if ((x0 <= 0 && x1 <= 0) || (x0 >= 1 && x1 >= 1))
		return;

	SDL_Rect rect;
	rect.h = round(sdl_state.h * y);
	rect.y = sdl_state.h - rect.h;
	rect.x = round(sdl_state.w * x0);
	rect.w = round(sdl_state.w * x1) - rect.x;
	rect.w = (rect.w <= 0) ? 1 : rect.w;
	SDL_RenderFillRect(sdl_state.ren, &rect);
}

void draw(const struct analysis_data *data, const struct analysis_params *params) {
	SDL_Rect rect = {.h = sdl_state.h, .w = sdl_state.w, .x = 0, .y = 0};
	SDL_SetRenderDrawColor(sdl_state.ren, 0, 0, 0, 255);
	SDL_RenderClear(sdl_state.ren);

	SDL_SetRenderDrawColor(sdl_state.ren, 255, 255, 255, 255);
	draw_plot(data->plot_size, data->plot, params->min_freq, params->max_freq);
	SDL_SetRenderDrawColor(sdl_state.ren, 255, 0, 0, 255);
	double guessed = logscale(data->guessed_frequency, params->min_freq, params->max_freq);
	plot_interval(guessed, guessed, 1);
	SDL_RenderPresent(sdl_state.ren);
}

void draw_plot(size_t n, const struct point *data, double A, double B)
{
	for (int i = 0; i < n; i++) {
		double x0 = (i) ? data[i - 1].x : 0;
		double x1 = data[i].x;
		double y = sqrt(data[i].y);
		plot_interval(logscale(x0, A, B), logscale(x1, A, B), y);
	}
}

int main(void)
{
	struct buffer buf = {.s = 0, .e = 0, .size = BUFSIZE};
	buf.data = malloc(buf.size * sizeof(*buf.data));

	sdl_init();
	jack_init_client();
	jack_setup(record, &buf);
	jack_connect_ports();
	while (process(&buf))
		;
}
