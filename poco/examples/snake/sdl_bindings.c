#include "sdl_bindings.h"

#include "font8x8.h"

#include <stdint.h>

typedef struct SnakeSdlContext {
	SDL_Renderer* renderer;
	uint32_t random_state;
} SnakeSdlContext;

enum {
	SNAKE_FONT_COLUMNS = 5,
	SNAKE_FONT_ROWS = 7,
	SNAKE_FONT_ADVANCE = 6,
	SNAKE_FONT_LINE_ADVANCE = 8
};

static SnakeSdlContext snake_context;
static SnakeSdlContext* active_context;

static Uint8 color_component(int component)
{
	if (component < 0) {
		return 0;
	}
	if (component > 255) {
		return 255;
	}
	return (Uint8)component;
}

static SDL_Renderer* snake_renderer(void)
{
	return active_context != NULL ? active_context->renderer : NULL;
}

static void snake_clear(int red, int green, int blue)
{
	SDL_Renderer* renderer = snake_renderer();

	if (renderer == NULL) {
		return;
	}
	SDL_SetRenderDrawColor(renderer, color_component(red), color_component(green),
						   color_component(blue), 255);
	SDL_RenderClear(renderer);
}

static void snake_fill_rect(int x, int y, int width, int height, int red, int green, int blue)
{
	SDL_Renderer* renderer = snake_renderer();
	SDL_FRect rectangle;

	if (renderer == NULL || width <= 0 || height <= 0) {
		return;
	}
	rectangle.x = (float)x;
	rectangle.y = (float)y;
	rectangle.w = (float)width;
	rectangle.h = (float)height;
	SDL_SetRenderDrawColor(renderer, color_component(red), color_component(green),
						   color_component(blue), 255);
	SDL_RenderFillRect(renderer, &rectangle);
}

static unsigned int font_character(unsigned char character)
{
	if (character >= 'a' && character <= 'z') {
		character = (unsigned char)(character - 'a' + 'A');
	}
	if (character >= 128 ||
		(character != ' ' && snake_font8x8[character][0] == 0 && snake_font8x8[character][1] == 0 &&
		 snake_font8x8[character][2] == 0 && snake_font8x8[character][3] == 0 &&
		 snake_font8x8[character][4] == 0 && snake_font8x8[character][5] == 0 &&
		 snake_font8x8[character][6] == 0)) {
		return '?';
	}
	return character;
}

static void snake_draw_text(int x, int y, char* text)
{
	SDL_Renderer* renderer = snake_renderer();
	int origin_x = x;

	if (renderer == NULL || text == NULL) {
		return;
	}
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	while (*text != '\0') {
		unsigned int character = (unsigned char)*text++;
		int row;

		if (character == '\n') {
			x = origin_x;
			y += SNAKE_FONT_LINE_ADVANCE;
			continue;
		}
		character = font_character((unsigned char)character);
		for (row = 0; row < SNAKE_FONT_ROWS; ++row) {
			int column;

			for (column = 0; column < SNAKE_FONT_COLUMNS; ++column) {
				if ((snake_font8x8[character][row] & (1u << (SNAKE_FONT_COLUMNS - column - 1))) !=
					0) {
					SDL_FRect pixel = {(float)(x + column), (float)(y + row), 1.0f, 1.0f};

					SDL_RenderFillRect(renderer, &pixel);
				}
			}
		}
		x += SNAKE_FONT_ADVANCE;
	}
}

static void snake_present(void)
{
	SDL_Renderer* renderer = snake_renderer();

	if (renderer != NULL) {
		SDL_RenderPresent(renderer);
	}
}

static SnakeEvent snake_poll_event(void)
{
	SDL_Event event;
	SnakeEvent result = {SNAKE_EVENT_NONE, 0};

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_KEY_DOWN:
				result.type = SNAKE_EVENT_KEYDOWN;
				result.code = (int)event.key.key;
				return result;
			case SDL_EVENT_KEY_UP:
				result.type = SNAKE_EVENT_KEYUP;
				result.code = (int)event.key.key;
				return result;
			case SDL_EVENT_QUIT:
				result.type = SNAKE_EVENT_QUIT;
				return result;
			default:
				break;
		}
	}
	return result;
}

static long snake_ticks_ms(void)
{
	return (long)SDL_GetTicks();
}

static int snake_random(int upper_bound)
{
	uint32_t value;

	if (active_context == NULL || upper_bound <= 0) {
		return 0;
	}
	value = active_context->random_state;
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	active_context->random_state = value;
	return (int)(value % (uint32_t)upper_bound);
}

static PocoStatus snake_sdl_initialize(PocoLibrary* library)
{
	SnakeSdlContext* context = library != NULL ? library->user_data : NULL;
	uint32_t seed;

	if (context == NULL || context->renderer == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	seed = (uint32_t)SDL_GetTicks() ^ (uint32_t)(uintptr_t)context->renderer ^ 0x9e3779b9u;
	context->random_state = seed != 0 ? seed : 0x6d2b79f5u;
	active_context = context;
	return POCO_STATUS_OK;
}

static void snake_sdl_cleanup(PocoLibrary* library)
{
	if (library != NULL && active_context == library->user_data) {
		active_context = NULL;
	}
}

static const PocoBindingPointerContract draw_text_pointer_contracts[] = {
	{
		.parameter_index = 2,
		.permissions = POCO_POINTER_PERMISSION_READ,
		.pointer_depth = 1,
		.span_kind = POCO_BINDING_SPAN_C_STRING,
		.byte_count_parameter = POCO_BINDING_PARAMETER_NONE,
		.element_count_parameter = POCO_BINDING_PARAMETER_NONE,
		.string_parameter = POCO_BINDING_PARAMETER_NONE,
	},
};

static const PocoBindingContract draw_text_contract = {
	.pointer_contracts = draw_text_pointer_contracts,
	.pointer_contract_count =
		sizeof(draw_text_pointer_contracts) / sizeof(draw_text_pointer_contracts[0]),
};

static const PocoBinding snake_sdl_bindings[] = {
	{
		.prototype = "void clear(int red, int green, int blue);",
		.function = (PocoNativeFunction)snake_clear,
	},
	{
		.prototype =
			"void fill_rect(int x, int y, int width, int height, int red, int green, int blue);",
		.function = (PocoNativeFunction)snake_fill_rect,
	},
	{
		.prototype = "void draw_text(int x, int y, char *text);",
		.function = (PocoNativeFunction)snake_draw_text,
		.contract = &draw_text_contract,
	},
	{
		.prototype = "void present();",
		.function = (PocoNativeFunction)snake_present,
	},
	{
		.prototype = "struct SnakeEvent { int type; int code; } poll_event();",
		.function = (PocoNativeFunction)snake_poll_event,
	},
	{
		.prototype = "long ticks_ms();",
		.function = (PocoNativeFunction)snake_ticks_ms,
	},
	{
		.prototype = "int random(int upper_bound);",
		.function = (PocoNativeFunction)snake_random,
	},
};

PocoLibrary snake_sdl_library(SDL_Renderer* renderer)
{
	PocoLibrary library = {
		"snake-sdl",
		snake_sdl_bindings,
		sizeof(snake_sdl_bindings) / sizeof(snake_sdl_bindings[0]),
		snake_sdl_initialize,
		snake_sdl_cleanup,
		&snake_context,
	};

	snake_context.renderer = renderer;
	return library;
}
