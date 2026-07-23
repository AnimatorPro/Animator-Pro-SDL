#include <poco/poco.h>

#include <SDL3/SDL.h>

#include "sdl_bindings.h"

#include <stdio.h>

#ifndef POCO_SNAKE_SCRIPT
#define POCO_SNAKE_SCRIPT "snake.poc"
#endif

enum {
	SNAKE_LOGICAL_WIDTH = 320,
	SNAKE_LOGICAL_HEIGHT = 240,
	SNAKE_WINDOW_HEIGHT = 900,
	SNAKE_WINDOW_WIDTH = 1200,
	SNAKE_FRAME_MILLISECONDS = 16
};

static void report_diagnostic(void* user_data, const PocoDiagnostic* diagnostic)
{
	const char* source_name;
	const char* message;

	(void)user_data;
	if (diagnostic == NULL) {
		return;
	}
	source_name = diagnostic->source_name != NULL ? diagnostic->source_name : "<unknown>";
	message = diagnostic->message != NULL ? diagnostic->message : "Poco error";
	fprintf(stderr, "%s:%ld:%d: %s\n", source_name, diagnostic->line, diagnostic->column, message);
}

static void report_poco_failure(PocoVm* vm, const char* operation, PocoStatus status)
{
	const char* detail = vm != NULL ? poco_get_last_error(vm) : NULL;

	if (detail != NULL && detail[0] != '\0') {
		fprintf(stderr, "snake: %s failed (%d): %s\n", operation, (int)status, detail);
	} else {
		fprintf(stderr, "snake: %s failed (%d)\n", operation, (int)status);
	}
}

static PocoStatus invoke(PocoActivation* activation, const char* function,
						 PocoCallbackValue* out_result)
{
	PocoCall* call = NULL;
	PocoStatus status;

	status = poco_call_begin(activation, function, &call);
	if (status == POCO_STATUS_OK) {
		status = poco_call_invoke(call, out_result);
	}
	poco_call_end(call);
	return status;
}

static PocoStatus invoke_void(PocoActivation* activation, const char* function)
{
	PocoCallbackValue result;
	PocoStatus status;

	status = invoke(activation, function, &result);
	if (status == POCO_STATUS_OK && result.kind != POCO_CALLBACK_VALUE_INVALID) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	return status;
}

static PocoStatus invoke_update(PocoActivation* activation, int* quit_requested)
{
	PocoCallbackValue result;
	PocoStatus status;

	status = invoke(activation, "update", &result);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	if (result.kind != POCO_CALLBACK_VALUE_INT) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	*quit_requested = result.value.int_value != 0;
	return POCO_STATUS_OK;
}

static int create_window(SDL_Window** out_window, SDL_Renderer** out_renderer)
{
	SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;

	/* Register as a regular foreground GUI app so the window can take focus
	 * over the launching terminal on macOS. Must be set before SDL_Init. */
	SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "snake: SDL initialization failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 0;
	}
	if (!SDL_CreateWindowAndRenderer("Poco Snake", SNAKE_WINDOW_WIDTH, SNAKE_WINDOW_HEIGHT, flags,
									 out_window, out_renderer)) {
		fprintf(stderr, "snake: window creation failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 0;
	}
	if (!SDL_SetRenderLogicalPresentation(*out_renderer, SNAKE_LOGICAL_WIDTH, SNAKE_LOGICAL_HEIGHT,
										  SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
		fprintf(stderr, "snake: logical presentation setup failed: %s\n", SDL_GetError());
		goto FAILURE;
	}
	if (!SDL_SetDefaultTextureScaleMode(*out_renderer, SDL_SCALEMODE_PIXELART)) {
		fprintf(stderr, "snake: pixel-art scaling setup failed: %s\n", SDL_GetError());
		goto FAILURE;
	}
	/* Bring the window to the front so it opens on top of the terminal. */
	SDL_RaiseWindow(*out_window);
	return 1;

FAILURE:
	SDL_DestroyRenderer(*out_renderer);
	SDL_DestroyWindow(*out_window);
	*out_renderer = NULL;
	*out_window = NULL;
	SDL_Quit();
	return 0;
}

static void pace_frame(Uint64 frame_started)
{
	Uint64 elapsed = SDL_GetTicks() - frame_started;

	if (elapsed < SNAKE_FRAME_MILLISECONDS) {
		SDL_Delay((Uint32)(SNAKE_FRAME_MILLISECONDS - elapsed));
	}
}

int main(int argc, char** argv)
{
	const char* script_path;
	char script_buf[1024];
	PocoVmOptions options = {0};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	PocoLibrary sdl_library;
	SDL_Window* window = NULL;
	SDL_Renderer* renderer = NULL;
	PocoStatus status = POCO_STATUS_OK;
	int lifecycle_started = 0;
	int exit_code = 1;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [script.poc]\n", argv[0]);
		return 2;
	}
	if (argc == 2) {
		script_path = argv[1];
	} else {
		/* Default to snake.poc sitting next to the executable, so the example
		   runs from any working directory. */
		const char* base = SDL_GetBasePath();

		if (base != NULL) {
			SDL_snprintf(script_buf, sizeof(script_buf), "%s%s", base, POCO_SNAKE_SCRIPT);
			script_path = script_buf;
		} else {
			script_path = POCO_SNAKE_SCRIPT;
		}
	}

	/* Confirm the script is readable before opening a window: on macOS,
	   tearing down a just-created window before the event loop has pumped
	   hangs, so a missing script must fail here rather than after create_window. */
	{
		FILE* script_probe = fopen(script_path, "rb");

		if (script_probe == NULL) {
			fprintf(stderr, "snake: cannot open script '%s'\n", script_path);
			return 1;
		}
		fclose(script_probe);
	}

	if (!create_window(&window, &renderer)) {
		goto CLEANUP;
	}

	sdl_library = snake_sdl_library(renderer);
	options.diagnostic_callback = report_diagnostic;
	status = poco_vm_create(&options, &vm);
	if (status != POCO_STATUS_OK) {
		report_poco_failure(vm, "VM creation", status);
		goto CLEANUP;
	}
	status = poco_vm_register_standard_library(vm);
	if (status != POCO_STATUS_OK) {
		report_poco_failure(vm, "standard library registration", status);
		goto CLEANUP;
	}
	status = poco_vm_register_library(vm, &sdl_library);
	if (status != POCO_STATUS_OK) {
		report_poco_failure(vm, "SDL library registration", status);
		goto CLEANUP;
	}
	status = poco_vm_compile_file(vm, script_path, &program);
	if (status != POCO_STATUS_OK) {
		report_poco_failure(vm, "script compilation", status);
		goto CLEANUP;
	}
	status = poco_activation_acquire(program, &activation);
	if (status != POCO_STATUS_OK) {
		report_poco_failure(vm, "activation acquisition", status);
		goto CLEANUP;
	}
	status = poco_activation_init(activation);
	if (status != POCO_STATUS_OK) {
		report_poco_failure(vm, "global initialization", status);
		goto CLEANUP;
	}
	status = invoke_void(activation, "init");
	if (status != POCO_STATUS_OK) {
		report_poco_failure(vm, "init()", status);
		goto CLEANUP;
	}
	lifecycle_started = 1;
	status = invoke_void(activation, "load_assets");
	if (status != POCO_STATUS_OK) {
		report_poco_failure(vm, "load_assets()", status);
		goto CLEANUP;
	}

	for (;;) {
		Uint64 frame_started = SDL_GetTicks();
		int quit_requested = 0;

		status = invoke_update(activation, &quit_requested);
		if (status != POCO_STATUS_OK) {
			report_poco_failure(vm, "update()", status);
			goto CLEANUP;
		}
		if (quit_requested) {
			break;
		}
		status = invoke_void(activation, "draw");
		if (status != POCO_STATUS_OK) {
			report_poco_failure(vm, "draw()", status);
			goto CLEANUP;
		}
		pace_frame(frame_started);
	}

	exit_code = 0;

CLEANUP:
	if (lifecycle_started) {
		PocoStatus shutdown_status = invoke_void(activation, "shutdown");

		if (shutdown_status != POCO_STATUS_OK) {
			report_poco_failure(vm, "shutdown()", shutdown_status);
			exit_code = 1;
		}
	}
	poco_activation_release(activation);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return exit_code;
}
