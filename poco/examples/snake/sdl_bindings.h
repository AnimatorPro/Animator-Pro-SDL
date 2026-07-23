#ifndef POCO_EXAMPLES_SNAKE_SDL_BINDINGS_H
#define POCO_EXAMPLES_SNAKE_SDL_BINDINGS_H

#include <poco/poco.h>

#include <SDL3/SDL.h>

typedef struct SnakeEvent {
	int type;
	int code;
} SnakeEvent;

enum SnakeEventType {
	SNAKE_EVENT_NONE = 0,
	SNAKE_EVENT_KEYDOWN = 1,
	SNAKE_EVENT_KEYUP = 2,
	SNAKE_EVENT_QUIT = 3
};

PocoLibrary snake_sdl_library(SDL_Renderer* renderer);

#endif
