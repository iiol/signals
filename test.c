#include <stdlib.h>
#include <math.h>

#define TITLE "test"

// Count of samples
#define CS 4096
// Quantization value
#define MAX_QV 10000

enum sig_types {
	NOISE,
	SIN,
	COS,
};

// Добавить регулировку амплитуды
// Считать и показывать свёртку
static enum sig_types sigtype = SIN;
static int buf[CS];
static int graph[MAX_QV];
//static int conv[MAX_CONV];
static float rand_count = 1;
static float sin_step = 0.001;
static float cos_step = 0.001;

static float sig_av = 0;
static float x_offst;

static int stop;

static void
test_setbuf(void)
{
	int i, j;
	double di;

	if (stop)
		return;

	switch (sigtype) {
	case SIN:
		for (i = di = 0; i < CS; ++i, di += sin_step)
			buf[i] = sin(di) * 5000 + 5000 + sig_av;
		break;

	case COS:
		for (i = di = 0; i < CS; ++i, di += cos_step)
			buf[i] = cos(di) * 5000 + 5000 + sig_av;
		break;

	case NOISE:
		for (i = 0; i < CS; ++i)
			for (j = buf[i] = 0; j < rand_count; ++j)
				buf[i] += rand() % MAX_QV / rand_count;
		break;
	}

	for (i = 0; i < CS; ++i) {
		if (buf[i] < 0)
			buf[i] = 0;
		else if (buf[i] >= MAX_QV)
			buf[i] = MAX_QV - 1;
	}
}

static void
test(struct nk_context *ctx)
{
	int i;
	char str[128];
	static nk_flags window_flags = 0;

	test_setbuf();
	window_flags |= NK_WINDOW_TITLE;
	window_flags |= NK_WINDOW_BORDER;
	window_flags |= NK_WINDOW_MOVABLE;
	window_flags |= NK_WINDOW_SCALABLE;

	if (nk_begin(ctx, "signal", nk_rect(0, 0, 800, 600), window_flags)) {
		nk_layout_row_dynamic(ctx, 400, 1);
		if (nk_chart_begin_colored(ctx, NK_CHART_LINES_NO_RECT, nk_rgb(255,0,0), nk_rgb(150,0,0), CS, 0.0f, MAX_QV)) {
			for (i = x_offst; i < CS + x_offst; ++i) {
				if (i < 0 || i > CS - 1)
					nk_chart_push_slot(ctx, MAX_QV/2, 0);
				else
					nk_chart_push_slot(ctx, (float)buf[i], 0);
			}
		}
		nk_chart_end(ctx);

		nk_layout_row_static(ctx, 30, 300, 1);
		nk_slider_float(ctx, -CS/2, &x_offst, CS/2, CS/30);
	}
	nk_end(ctx);

	if (nk_begin(ctx, "graph", nk_rect(800, 0, 400, 300), window_flags)) {
		memset(graph, 0, MAX_QV * sizeof (int));

		nk_layout_row_dynamic(ctx, 200, 1);
		if (nk_chart_begin_colored(ctx, NK_CHART_LINES_NO_RECT, nk_rgb(255,0,0), nk_rgb(150,0,0), MAX_QV, 0.0f, CS/100))
			for (i = 0; i < CS; ++i)
				++graph[buf[i]];
		for (i = 0; i < MAX_QV; ++i) {
			if (graph[i] < 0)
				graph[i] = 0;
			else if (graph[i] >= CS/100)
				graph[i] = CS/100;
		}
		for (i = MAX_QV - 1; i >= 0; --i)
			nk_chart_push_slot(ctx, (float)graph[i], 0);
		nk_chart_end(ctx);
	}
	nk_end(ctx);

	if (nk_begin(ctx, "menu", nk_rect(800, 300, 400, 300), window_flags)) {
		nk_layout_row_dynamic(ctx, 30, 1);
		nk_label(ctx, "Generate:", NK_TEXT_LEFT);
		nk_layout_row_dynamic(ctx, 30, 2);
		if (nk_option_label(ctx, "sin", sigtype == SIN))
			sigtype = SIN;
		if (nk_option_label(ctx, "noise", sigtype == NOISE))
			sigtype = NOISE;
		if (nk_option_label(ctx, "cos", sigtype == COS))
			sigtype = COS;

		nk_layout_row_dynamic(ctx, 30, 1);
		nk_checkbox_label(ctx, "Stop", &stop);
	}
	nk_end(ctx);

	if (nk_begin(ctx, "settings", nk_rect(800, 300, 400, 300), window_flags)) {
		if (sigtype == NOISE) {
			nk_layout_row_begin(ctx, NK_STATIC, 30, 3);

			nk_layout_row_push(ctx, 100);
			nk_label(ctx, "Rand count:", NK_TEXT_LEFT);
			nk_layout_row_push(ctx, 110);
			nk_slider_float(ctx, 1, &rand_count, 20, 1);
			sprintf(str, "%.0f", rand_count);
			nk_layout_row_push(ctx, 110);
			nk_label(ctx, str, NK_TEXT_LEFT);

			nk_layout_row_end(ctx);
		}
		else if (sigtype == SIN || sigtype == COS) {
			float *step;

			step = (sigtype == SIN) ? &sin_step : &cos_step;
			nk_layout_row_begin(ctx, NK_STATIC, 30, 3);

			nk_layout_row_push(ctx, 50);
			nk_label(ctx, "Step:", NK_TEXT_LEFT);
			nk_layout_row_push(ctx, 110);
			nk_slider_float(ctx, 0.001, step, 0.06, 0.001);
			sprintf(str, "%.3f", *step);
			nk_layout_row_push(ctx, 110);
			nk_label(ctx, str, NK_TEXT_LEFT);
			nk_layout_row_end(ctx);

			nk_layout_row_push(ctx, 50);
			nk_label(ctx, "Av:", NK_TEXT_LEFT);
			nk_layout_row_push(ctx, 110);
			nk_slider_float(ctx, -MAX_QV, &sig_av, MAX_QV, MAX_QV/20);
			sprintf(str, "%.0f", sig_av);
			nk_layout_row_push(ctx, 110);
			nk_label(ctx, str, NK_TEXT_LEFT);

			nk_layout_row_end(ctx);
		}
	}
	nk_end(ctx);
}
