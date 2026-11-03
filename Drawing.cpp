#include <RetroEngine.hpp>

#if RETRO_PLATFORM == RETRO_PS4
#ifdef GL_GLEXT_PROTOTYPES
#undef GL_GLEXT_PROTOTYPES
#endif
#define EGL_EGLEXT_PROTOTYPES 1

#define GL_GLES_PROTOTYPES 1
#define GL_GLEXT_PROTOTYPES 1
#include <orbis/Pigletv2VSH.h>
#include <orbis/libkernel.h>
#include "util.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/ShellUIUtil.h>
#include <orbis/Pigletv2VSH.h>

#define RENDER_WIDTH 1920
#define RENDER_HEIGHT 1080
#define PS4_OUTPUT_WIDTH RENDER_WIDTH
#define PS4_OUTPUT_HEIGHT RENDER_HEIGHT

#define PIGLET_MODULE_NAME "libScePigletv2VSH.sprx"
#define SHCOMP_MODULE_NAME "libSceShaccVSH.sprx"

#if 1
#	define MODULE_PATH_PREFIX "/app0/sce_module"
#else
#	define MODULE_PATH_PREFIX "/data/self/system/common/lib"
#endif

static int32_t s_piglet_module = -1;
static int32_t s_shcomp_module = -1;
static bool s_egl_initialized = false;
static bool s_system_service_loaded = false;

static EGLDisplay s_display = EGL_NO_DISPLAY;
static EGLSurface s_surface = EGL_NO_SURFACE;
static EGLContext s_context = EGL_NO_CONTEXT;

static void destroy_context(void);

static GLuint s_texture_id = 0;
static GLuint s_program_id = 0;

static GLint s_xyz_loc;
static GLint s_uv_loc;
static GLint s_sampler_loc;

static const GLfloat s_obj_vertices[] = {
	-0.5f, 0.5f, 0.0f,  /* XYZ #0 */
	 0.0f, 0.0f,        /* UV  #0 */
	-0.5f, -0.5f, 0.0f, /* XYZ #1 */
	 0.0f,  1.0f,       /* UV  #1 */
	 0.5f, -0.5f, 0.0f, /* XYZ #2 */
	 1.0f,  1.0f,       /* UV  #2 */
	 0.5f,  0.5f, 0.0f, /* XYZ #3 */
	 1.0f,  0.0f        /* UV  #3 */
};
static const GLushort s_obj_indices[] = {
	0, 1, 2, 0, 2, 3,
};

static const GLubyte s_texture_data[4 * 3] = {
	255, 0, 0,   /* red */
	0, 255, 0,   /* green */
	0, 0, 255,   /* blue */
	255, 255, 0, /* yellow */
};

static const GLchar s_vertex_shader_code[] =
	"attribute vec4 a_xyz;\n"
	"attribute vec2 a_uv;\n"
	"varying vec2 v_uv;\n"
	"\n"
	"void main() {\n"
	"	gl_Position = a_xyz;\n"
	"	v_uv = a_uv;\n"
	"}\n";

static const GLchar s_fragment_shader_code[] =
	"precision mediump float;\n"
	"varying vec2 v_uv;\n"
	"uniform sampler2D s_texture;\n"
	"\n"
	"void main() {\n"
	"	gl_FragColor = texture2D(s_texture, v_uv);\n"
	"}\n";

const char* g_sandbox_word = NULL;

static bool load_modules(void) {
	int ret;

	ret = sceKernelLoadStartModule(MODULE_PATH_PREFIX "/" PIGLET_MODULE_NAME, 0, NULL, 0, NULL, NULL);
	if (ret < 0) {
		EPRINTF("sceKernelLoadStartModule(%s) failed: 0x%08X\n", PIGLET_MODULE_NAME, ret);
		goto err;
	}
	s_piglet_module = ret;

	ret = sceKernelLoadStartModule(MODULE_PATH_PREFIX "/" SHCOMP_MODULE_NAME, 0, NULL, 0, NULL, NULL);
	if (ret < 0) {
		EPRINTF("sceKernelLoadStartModule(%s) failed: 0x%08X\n", SHCOMP_MODULE_NAME, ret);
        goto err_unload_piglet;
	}
	s_shcomp_module = ret;

	return true;

err_unload_piglet:
	ret = sceKernelStopUnloadModule(s_piglet_module, 0, NULL, 0, NULL, NULL);
	if (ret < 0) {
		EPRINTF("sceKernelStopUnloadModule(%s) failed: 0x%08X\n", PIGLET_MODULE_NAME, ret);
	} else {
		s_piglet_module = -1;
	}

err:
	return false;
}

static void unload_modules(void) {
	int ret;

    if (s_shcomp_module >= 0) {
		ret = sceKernelStopUnloadModule(s_shcomp_module, 0, NULL, 0, NULL, NULL);
		if (ret < 0) {
			EPRINTF("sceKernelStopUnloadModule(%s) failed: 0x%08X\n", SHCOMP_MODULE_NAME, ret);
		} else {
			s_shcomp_module = -1;
		}
	}

    if (s_piglet_module >= 0) {
		ret = sceKernelStopUnloadModule(s_piglet_module, 0, NULL, 0, NULL, NULL);
		if (ret < 0) {
			EPRINTF("sceKernelStopUnloadModule(%s) failed: 0x%08X\n", PIGLET_MODULE_NAME, ret);
		} else {
			s_piglet_module = -1;
		}
	}
}

/* XXX: patches below are given for Piglet module from 4.74 Devkit PUP */
static void pgl_patches_cb(void* arg, uint8_t* base, uint64_t size) {
	/* Patch runtime compiler check */
	const uint8_t p_set_eax_to_1[] = {
		0x31, 0xC0, 0xFF, 0xC0, 0x90,
	};
	memcpy(base + 0x5451F, p_set_eax_to_1, sizeof(p_set_eax_to_1));

	/* Tell that runtime compiler exists */
	*(uint8_t*)(base + 0xB2DEC) = 0;
	*(uint8_t*)(base + 0xB2DED) = 0;
	*(uint8_t*)(base + 0xB2DEE) = 1;
	*(uint8_t*)(base + 0xB2E21) = 1;

	/* Inform Piglet that we have shader compiler module loaded */
	*(int32_t*)(base + 0xB2E24) = s_shcomp_module;
}

static bool do_patches(void) {
    if (!patch_module(s_piglet_module, &pgl_patches_cb, NULL)) {
		EPRINTF("Unable to patch PGL module.\n");
		goto err;
	}

	return true;

err:
	return false;
}

static void cleanup(void) {
	int ret;

    destroy_context();
    unload_modules();

    if (s_system_service_loaded) {
        ret = sceSysmoduleUnloadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
        if (ret) {
            EPRINTF("sceSysmoduleUnloadModuleInternal(%s) failed: 0x%08X\n", "ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE", ret);
        }
        s_system_service_loaded = false;
	}
}

static bool create_context(unsigned int width, unsigned int height) {
	OrbisPglConfig pgl_config;
	OrbisPglWindow render_window = { 0, width, height };
	EGLConfig config = NULL;
	EGLint num_configs;

	EGLint attribs[] = {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 0,
		EGL_STENCIL_SIZE, 0,
		EGL_SAMPLE_BUFFERS, 0,
		EGL_SAMPLES, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE,
	};

	EGLint ctx_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};

	EGLint window_attribs[] = {
		EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
		EGL_NONE,
	};

	int major, minor;
	int ret;

	memset(&pgl_config, 0, sizeof(pgl_config));
	{
		pgl_config.size = sizeof(pgl_config);
        pgl_config.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT | ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60;
		pgl_config.processOrder = 1;
		pgl_config.systemSharedMemorySize = 0x200000;
		pgl_config.videoSharedMemorySize = 0x2400000;
		pgl_config.maxMappedFlexibleMemory = 0xAA00000;
		pgl_config.drawCommandBufferSize = 0xC0000;
		pgl_config.lcueResourceBufferSize = 0x10000;
		pgl_config.dbgPosCmd_0x40 = RENDER_WIDTH;
		pgl_config.dbgPosCmd_0x44 = RENDER_HEIGHT;
		pgl_config.dbgPosCmd_0x48 = 0;
		pgl_config.dbgPosCmd_0x4C = 0;
		pgl_config.unk_0x5C = 2;
	}

	if (!scePigletSetConfigurationVSH(&pgl_config)) {
		EPRINTF("scePigletSetConfigurationVSH failed.\n");
		goto err;
	}

	s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (s_display == EGL_NO_DISPLAY) {
		EPRINTF("eglGetDisplay failed.\n");
		goto err;
	}

	if (!eglInitialize(s_display, &major, &minor)) {
		ret = eglGetError();
		EPRINTF("eglInitialize failed: 0x%08X\n", ret);
		goto err;
	}
    s_egl_initialized = true;
	printf("EGL version major:%d, minor:%d\n", major, minor);

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		ret = eglGetError();
		EPRINTF("eglBindAPI failed: 0x%08X\n", ret);
		goto err;
	}

	if (!eglSwapInterval(s_display, 0)) {
		ret = eglGetError();
		EPRINTF("eglSwapInterval failed: 0x%08X\n", ret);
		goto err;
	}

	if (!eglChooseConfig(s_display, attribs, &config, 1, &num_configs)) {
		ret = eglGetError();
		EPRINTF("eglChooseConfig failed: 0x%08X\n", ret);
		goto err;
	}
	if (num_configs != 1) {
		EPRINTF("No available configuration found.\n");
		goto err;
	}

	s_surface = eglCreateWindowSurface(s_display, config, &render_window, window_attribs);
	if (s_surface == EGL_NO_SURFACE) {
		ret = eglGetError();
		EPRINTF("eglCreateWindowSurface failed: 0x%08X\n", ret);
		goto err;
	}

	s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, ctx_attribs);
	if (s_context == EGL_NO_CONTEXT) {
		ret = eglGetError();
		EPRINTF("eglCreateContext failed: 0x%08X\n", ret);
		goto err;
	}

	if (!eglMakeCurrent(s_display, s_surface, s_surface, s_context)) {
		ret = eglGetError();
		EPRINTF("eglMakeCurrent failed: 0x%08X\n", ret);
		goto err;
	}

	printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

	return true;

err:
    destroy_context();
	return false;
}

static void destroy_context(void) {
    if (s_display != EGL_NO_DISPLAY) {
        if (s_context != EGL_NO_CONTEXT || s_surface != EGL_NO_SURFACE) {
            if (!eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
                EPRINTF("eglMakeCurrent during shutdown failed: 0x%08X\n", eglGetError());
            }
        }

        if (s_surface != EGL_NO_SURFACE && !eglDestroySurface(s_display, s_surface)) {
            EPRINTF("eglDestroySurface failed: 0x%08X\n", eglGetError());
        }
        s_surface = EGL_NO_SURFACE;

        if (s_context != EGL_NO_CONTEXT && !eglDestroyContext(s_display, s_context)) {
            EPRINTF("eglDestroyContext failed: 0x%08X\n", eglGetError());
        }
        s_context = EGL_NO_CONTEXT;

        if (s_egl_initialized && !eglTerminate(s_display)) {
            EPRINTF("eglTerminate failed: 0x%08X\n", eglGetError());
        }
        s_egl_initialized = false;
	}
	s_display = EGL_NO_DISPLAY;
}

static bool create_texture(void) {
	int ret;

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	ret = glGetError();
	if (ret) {
		EPRINTF("glPixelStorei failed: 0x%08X\n", ret);
		goto err;
	}

	glGenTextures(1, &s_texture_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glGenTextures failed: 0x%08X\n", ret);
		goto err;
	}

	glBindTexture(GL_TEXTURE_2D, s_texture_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glBindTexture failed: 0x%08X\n", ret);
		goto err;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, s_texture_data);
	ret = glGetError();
	if (ret) {
		EPRINTF("glTexImage2D failed: 0x%08X\n", ret);
		goto err;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	ret = glGetError();
	if (ret) {
		EPRINTF("glTexParameteri failed: 0x%08X\n", ret);
		goto err;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	ret = glGetError();
	if (ret) {
		EPRINTF("glTexParameteri failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

static bool destroy_texture(void) {
	int ret;

	glDeleteTextures(1, &s_texture_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glDeleteTextures failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

static bool compile_shader(GLenum type, const char* source, size_t length, GLuint* pShader) {
	GLuint shader;
	GLint tmp_length = (int)length;
	GLint success;
	char log_buf[256];
	int ret;

	shader = glCreateShader(type);
	if (!shader) {
		ret = glGetError();
		EPRINTF("glCreateShader failed: 0x%08X\n", ret);
		goto err;
	}

	glShaderSource(shader, 1, &source, &tmp_length);
	ret = glGetError();
	if (ret) {
		EPRINTF("glShaderSource failed: 0x%08X\n", ret);
		goto err;
	}

	glCompileShader(shader);
	ret = glGetError();
	if (ret) {
		EPRINTF("glCompileShader failed: 0x%08X\n", ret);
		goto err;
	}

	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	ret = glGetError();
	if (ret) {
		EPRINTF("glGetShaderiv failed: 0x%08X\n", ret);
		goto err;
	}
	if (!success) {
		glGetShaderInfoLog(shader, sizeof(log_buf), NULL, log_buf);
		ret = glGetError();
		if (ret) {
			EPRINTF("glGetShaderInfoLog failed: 0x%08X\n", ret);
			goto err;
		}
		if (strlen(log_buf) > 1) {
			EPRINTF("shader compilation failed with log:\n%s\n", log_buf);
		} else {
			EPRINTF("shader compilation failed\n");
		}
		goto err;
	}

	if (pShader) {
		*pShader = shader;
	}

	return true;

err:
	return false;
}

static bool create_program(void) {
	GLuint vertex_shader_id = 0;
	GLuint fragment_shader_id = 0;
	GLuint program_id = 0;
	GLint success;
	char log[256];
	int ret;

	if (!compile_shader(GL_VERTEX_SHADER, s_vertex_shader_code, strlen(s_vertex_shader_code), &vertex_shader_id)) {
		EPRINTF("Unable to compile vertex shader.\n");
		goto err;
	}

	if (!compile_shader(GL_FRAGMENT_SHADER, s_fragment_shader_code, strlen(s_fragment_shader_code), &fragment_shader_id)) {
		EPRINTF("Unable to compile fragment shader.\n");
		goto err;
	}

	program_id = glCreateProgram();
	if (!program_id) {
		ret = glGetError();
		EPRINTF("glCreateProgram failed: 0x%08X\n", ret);
		goto err;
	}

	glAttachShader(program_id, vertex_shader_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glAttachShader(vertex_shader) failed: 0x%08X\n", ret);
		goto err;
	}

	glAttachShader(program_id, fragment_shader_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glAttachShader(fragment_shader) failed: 0x%08X\n", ret);
		goto err;
	}

	glLinkProgram(program_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glLinkProgram() failed: 0x%08X\n", ret);
		goto err;
	}

	glGetProgramiv(program_id, GL_LINK_STATUS, &success);
	ret = glGetError();
	if (ret) {
		EPRINTF("glGetProgramiv() failed: 0x%08X\n", ret);
		goto err;
	}
	if (!success) {
		glGetProgramInfoLog(program_id, sizeof(log), NULL, log);
		ret = glGetError();
		if (ret) {
			EPRINTF("glGetProgramInfoLog failed: 0x%08X\n", ret);
			goto err;
		}
		if (strlen(log) > 1) {
			EPRINTF("Unable to link shader program:\n%s\n", log);
		} else {
			EPRINTF("Unable to link shader program.\n");
		}
		goto err;
	}

	glDeleteShader(fragment_shader_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glDeleteShader(fragment_shader) failed: 0x%08X\n", ret);
		goto err;
	}

	glDeleteShader(vertex_shader_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glDeleteShader(vertex_shader) failed: 0x%08X\n", ret);
		goto err;
	}

	s_xyz_loc = glGetAttribLocation(program_id, "a_xyz");
	s_uv_loc = glGetAttribLocation(program_id, "a_uv");
	s_sampler_loc = glGetUniformLocation(program_id, "s_texture");

	s_program_id = program_id;

	return true;

err:
	if (program_id > 0) {
		glDeleteProgram(program_id);
		ret = glGetError();
		if (ret) {
			EPRINTF("glDeleteProgram failed: 0x%08X\n", ret);
			goto err;
		}
	}

	if (fragment_shader_id > 0) {
		glDeleteShader(fragment_shader_id);
		ret = glGetError();
		if (ret) {
			EPRINTF("glDeleteShader(fragment_shader) failed: 0x%08X\n", ret);
			goto err;
		}
		fragment_shader_id = 0;
	}

	if (vertex_shader_id > 0) {
		glDeleteShader(vertex_shader_id);
		ret = glGetError();
		if (ret) {
			EPRINTF("glDeleteShader(vertex_shader) failed: 0x%08X\n", ret);
			goto err;
		}
		vertex_shader_id = 0;
	}

	return false;
}

static bool destroy_program(void) {
	int ret;

	glDeleteProgram(s_program_id);
	ret = glGetError();
	if (ret) {
		EPRINTF("glDeleteProgram failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

static bool main_loop(void) {
	int ret;

	while (1) {
		glClear(GL_COLOR_BUFFER_BIT);
		ret = glGetError();
		if (ret) {
			EPRINTF("glClear failed: 0x%08X\n", ret);
			goto err;
		}

		glUseProgram(s_program_id);
		ret = glGetError();
		if (ret) {
			EPRINTF("glUseProgram failed: 0x%08X\n", ret);
			goto err;
		}

		glVertexAttribPointer(s_xyz_loc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), s_obj_vertices);
		ret = glGetError();
		if (ret) {
			EPRINTF("glVertexAttribPointer failed: 0x%08X\n", ret);
			goto err;
		}
		glVertexAttribPointer(s_uv_loc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &s_obj_vertices[3]);
		ret = glGetError();
		if (ret) {
			EPRINTF("glVertexAttribPointer failed: 0x%08X\n", ret);
			goto err;
		}

		glEnableVertexAttribArray(s_xyz_loc);
		ret = glGetError();
		if (ret) {
			EPRINTF("glEnableVertexAttribArray failed: 0x%08X\n", ret);
			goto err;
		}
		glEnableVertexAttribArray(s_uv_loc);
		ret = glGetError();
		if (ret) {
			EPRINTF("glEnableVertexAttribArray failed: 0x%08X\n", ret);
			goto err;
		}

		glActiveTexture(GL_TEXTURE0);
		ret = glGetError();
		if (ret) {
			EPRINTF("glActiveTexture failed: 0x%08X\n", ret);
			goto err;
		}
		glBindTexture(GL_TEXTURE_2D, s_texture_id);
		ret = glGetError();
		if (ret) {
			EPRINTF("glBindTexture failed: 0x%08X\n", ret);
			goto err;
		}

		glUniform1i(s_sampler_loc, 0);
		ret = glGetError();
		if (ret) {
			EPRINTF("glUniform1i failed: 0x%08X\n", ret);
			goto err;
		}

		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, s_obj_indices);
		ret = glGetError();
		if (ret) {
			EPRINTF("glDrawElements failed: 0x%08X\n", ret);
			goto err;
		}

		if (!eglSwapBuffers(s_display, s_surface)) {
			ret = eglGetError();
			EPRINTF("eglSwapBuffers failed: 0x%08X\n", ret);
			goto err;
		}
	}

	return true;

err:
	return false;
}

#endif

ushort blendLookupTable[0x20 * 0x100];
ushort subtractLookupTable[0x20 * 0x100];
ushort tintLookupTable[0x10000];

// Extras used in blending
#define maxVal(a, b) (a >= b ? a : b)
#define minVal(a, b) (a <= b ? a : b)

bool windowCreated = false;

int SCREEN_XSIZE_CONFIG = 424;
int SCREEN_XSIZE        = 424;
int SCREEN_CENTERX      = 424 / 2;

float SCREEN_XSIZE_F   = 424;
float SCREEN_CENTERX_F = 424 / 2;

float SCREEN_YSIZE_F   = SCREEN_YSIZE;
float SCREEN_CENTERY_F = SCREEN_YSIZE / 2;

int touchWidth     = SCREEN_XSIZE;
int touchHeight    = SCREEN_YSIZE;
float touchWidthF  = SCREEN_XSIZE;
float touchHeightF = SCREEN_YSIZE;

DrawListEntry drawListEntries[DRAWLAYER_COUNT];

int gfxDataPosition = 0;
GFXSurface gfxSurface[SURFACE_COUNT];
byte graphicData[GFXDATA_SIZE];

DisplaySettings displaySettings;
bool convertTo32Bit     = false;
bool mixFiltersOnJekyll = false;

#if RETRO_USING_OPENGL
GLint defaultFramebuffer = -1;
GLuint framebufferHiRes  = -1;
GLuint renderbufferHiRes = -1;
#endif

#if !RETRO_USE_ORIGINAL_CODE
// enable integer scaling, which is a modification of enhanced scaling
bool integerScaling = false;
// allows me to disable it to prevent blur on resolutions that match only on 1 axis
bool disableEnhancedScaling = false;
// enable bilinear scaling, which just disables the fancy upscaling that enhanced scaling does.
bool bilinearScaling = false;
#endif

int InitRenderDevice()
{
    char gameTitle[0x40];

    sprintf(gameTitle, "%s%s", Engine.gameWindowText, Engine.usingDataFile_Config ? "" : " (Using Data Folder)");

#if !RETRO_USE_ORIGINAL_CODE
#if RETRO_USING_SDL2

#if RETRO_PLATFORM == RETRO_ANDROID
    setenv("SDL_AUDIODRIVER", "openslES", 1);   // This is a workaround to eliminate audio delay, since we use SDL 2.28 (as of this commit this is coming from.)
                                                // This could be resolved by properly updating SDL to 2.32.10, but that'd involve updating a lot of app related files.
#endif
#if RETRO_PLATFORM == RETRO_PS4
    Engine.startFullScreen = true;
    const Uint32 sdlInitFlags = SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE) != 0) {
        PrintLog("ERROR: failed to load the PS4 user service module.");
        return 0;
    }
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD) != 0) {
        PrintLog("ERROR: failed to load the PS4 pad module.");
        return 0;
    }
    if (SDL_Init(sdlInitFlags) != 0) {
        PrintLog("ERROR: failed to initialize SDL! %s", SDL_GetError());
        return 0;
    }
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT) != 0) {
        PrintLog("WARNING: failed to load the PS4 audio output module; continuing without audio.");
    }
    else if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        PrintLog("WARNING: SDL audio is unavailable; continuing without audio. %s", SDL_GetError());
    }

    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE) != 0) {
        PrintLog("ERROR: failed to load the PS4 system service module.");
        return 0;
    }
    s_system_service_loaded = true;
    if (!load_modules() || !do_patches() || !create_context(PS4_OUTPUT_WIDTH, PS4_OUTPUT_HEIGHT)) {
        cleanup();
        return 0;
    }
    if (!eglSwapInterval(s_display, Engine.vsync ? 1 : 0)) {
        PrintLog("ERROR: eglSwapInterval failed: 0x%04X", eglGetError());
        cleanup();
        return 0;
    }
#else
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        PrintLog("ERROR: failed to initialize SDL! %s", SDL_GetError());
        return 0;
    }
#endif

#if RETRO_PLATFORM != RETRO_PS4
    SDL_DisableScreenSaver();

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, Engine.vsync ? "1" : "0");
    byte flags = 0;
#if RETRO_USING_OPENGL
    flags |= SDL_WINDOW_OPENGL;

#if RETRO_PLATFORM != RETRO_OSX
#if RETRO_PLATFORM != RETRO_ANDROID
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else // Android
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
#endif
#endif
#if RETRO_DEVICETYPE == RETRO_MOBILE
    Engine.startFullScreen = true;

    SDL_DisplayMode dm;
    SDL_GetDesktopDisplayMode(0, &dm);

    bool landscape = dm.h < dm.w;
    int h          = landscape ? dm.w : dm.h;
    int w          = landscape ? dm.h : dm.w;

    SCREEN_XSIZE = ((float)SCREEN_YSIZE * h / w);
    if (SCREEN_XSIZE % 2)
        ++SCREEN_XSIZE;
#endif

    SCREEN_CENTERX = SCREEN_XSIZE / 2;
    int windowWidth = SCREEN_XSIZE * Engine.windowScale;
    int windowHeight = SCREEN_YSIZE * Engine.windowScale;
#if RETRO_PLATFORM == RETRO_PS4
    windowWidth = PS4_OUTPUT_WIDTH;
    windowHeight = PS4_OUTPUT_HEIGHT;
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    Engine.window = SDL_CreateWindow(gameTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight,
                                     SDL_WINDOW_ALLOW_HIGHDPI | flags);

    if (!Engine.window) {
        PrintLog("ERROR: failed to create window! %s", SDL_GetError());
        return 0;
    }

#if !RETRO_USING_OPENGL
    Engine.renderer = SDL_CreateRenderer(Engine.window, -1, SDL_RENDERER_ACCELERATED);

    if (!Engine.renderer) {
        PrintLog("Accelerated renderer unavailable, trying software renderer: %s", SDL_GetError());
        Engine.renderer = SDL_CreateRenderer(Engine.window, -1, SDL_RENDERER_SOFTWARE);
        if (!Engine.renderer) {
            PrintLog("ERROR: failed to create renderer! %s", SDL_GetError());
            return 0;
        }
    }

#if RETRO_PLATFORM == RETRO_PS4
    SDL_RendererInfo rendererInfo;
    if (SDL_GetRendererInfo(Engine.renderer, &rendererInfo) == 0) {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRendererOutputSize(Engine.renderer, &outputWidth, &outputHeight);
        PrintLog("PS4 SDL renderer: %s (flags=0x%x), output=%dx%d",
                 rendererInfo.name ? rendererInfo.name : "unknown", rendererInfo.flags, outputWidth, outputHeight);
    } else {
        PrintLog("WARNING: could not query PS4 SDL renderer: %s", SDL_GetError());
    }
#endif

#if RETRO_PLATFORM == RETRO_PS4
    Engine.useHQModes = false;
    Engine.scalingMode = 0;
    drawStageGFXHQ = false;
    SDL_RenderSetLogicalSize(Engine.renderer, PS4_OUTPUT_WIDTH, PS4_OUTPUT_HEIGHT);
#else
    SDL_RenderSetLogicalSize(Engine.renderer, SCREEN_XSIZE, SCREEN_YSIZE);
#endif
    SDL_SetRenderDrawBlendMode(Engine.renderer, SDL_BLENDMODE_BLEND);

#if RETRO_SOFTWARE_RENDER
    Engine.screenBuffer = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SCREEN_XSIZE, SCREEN_YSIZE);

    if (!Engine.screenBuffer) {
        PrintLog("ERROR: failed to create screen buffer!\nerror msg: %s", SDL_GetError());
        return 0;
    }

#if RETRO_PLATFORM != RETRO_PS4
    Engine.screenBuffer2x =
        SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SCREEN_XSIZE * 2, SCREEN_YSIZE * 2);
    if (!Engine.screenBuffer2x) {
        PrintLog("ERROR: failed to create screen buffer HQ!\nerror msg: %s", SDL_GetError());
        return 0;
    }
#endif
#endif
#endif

    if (Engine.borderless && Engine.window) {
        SDL_RestoreWindow(Engine.window);
        SDL_SetWindowBordered(Engine.window, SDL_FALSE);
    }

#if RETRO_PLATFORM != RETRO_PS4
    SDL_DisplayMode disp;
    if (SDL_GetDisplayMode(0, 0, &disp) == 0) {
        Engine.screenRefreshRate = disp.refresh_rate;
    }
#endif

#endif

#endif

#if RETRO_USING_SDL1
    SDL_Init(SDL_INIT_EVERYTHING);

    byte flags = 0;
#if RETRO_USING_OPENGL
    flags |= SDL_OPENGL;
#endif

    Engine.windowSurface = SDL_SetVideoMode(SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 32, SDL_SWSURFACE | flags);
    if (!Engine.windowSurface) {
        PrintLog("ERROR: failed to create window!\nerror msg: %s", SDL_GetError());
        return 0;
    }
    // Set the window caption
    SDL_WM_SetCaption(gameTitle, NULL);

    Engine.screenBuffer =
        SDL_CreateRGBSurface(0, SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 16, 0xF800, 0x7E0, 0x1F, 0x00);

    if (!Engine.screenBuffer) {
        PrintLog("ERROR: failed to create screen buffer!\nerror msg: %s", SDL_GetError());
        return 0;
    }

    /*Engine.screenBuffer2x = SDL_SetVideoMode(SCREEN_XSIZE * 2, SCREEN_YSIZE * 2, 16, SDL_SWSURFACE | flags);
    if (!Engine.screenBuffer2x) {
        PrintLog("ERROR: failed to create screen buffer HQ!\nerror msg: %s", SDL_GetError());
        return 0;
    }*/

    if (Engine.startFullScreen) {
        Engine.windowSurface =
            SDL_SetVideoMode(SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 16, SDL_SWSURFACE | SDL_FULLSCREEN | flags);
        SDL_ShowCursor(SDL_FALSE);
        Engine.isFullScreen = true;
    }

    // TODO: not supported in 1.2?
    if (Engine.borderless) {
        // SDL_RestoreWindow(Engine.window);
        // SDL_SetWindowBordered(Engine.window, SDL_FALSE);
    }

    // SDL_SetWindowPosition(Engine.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    Engine.useHQModes = false; // disabled
    Engine.borderless = false; // disabled
#endif

#if RETRO_USING_OPENGL

#if RETRO_PLATFORM == RETRO_PS4
    const char *rendererName = (const char *)glGetString(GL_RENDERER);
    if (rendererName)
        PrintLog("GLES renderer: %s", rendererName);
#else
    Engine.glContext = SDL_GL_CreateContext(Engine.window);
    if (!Engine.glContext) {
        PrintLog("ERROR: failed to create OpenGL context! %s", SDL_GetError());
        return 0;
    }

    SDL_GL_SetSwapInterval(Engine.vsync ? 1 : 0);

#if RETRO_PLATFORM != RETRO_ANDROID && RETRO_PLATFORM != RETRO_OSX
    GLenum err = glewInit();
    if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY) {
        PrintLog("glew init error:");
        PrintLog((const char *)glewGetErrorString(err));
        return false;
    }
#endif
#endif

    displaySettings.unknown2 = 0;

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

#if RETRO_PLATFORM == RETRO_PS4
    if (!InitHardwareRenderer()) {
        PrintLog("ERROR: failed to initialize the hardware renderer.");
        cleanup();
        return 0;
    }
#endif

#if RETRO_PLATFORM == RETRO_ANDROID
    Engine.windowScale     = 1;
    displaySettings.width  = SCREEN_XSIZE;
    displaySettings.height = SCREEN_YSIZE;
#else
    displaySettings.width  = SCREEN_XSIZE_CONFIG * Engine.windowScale;
    displaySettings.height = SCREEN_YSIZE * Engine.windowScale;
#endif

    textureList[0].id = -1;
    SetupViewport();

    ResetRenderStates();
    SetupDrawIndexList();

    for (int c = 0; c < 0x10000; ++c) {
        int r               = (c & 0b1111100000000000) >> 8;
        int g               = (c & 0b0000011111100000) >> 3;
        int b               = (c & 0b0000000000011111) << 3;
        gfxPalette16to32[c] = (0xFF << 24) | (b << 16) | (g << 8) | (r << 0);
    }

#if RETRO_PLATFORM == RETRO_ANDROID
    Engine.startFullScreen = true;
#endif
#endif

#if RETRO_PLATFORM != RETRO_ANDROID
    SetScreenDimensions(SCREEN_XSIZE_CONFIG * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale);
#else
    SetScreenDimensions(SCREEN_XSIZE, SCREEN_YSIZE);
#endif

    Engine.frameBuffer = new ushort[GFX_LINESIZE * SCREEN_YSIZE];
#if RETRO_SOFTWARE_RENDER
    Engine.frameBuffer2x = new ushort[GFX_LINESIZE_DOUBLE * (SCREEN_YSIZE * 2)];
    memset(Engine.frameBuffer, 0, (GFX_LINESIZE * SCREEN_YSIZE) * sizeof(ushort));
    memset(Engine.frameBuffer2x, 0, GFX_LINESIZE_DOUBLE * (SCREEN_YSIZE * 2) * sizeof(ushort));
#else
    memset(Engine.frameBuffer, 0, (GFX_LINESIZE * SCREEN_YSIZE) * sizeof(ushort));
#endif
    Engine.texBuffer = new uint[GFX_LINESIZE * SCREEN_YSIZE];
    memset(Engine.texBuffer, 0, (GFX_LINESIZE * SCREEN_YSIZE) * sizeof(uint));

#endif

    if (Engine.startFullScreen) {
        SetFullScreen(true);
    }

    OBJECT_BORDER_X2 = SCREEN_XSIZE + 0x80;
    // OBJECT_BORDER_Y2 = SCREEN_YSIZE + 0x100;
    OBJECT_BORDER_X4 = SCREEN_XSIZE + 0x20;
    // OBJECT_BORDER_Y4 = SCREEN_YSIZE + 0x80;

    InitInputDevices();

    return 1;
}
void FlipScreen()
{
#if !RETRO_USE_ORIGINAL_CODE
    float dimAmount = 1.0;
    if ((!Engine.masterPaused || Engine.frameStep) && !drawStageGFXHQ) {
        if (Engine.dimTimer < Engine.dimLimit) {
            if (Engine.dimPercent < 1.0) {
                Engine.dimPercent += 0.05;
                if (Engine.dimPercent > 1.0)
                    Engine.dimPercent = 1.0;
            }
        }
        else if (Engine.dimPercent > 0.25 && Engine.dimLimit >= 0) {
            Engine.dimPercent *= 0.9;
        }

        dimAmount = Engine.dimMax * Engine.dimPercent;
    }

#if RETRO_SOFTWARE_RENDER && !RETRO_USING_OPENGL
#if RETRO_USING_SDL2
    SDL_Rect destScreenPos_scaled;
    SDL_Texture *texTarget = NULL;
#if RETRO_PLATFORM == RETRO_PS4
    SDL_Rect ps4OutputRect = { 0, 0, PS4_OUTPUT_WIDTH, PS4_OUTPUT_HEIGHT };
    const SDL_Rect *frameDestinationRect = &ps4OutputRect;
#else
    const SDL_Rect *frameDestinationRect = NULL;
#endif

    switch (Engine.scalingMode) {
        // reset to default if value is invalid.
        default: Engine.scalingMode = 0; break;
        case 0: break;                         // nearest
        case 1: integerScaling = true; break;  // integer scaling
        case 2: break;                         // sharp bilinear
        case 3: bilinearScaling = true; break; // regular old bilinear
    }

    SDL_GetWindowSize(Engine.window, &Engine.windowXSize, &Engine.windowYSize);
    float screenxsize = SCREEN_XSIZE;
    float screenysize = SCREEN_YSIZE;

    // check if enhanced scaling is even necessary to be calculated by checking if the screen size is close enough on one axis
    // unfortunately it has to be "close enough" because of floating point precision errors. dang it
    if (Engine.scalingMode == 2) {
        bool cond1 = std::round((Engine.windowXSize / screenxsize) * 24) / 24 == std::floor(Engine.windowXSize / screenxsize);
        bool cond2 = std::round((Engine.windowYSize / screenysize) * 24) / 24 == std::floor(Engine.windowYSize / screenysize);
        if (cond1 || cond2)
            disableEnhancedScaling = true;
    }

    // get 2x resolution if HQ is enabled.
    if (drawStageGFXHQ) {
        screenxsize *= 2;
        screenysize *= 2;
    }

    if (Engine.scalingMode != 0 && !disableEnhancedScaling) {
        // set up integer scaled texture, which is scaled to the largest integer scale of the screen buffer
        // before you make a texture that's larger than the window itself. This texture will then be scaled
        // up to the actual screen size using linear interpolation. This makes even window/screen scales
        // nice and sharp, and uneven scales as sharp as possible without creating wonky pixel scales,
        // creating a nice image.

        // get integer scale
        float scale = 1;
        if (!bilinearScaling) {
            scale =
                std::fminf(std::floor((float)Engine.windowXSize / (float)SCREEN_XSIZE), std::floor((float)Engine.windowYSize / (float)SCREEN_YSIZE));
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear"); // set interpolation to linear
        // create texture that's integer scaled.
        texTarget = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, SCREEN_XSIZE * scale, SCREEN_YSIZE * scale);

        // keep aspect
        float aspectScale = std::fminf(Engine.windowYSize / screenysize, Engine.windowXSize / screenxsize);
        if (integerScaling) {
            aspectScale = std::floor(aspectScale);
        }
        float xoffset          = (Engine.windowXSize - (screenxsize * aspectScale)) / 2;
        float yoffset          = (Engine.windowYSize - (screenysize * aspectScale)) / 2;
        destScreenPos_scaled.x = std::round(xoffset);
        destScreenPos_scaled.y = std::round(yoffset);
        destScreenPos_scaled.w = std::round(screenxsize * aspectScale);
        destScreenPos_scaled.h = std::round(screenysize * aspectScale);
        // fill the screen with the texture, making lerp work.
        SDL_RenderSetLogicalSize(Engine.renderer, Engine.windowXSize, Engine.windowYSize);
    }

    int pitch = 0;
    SDL_SetRenderTarget(Engine.renderer, texTarget);

    // Clear the screen. This is needed to keep the
    // pillarboxes in fullscreen from displaying garbage data.
    SDL_RenderClear(Engine.renderer);

    ushort *pixels = NULL;
    if (!drawStageGFXHQ) {
        SDL_LockTexture(Engine.screenBuffer, NULL, (void **)&pixels, &pitch);
        ushort *frameBufferPtr = Engine.frameBuffer;
        for (int y = 0; y < SCREEN_YSIZE; ++y) {
            memcpy(pixels, frameBufferPtr, SCREEN_XSIZE * sizeof(ushort));
            frameBufferPtr += GFX_LINESIZE;
            pixels += pitch / sizeof(ushort);
        }
        // memcpy(pixels, Engine.frameBuffer, pitch * SCREEN_YSIZE); //faster but produces issues with odd numbered screen sizes
        SDL_UnlockTexture(Engine.screenBuffer);

        SDL_RenderCopy(Engine.renderer, Engine.screenBuffer, NULL, frameDestinationRect);
    }
    else {
        int w = 0, h = 0;
        SDL_QueryTexture(Engine.screenBuffer2x, NULL, NULL, &w, &h);
        SDL_LockTexture(Engine.screenBuffer2x, NULL, (void **)&pixels, &pitch);

        ushort *framebufferPtr = Engine.frameBuffer;
        for (int y = 0; y < (SCREEN_YSIZE / 2) + 12; ++y) {
            for (int x = 0; x < GFX_LINESIZE; ++x) {
                *pixels = *framebufferPtr;
                pixels++;
                *pixels = *framebufferPtr;
                pixels++;
                framebufferPtr++;
            }

            framebufferPtr -= GFX_LINESIZE;
            for (int x = 0; x < GFX_LINESIZE; ++x) {
                *pixels = *framebufferPtr;
                pixels++;
                *pixels = *framebufferPtr;
                pixels++;
                framebufferPtr++;
            }
        }

        framebufferPtr = Engine.frameBuffer2x;
        for (int y = 0; y < ((SCREEN_YSIZE / 2) - 12) * 2; ++y) {
            for (int x = 0; x < GFX_LINESIZE; ++x) {
                *pixels = *framebufferPtr;
                framebufferPtr++;
                pixels++;

                *pixels = *framebufferPtr;
                framebufferPtr++;
                pixels++;
            }
        }
        SDL_UnlockTexture(Engine.screenBuffer2x);
        SDL_RenderCopy(Engine.renderer, Engine.screenBuffer2x, NULL, frameDestinationRect);
    }

    if (Engine.scalingMode != 0 && !disableEnhancedScaling) {
        // set render target back to the screen.
        SDL_SetRenderTarget(Engine.renderer, NULL);
        // clear the screen itself now, for same reason as above
        SDL_RenderClear(Engine.renderer);
        // copy texture to screen with lerp
        SDL_RenderCopy(Engine.renderer, texTarget, NULL, &destScreenPos_scaled);
        // Apply dimming
        SDL_SetRenderDrawColor(Engine.renderer, 0, 0, 0, 0xFF - (dimAmount * 0xFF));
        if (dimAmount < 1.0)
            SDL_RenderFillRect(Engine.renderer, NULL);
        // finally present it
        SDL_RenderPresent(Engine.renderer);
        // reset everything just in case
#if RETRO_PLATFORM == RETRO_PS4
        SDL_RenderSetLogicalSize(Engine.renderer, PS4_OUTPUT_WIDTH, PS4_OUTPUT_HEIGHT);
#else
        SDL_RenderSetLogicalSize(Engine.renderer, SCREEN_XSIZE, SCREEN_YSIZE);
#endif
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        // putting some FLEX TAPE� on that memory leak
        SDL_DestroyTexture(texTarget);
    }
    else {
        // Apply dimming
        SDL_SetRenderDrawColor(Engine.renderer, 0, 0, 0, 0xFF - (dimAmount * 0xFF));
        if (dimAmount < 1.0)
            SDL_RenderFillRect(Engine.renderer, NULL);
        // no change here
        SDL_RenderPresent(Engine.renderer);
    }
#endif

#if RETRO_USING_SDL1
    ushort *px = (ushort *)Engine.screenBuffer->pixels;
    int w      = SCREEN_XSIZE * Engine.windowScale;
    int h      = SCREEN_YSIZE * Engine.windowScale;

    if (Engine.windowScale == 1) {
        ushort *frameBufferPtr = Engine.frameBuffer;
        for (int y = 0; y < SCREEN_YSIZE; ++y) {
            for (int x = 0; x < SCREEN_XSIZE; ++x) {
                pixels[x] = frameBufferPtr[x];
            }
            frameBufferPtr += GFX_LINESIZE;
            px += Engine.screenBuffer->pitch / sizeof(ushort);
        }
        // memcpy(Engine.screenBuffer->pixels, Engine.frameBuffer, Engine.screenBuffer->pitch * SCREEN_YSIZE);
    }
    else {
        // TODO: this better, I really dont know how to use SDL1.2 well lol
        int dx = 0, dy = 0;
        do {
            do {
                int x = (int)(dx * (1.0f / Engine.windowScale));
                int y = (int)(dy * (1.0f / Engine.windowScale));

                px[dx + (dy * w)] = Engine.frameBuffer[x + (y * GFX_LINESIZE)];

                dx++;
            } while (dx < w);
            dy++;
            dx = 0;
        } while (dy < h);
    }

    // Apply image to screen
    SDL_BlitSurface(Engine.screenBuffer, NULL, Engine.windowSurface, NULL);

    // Update Screen
    SDL_Flip(Engine.windowSurface);
#endif

#endif // !RETRO_SOFTWARE_RENDER

#endif
}

bool SwapRenderBuffers()
{
#if RETRO_USING_OPENGL && RETRO_USING_SDL2
#if RETRO_PLATFORM == RETRO_PS4
    if (eglSwapBuffers(s_display, s_surface) != EGL_TRUE) {
        PrintLog("ERROR: eglSwapBuffers failed: 0x%04X", eglGetError());
        Engine.running = false;
        return false;
    }
    return true;
#else
    SDL_GL_SwapWindow(Engine.window);
    return true;
#endif
#endif
    return true;
}

void ReleaseRenderDevice(bool refresh)
{
    if (!refresh) {
        ClearMeshData();
        ClearTextures(false);
    }

#if !RETRO_USE_ORIGINAL_CODE
    if (Engine.frameBuffer)
        delete[] Engine.frameBuffer;
#if RETRO_SOFTWARE_RENDER
    if (Engine.frameBuffer2x)
        delete[] Engine.frameBuffer2x;
#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
    if (Engine.screenBuffer)
        SDL_DestroyTexture(Engine.screenBuffer);
    Engine.screenBuffer = NULL;
    if (Engine.screenBuffer2x)
        SDL_DestroyTexture(Engine.screenBuffer2x);
    Engine.screenBuffer2x = NULL;
#endif
#if RETRO_USING_SDL1
    SDL_FreeSurface(Engine.screenBuffer);
#endif
#endif
    if (Engine.texBuffer)
        delete[] Engine.texBuffer;

#if RETRO_USING_OPENGL
#if RETRO_PLATFORM == RETRO_PS4
    ReleaseHardwareRenderer();
    cleanup();
#else
    if (Engine.glContext)
        SDL_GL_DeleteContext(Engine.glContext);
    Engine.glContext = NULL;
#endif
#endif

#if RETRO_USING_SDL2
#if !RETRO_USING_OPENGL
    if (Engine.renderer)
        SDL_DestroyRenderer(Engine.renderer);
    Engine.renderer = NULL;
#endif
    if (Engine.window)
        SDL_DestroyWindow(Engine.window);
    Engine.window = NULL;
#endif
#endif
}

void GenerateBlendLookupTable(void)
{
    for (int y = 0; y < 0x100; y++) {
        for (int x = 0; x < 0x20; x++) {
            blendLookupTable[x + (0x20 * y)]    = y * x >> 8;
            subtractLookupTable[x + (0x20 * y)] = y * (0x1F - x) >> 8;
        }
    }

    for (int i = 0; i < 0x10000; i++) {
        int tintValue      = ((i & 0x1F) + ((i & 0x7E0) >> 6) + ((i & 0xF800) >> 11)) / 3 + 6;
        tintLookupTable[i] = 0x841 * minVal(tintValue, 0x1F);
    }
}

void ClearScreen(byte index)
{
#if RETRO_SOFTWARE_RENDER
    ushort color        = activePalette[index];
    ushort *framebuffer = Engine.frameBuffer;
    int cnt             = GFX_LINESIZE * SCREEN_YSIZE;
    while (cnt--) {
        *framebuffer = color;
        ++framebuffer;
    }
#endif
}

void SetScreenDimensions(int width, int height)
{
    touchWidth               = width;
    touchHeight              = height;
    displaySettings.width    = width;
    displaySettings.height   = height;
    touchWidthF              = width;
    displaySettings.unknown1 = 16;
    touchHeightF             = height;
    // displaySettings.maxWidth = 424;
    double aspect    = (((width >> 16) * 65536.0) + width) / (((height >> 16) * 65536.0) + height);
    SCREEN_XSIZE_F   = SCREEN_YSIZE * aspect;
    SCREEN_CENTERX_F = aspect * SCREEN_CENTERY;
    SetPerspectiveMatrix(SCREEN_YSIZE * aspect, SCREEN_YSIZE_F, 0.0, 1000.0);
#if RETRO_USING_OPENGL
    glViewport(0, 0, displaySettings.width, displaySettings.height);
#endif

    Engine.useHighResAssets = displaySettings.height > (SCREEN_YSIZE * 2);
    int displayWidth        = aspect * SCREEN_YSIZE;
    // if (val > displaySettings.maxWidth)
    //    val = displaySettings.maxWidth;
#if !RETRO_USE_ORIGINAL_CODE
    SetScreenSize(displayWidth, (displayWidth + 9) & -0x8);
#else
    SetScreenSize(displayWidth, (displayWidth + 9) & -0x10);
#endif

    int width2 = 0;
    int wBuf   = GFX_LINESIZE - 1;
    while (wBuf > 0) {
        width2++;
        wBuf >>= 1;
    }
    int height2 = 0;
    int hBuf    = SCREEN_YSIZE - 1;
    while (hBuf > 0) {
        height2++;
        hBuf >>= 1;
    }
    int texWidth  = 1 << width2;
    int texHeight = 1 << height2;

    textureList[0].widthN  = 1.0f / texWidth;
    textureList[0].heightN = 1.0f / texHeight;

    float w  = (SCREEN_XSIZE * textureList[0].widthN);
    float w2 = (GFX_LINESIZE * textureList[0].widthN);
    float h  = (SCREEN_YSIZE * textureList[0].heightN);

    retroVertexList[0] = -SCREEN_CENTERX_F;
    retroVertexList[1] = SCREEN_CENTERY_F;
    retroVertexList[2] = 160.0;
    retroVertexList[6] = 0.0;
    retroVertexList[7] = 0.0;

    retroVertexList[9]  = SCREEN_CENTERX_F;
    retroVertexList[10] = SCREEN_CENTERY_F;
    retroVertexList[11] = 160.0;
    retroVertexList[15] = w;
    retroVertexList[16] = 0.0;

    retroVertexList[18] = -SCREEN_CENTERX_F;
    retroVertexList[19] = -SCREEN_CENTERY_F;
    retroVertexList[20] = 160.0;
    retroVertexList[24] = 0.0;
    retroVertexList[25] = h;

    retroVertexList[27] = SCREEN_CENTERX_F;
    retroVertexList[28] = -SCREEN_CENTERY_F;
    retroVertexList[29] = 160.0;
    retroVertexList[33] = w;
    retroVertexList[34] = h;

    screenBufferVertexList[0] = -1.0;
    screenBufferVertexList[1] = 1.0;
    screenBufferVertexList[2] = 1.0;
    screenBufferVertexList[6] = 0.0;
    screenBufferVertexList[7] = h;

    screenBufferVertexList[9]  = 1.0;
    screenBufferVertexList[10] = 1.0;
    screenBufferVertexList[11] = 1.0;
    screenBufferVertexList[15] = w2;
    screenBufferVertexList[16] = h;

    screenBufferVertexList[18] = -1.0;
    screenBufferVertexList[19] = -1.0;
    screenBufferVertexList[20] = 1.0;
    screenBufferVertexList[24] = 0.0;
    screenBufferVertexList[25] = 0.0;

    screenBufferVertexList[27] = 1.0;
    screenBufferVertexList[28] = -1.0;
    screenBufferVertexList[29] = 1.0;
    screenBufferVertexList[33] = w2;
    screenBufferVertexList[34] = 0.0;
}

void SetScreenSize(int width, int lineSize)
{
    SCREEN_XSIZE        = width;
    SCREEN_CENTERX      = width / 2;
    SCREEN_SCROLL_LEFT  = SCREEN_CENTERX - 8;
    SCREEN_SCROLL_RIGHT = SCREEN_CENTERX + 8;
    OBJECT_BORDER_X2    = width + 0x80;
    OBJECT_BORDER_X4    = width + 0x20;

    GFX_LINESIZE          = lineSize;
    GFX_LINESIZE_MINUSONE = lineSize - 1;
    GFX_LINESIZE_DOUBLE   = 2 * lineSize;
    GFX_FRAMEBUFFERSIZE   = SCREEN_YSIZE * lineSize;
    GFX_FBUFFERMINUSONE   = SCREEN_YSIZE * lineSize - 1;
}

#if RETRO_SOFTWARE_RENDER
void CopyFrameOverlay2x()
{
    ushort *frameBuffer   = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
    ushort *frameBuffer2x = Engine.frameBuffer2x;

    for (int y = 0; y < (SCREEN_YSIZE / 2) - 12; ++y) {
        for (int x = 0; x < GFX_LINESIZE; ++x) {
            if (*frameBuffer == 0xF81F) { // magenta
                frameBuffer2x += 2;
            }
            else {
                *frameBuffer2x = *frameBuffer;
                frameBuffer2x++;
                *frameBuffer2x = *frameBuffer;
                frameBuffer2x++;
            }
            ++frameBuffer;
        }

        frameBuffer -= GFX_LINESIZE;
        for (int x = 0; x < GFX_LINESIZE; ++x) {
            if (*frameBuffer == 0xF81F) { // magenta
                frameBuffer2x += 2;
            }
            else {
                *frameBuffer2x = *frameBuffer;
                frameBuffer2x++;
                *frameBuffer2x = *frameBuffer;
                frameBuffer2x++;
            }
            ++frameBuffer;
        }
    }
}
#endif

void SetupViewport()
{
    double aspect    = displaySettings.width / (double)displaySettings.height;
    SCREEN_XSIZE_F   = SCREEN_YSIZE * aspect;
    SCREEN_CENTERX_F = aspect * SCREEN_CENTERY;

#if RETRO_USING_OPENGL
#if RETRO_PLATFORM != RETRO_PS4
    glScalef(320.0f / (SCREEN_YSIZE * aspect), 1.0, 1.0);
#endif
#endif

    SetPerspectiveMatrix(90.0, 0.75, 1.0, 5000.0);

#if RETRO_USING_OPENGL
    glViewport(displaySettings.offsetX, 0, displaySettings.width, displaySettings.height);
#endif
    int displayWidth = aspect * SCREEN_YSIZE;
#if !RETRO_USE_ORIGINAL_CODE
    SetScreenSize(displayWidth, (displayWidth + 9) & -0x8);
#else
    SetScreenSize(displayWidth, (displayWidth + 9) & -0x10);
#endif

    Engine.useHighResAssets = displaySettings.height > (SCREEN_YSIZE * 2);

#if RETRO_USING_OPENGL
#if RETRO_PLATFORM != RETRO_PS4
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
#endif
#endif

    int width2 = 0;
    int wBuf   = GFX_LINESIZE - 1;
    while (wBuf > 0) {
        width2++;
        wBuf >>= 1;
    }
    int height2 = 0;
    int hBuf    = SCREEN_YSIZE - 1;
    while (hBuf > 0) {
        height2++;
        hBuf >>= 1;
    }
    int texWidth  = 1 << width2;
    int texHeight = 1 << height2;

    float w  = (SCREEN_XSIZE * textureList[0].widthN);
    float w2 = (GFX_LINESIZE * textureList[0].widthN);
    float h  = (SCREEN_YSIZE * textureList[0].heightN);

    retroVertexList[0] = -SCREEN_CENTERX_F;
    retroVertexList[1] = SCREEN_CENTERY_F;
    retroVertexList[2] = 160.0;
    retroVertexList[6] = 0.0;
    retroVertexList[7] = 0.0;

    retroVertexList[9]  = SCREEN_CENTERX_F;
    retroVertexList[10] = SCREEN_CENTERY_F;
    retroVertexList[11] = 160.0;
    retroVertexList[15] = w;
    retroVertexList[16] = 0.0;

    retroVertexList[18] = -SCREEN_CENTERX_F;
    retroVertexList[19] = -SCREEN_CENTERY_F;
    retroVertexList[20] = 160.0;
    retroVertexList[24] = 0.0;
    retroVertexList[25] = h;

    retroVertexList[27] = SCREEN_CENTERX_F;
    retroVertexList[28] = -SCREEN_CENTERY_F;
    retroVertexList[29] = 160.0;
    retroVertexList[33] = w;
    retroVertexList[34] = h;

    screenBufferVertexList[0] = -1.0;
    screenBufferVertexList[1] = 1.0;
    screenBufferVertexList[2] = 1.0;
    screenBufferVertexList[6] = 0.0;
    screenBufferVertexList[7] = h;

    screenBufferVertexList[9]  = 1.0;
    screenBufferVertexList[10] = 1.0;
    screenBufferVertexList[11] = 1.0;
    screenBufferVertexList[15] = w2;
    screenBufferVertexList[16] = h;

    screenBufferVertexList[18] = -1.0;
    screenBufferVertexList[19] = -1.0;
    screenBufferVertexList[20] = 1.0;
    screenBufferVertexList[24] = 0.0;
    screenBufferVertexList[25] = 0.0;

    screenBufferVertexList[27] = 1.0;
    screenBufferVertexList[28] = -1.0;
    screenBufferVertexList[29] = 1.0;
    screenBufferVertexList[33] = w2;
    screenBufferVertexList[34] = 0.0;

    StrCopy(textureList[0].fileName, "RetroBuffer");
    textureList[0].width   = texWidth;
    textureList[0].height  = texHeight;
    textureList[0].format  = TEXFMT_RETROBUFFER;
    textureList[0].widthN  = 1.0f / texWidth;
    textureList[0].heightN = 1.0f / texHeight;

    if (Engine.useHighResAssets) {
#if RETRO_USING_OPENGL
        if (framebufferHiRes != -1)
            glDeleteFramebuffers(1, &framebufferHiRes);
        if (renderbufferHiRes != -1)
            glDeleteTextures(1, &renderbufferHiRes);
        framebufferHiRes  = -1;
        renderbufferHiRes = -1;

        glGenFramebuffers(1, &framebufferHiRes);
        glBindFramebuffer(GL_FRAMEBUFFER, framebufferHiRes);
        glGenTextures(1, &renderbufferHiRes);
        glBindTexture(GL_TEXTURE_2D, renderbufferHiRes);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texWidth << 1, texHeight << 1, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderbufferHiRes, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
#endif

        float w                    = (((((GFX_LINESIZE >> 16) * 65536.0) + GFX_LINESIZE) + 0.5) * textureList[0].widthN) - 0.001;
        float h                    = (SCREEN_YSIZE * textureList[0].heightN) - 0.001;
        screenBufferVertexList[0]  = -1.0;
        screenBufferVertexList[1]  = 1.0;
        screenBufferVertexList[2]  = 1.0;
        screenBufferVertexList[6]  = 0.0;
        screenBufferVertexList[9]  = 1.0;
        screenBufferVertexList[10] = 1.0;
        screenBufferVertexList[11] = 1.0;
        screenBufferVertexList[7]  = h;
        screenBufferVertexList[16] = h;
        screenBufferVertexList[18] = -1.0;
        screenBufferVertexList[19] = -1.0;
        screenBufferVertexList[20] = 1.0;
        screenBufferVertexList[24] = 0.0;
        screenBufferVertexList[25] = 0.0;
        screenBufferVertexList[27] = 1.0;
        screenBufferVertexList[28] = -1.0;
        screenBufferVertexList[29] = 1.0;
        screenBufferVertexList[15] = w;
        screenBufferVertexList[34] = 0.0;
        screenBufferVertexList[33] = w;
    }
    else {
#if RETRO_USING_OPENGL
        if (framebufferHiRes != -1)
            glDeleteFramebuffers(1, &framebufferHiRes);
        if (renderbufferHiRes != -1)
            glDeleteTextures(1, &renderbufferHiRes);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        framebufferHiRes  = -1;
        renderbufferHiRes = -1;
#endif
    }

    bool transfer = false;
#if RETRO_USING_OPENGL
    if (textureList[0].id != -1) {
        glDeleteTextures(1, &textureList[0].id);
        transfer = true;
    }
    glGenTextures(1, &textureList[0].id);
    glBindTexture(GL_TEXTURE_2D, textureList[0].id);
#endif

    convertTo32Bit = true;
#if RETRO_USING_OPENGL
    if (displaySettings.height > 720) {
        convertTo32Bit = true;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    }
    else if (convertTo32Bit)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 0);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Engine.scalingMode ? GL_LINEAR : GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif

    mixFiltersOnJekyll = Engine.useHighResAssets;

    if (transfer && Engine.frameBuffer)
        TransferRetroBuffer();
}

void SetFullScreen(bool fs)
{
#if RETRO_PLATFORM == RETRO_PS4
    if (fs) {
        displaySettings.height  = PS4_OUTPUT_HEIGHT;
        displaySettings.width   = SCREEN_XSIZE_CONFIG * displaySettings.height / SCREEN_YSIZE;
        displaySettings.offsetX = (PS4_OUTPUT_WIDTH - displaySettings.width) / 2;
        if (displaySettings.width > PS4_OUTPUT_WIDTH) {
            displaySettings.width   = PS4_OUTPUT_WIDTH;
            displaySettings.offsetX = 0;
        }
    }
    else {
        displaySettings.width   = SCREEN_XSIZE_CONFIG * Engine.windowScale;
        displaySettings.height  = SCREEN_YSIZE * Engine.windowScale;
        displaySettings.offsetX = 0;
    }
    SetupViewport();
    Engine.isFullScreen = fs;
    return;
#endif
    if (fs) {
#if RETRO_USING_SDL1
        Engine.windowSurface =
            SDL_SetVideoMode(SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 16, SDL_SWSURFACE | SDL_FULLSCREEN);
        SDL_ShowCursor(SDL_FALSE);
#elif RETRO_USING_SDL2
        SDL_RestoreWindow(Engine.window);
        SDL_SetWindowFullscreen(Engine.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(SDL_FALSE);

#if RETRO_USING_OPENGL
        SDL_DisplayMode mode;
        SDL_GetDesktopDisplayMode(0, &mode);

        int w = mode.w;
        int h = mode.h;
        if (mode.h > mode.w) {
            w = mode.h;
            h = mode.w;
        }

#if RETRO_PLATFORM != RETRO_iOS && RETRO_PLATFORM != RETRO_ANDROID
        float aspect            = SCREEN_XSIZE_CONFIG / (float)SCREEN_YSIZE;
        displaySettings.height  = h;
        displaySettings.width   = aspect * displaySettings.height;
        displaySettings.offsetX = abs(w - displaySettings.width) / 2;
        if (displaySettings.width > w) {
            displaySettings.offsetX = 0;
            displaySettings.width   = w;
        }

        SetupViewport();
#else
        displaySettings.height = h;
        displaySettings.width  = w;
        glViewport(0, 0, displaySettings.width, displaySettings.height);
#endif
#endif
#endif
    }
    else {
#if RETRO_USING_SDL1
        Engine.windowSurface = SDL_SetVideoMode(SCREEN_XSIZE * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale, 16, SDL_SWSURFACE);
        SDL_ShowCursor(SDL_TRUE);
#elif RETRO_USING_SDL2
        SDL_SetWindowFullscreen(Engine.window, false);
        SDL_ShowCursor(SDL_TRUE);
        SDL_SetWindowSize(Engine.window, SCREEN_XSIZE_CONFIG * Engine.windowScale, SCREEN_YSIZE * Engine.windowScale);
        SDL_SetWindowPosition(Engine.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_RestoreWindow(Engine.window);

        displaySettings.width   = SCREEN_XSIZE_CONFIG * Engine.windowScale;
        displaySettings.height  = SCREEN_YSIZE * Engine.windowScale;
        displaySettings.offsetX = 0;
        SetupViewport();
#endif
    }
    Engine.isFullScreen = fs;
}

void DrawObjectList(int Layer)
{
    int size = drawListEntries[Layer].listSize;
    for (int i = 0; i < size; ++i) {
        objectEntityPos = drawListEntries[Layer].entityRefs[i];
        int type        = objectEntityList[objectEntityPos].type;
        if (type) {
            if (scriptCode[objectScriptList[type].eventDraw.scriptCodePtr] > 0)
                ProcessScript(objectScriptList[type].eventDraw.scriptCodePtr, objectScriptList[type].eventDraw.jumpTablePtr, EVENT_DRAW);
        }
    }
}
void DrawStageGFX()
{
    waterDrawPos = waterLevel - yScrollOffset;

#if RETRO_SOFTWARE_RENDER
    if (waterDrawPos < 0)
        waterDrawPos = 0;

    if (waterDrawPos > SCREEN_YSIZE)
        waterDrawPos = SCREEN_YSIZE;
#endif

    if (tLayerMidPoint < 3) {
        DrawObjectList(0);

        if (activeTileLayers[0] < LAYER_COUNT) {
            switch (stageLayouts[activeTileLayers[0]].type) {
                case LAYER_HSCROLL: DrawHLineScrollLayer(0); break;

                case LAYER_VSCROLL: DrawVLineScrollLayer(0); break;

                case LAYER_3DFLOOR:
#if RETRO_SOFTWARE_RENDER
                    drawStageGFXHQ = false;
#endif
                    Draw3DFloorLayer(0);
                    break;

                case LAYER_3DSKY:
#if RETRO_SOFTWARE_RENDER
#if !RETRO_USE_ORIGINAL_CODE
                    if (Engine.useHQModes)
#endif
                        drawStageGFXHQ = true;

                    Draw3DSkyLayer(0);
#endif
                    break;

                default: break;
            }
        }

        DrawObjectList(1);

        if (activeTileLayers[1] < LAYER_COUNT) {
            switch (stageLayouts[activeTileLayers[1]].type) {
                case LAYER_HSCROLL: DrawHLineScrollLayer(1); break;

                case LAYER_VSCROLL: DrawVLineScrollLayer(1); break;

                case LAYER_3DFLOOR:
#if RETRO_SOFTWARE_RENDER
                    drawStageGFXHQ = false;
#endif
                    Draw3DFloorLayer(1);
                    break;

                case LAYER_3DSKY:
#if RETRO_SOFTWARE_RENDER
#if !RETRO_USE_ORIGINAL_CODE
                    if (Engine.useHQModes)
#endif
                        drawStageGFXHQ = true;

                    Draw3DSkyLayer(1);
#endif
                    break;

                default: break;
            }
        }

        DrawObjectList(2);
        DrawObjectList(3);
        DrawObjectList(4);

        if (activeTileLayers[2] < LAYER_COUNT) {
            switch (stageLayouts[activeTileLayers[2]].type) {
                case LAYER_HSCROLL: DrawHLineScrollLayer(2); break;

                case LAYER_VSCROLL: DrawVLineScrollLayer(2); break;

                case LAYER_3DFLOOR:
#if RETRO_SOFTWARE_RENDER
                    drawStageGFXHQ = false;
#endif
                    Draw3DFloorLayer(2);
                    break;

                case LAYER_3DSKY:
#if RETRO_SOFTWARE_RENDER
#if !RETRO_USE_ORIGINAL_CODE
                    if (Engine.useHQModes)
#endif
                        drawStageGFXHQ = true;

                    Draw3DSkyLayer(2);
#endif
                    break;

                default: break;
            }
        }
    }
    else if (tLayerMidPoint < 6) {
        DrawObjectList(0);

        if (activeTileLayers[0] < LAYER_COUNT) {
            switch (stageLayouts[activeTileLayers[0]].type) {
                case LAYER_HSCROLL: DrawHLineScrollLayer(0); break;

                case LAYER_VSCROLL: DrawVLineScrollLayer(0); break;

                case LAYER_3DFLOOR:
#if RETRO_SOFTWARE_RENDER
                    drawStageGFXHQ = false;
#endif
                    Draw3DFloorLayer(0);
                    break;

                case LAYER_3DSKY:
#if RETRO_SOFTWARE_RENDER
#if !RETRO_USE_ORIGINAL_CODE
                    if (Engine.useHQModes)
#endif
                        drawStageGFXHQ = true;

                    Draw3DSkyLayer(0);
#endif
                    break;

                default: break;
            }
        }

        DrawObjectList(1);

        if (activeTileLayers[1] < LAYER_COUNT) {
            switch (stageLayouts[activeTileLayers[1]].type) {
                case LAYER_HSCROLL: DrawHLineScrollLayer(1); break;

                case LAYER_VSCROLL: DrawVLineScrollLayer(1); break;

                case LAYER_3DFLOOR:
#if RETRO_SOFTWARE_RENDER
                    drawStageGFXHQ = false;
#endif
                    Draw3DFloorLayer(1);
                    break;

                case LAYER_3DSKY:
#if RETRO_SOFTWARE_RENDER
#if !RETRO_USE_ORIGINAL_CODE
                    if (Engine.useHQModes)
#endif
                        drawStageGFXHQ = true;

                    Draw3DSkyLayer(1);
#endif
                    break;

                default: break;
            }
        }

        DrawObjectList(2);

        if (activeTileLayers[2] < LAYER_COUNT) {
            switch (stageLayouts[activeTileLayers[2]].type) {
                case LAYER_HSCROLL: DrawHLineScrollLayer(2); break;
                case LAYER_VSCROLL: DrawVLineScrollLayer(2); break;
                case LAYER_3DFLOOR:
#if RETRO_SOFTWARE_RENDER
                    drawStageGFXHQ = false;
#endif
                    Draw3DFloorLayer(2);
                    break;
                case LAYER_3DSKY:
#if RETRO_SOFTWARE_RENDER
#if !RETRO_USE_ORIGINAL_CODE
                    if (Engine.useHQModes)
#endif
                        drawStageGFXHQ = true;

                    Draw3DSkyLayer(2);
#endif
                    break;
                default: break;
            }
        }

        DrawObjectList(3);
        DrawObjectList(4);
    }

    if (tLayerMidPoint < 6) {
        if (activeTileLayers[3] < LAYER_COUNT) {
            switch (stageLayouts[activeTileLayers[3]].type) {
                case LAYER_HSCROLL: DrawHLineScrollLayer(3); break;

                case LAYER_VSCROLL: DrawVLineScrollLayer(3); break;

                case LAYER_3DFLOOR:
#if RETRO_SOFTWARE_RENDER
                    drawStageGFXHQ = false;
#endif
                    Draw3DFloorLayer(3);
                    break;

                case LAYER_3DSKY:
#if RETRO_SOFTWARE_RENDER
#if !RETRO_USE_ORIGINAL_CODE
                    if (Engine.useHQModes)
#endif
                        drawStageGFXHQ = true;

                    Draw3DSkyLayer(3);
#endif
                    break;
                default: break;
            }
        }

        DrawObjectList(5);
#if RETRO_REV03
#if !RETRO_USE_ORIGINAL_CODE
        // Hacky fix for Tails Object not working properly in special stages on non-Origins bytecode
        if (forceUseScripts || Engine.usingOrigins)
#endif
            DrawObjectList(7);
#endif
        DrawObjectList(6);
    }

#if !RETRO_USE_ORIGINAL_CODE
    if (drawStageGFXHQ)
        DrawDebugOverlays();
#endif

#if RETRO_SOFTWARE_RENDER
    if (drawStageGFXHQ) {
        CopyFrameOverlay2x();

        if (fadeMode == 1) {
            DrawRectangle(0, 0, SCREEN_XSIZE, SCREEN_YSIZE, fadeR, fadeG, fadeB, fadeA);
            SetFadeHQ(fadeR, fadeG, fadeB, fadeA);
        }
        else if (fadeMode == 2) {
            DrawClassicFade(0, 0, SCREEN_XSIZE, SCREEN_YSIZE, fadeR, fadeG, fadeB, fadeX);
        }
    }
    else {
        if (fadeMode == 1) {
            DrawRectangle(0, 0, SCREEN_XSIZE, SCREEN_YSIZE, fadeR, fadeG, fadeB, fadeA);
        }
        else if (fadeMode == 2) {
            DrawClassicFade(0, 0, SCREEN_XSIZE, SCREEN_YSIZE, fadeR, fadeG, fadeB, fadeX);
        }
    }
#endif

#if !RETRO_USE_ORIGINAL_CODE
    if (!drawStageGFXHQ)
        DrawDebugOverlays();
#endif
}

#if !RETRO_USE_ORIGINAL_CODE
void DrawDebugOverlays()
{
    if (showHitboxes) {
        for (int i = 0; i < debugHitboxCount; ++i) {
            DebugHitboxInfo *info = &debugHitboxList[i];
            int x                 = info->xpos + (info->left << 16);
            int y                 = info->ypos + (info->top << 16);
            int w                 = abs((info->xpos + (info->right << 16)) - x) >> 16;
            int h                 = abs((info->ypos + (info->bottom << 16)) - y) >> 16;
            x                     = (x >> 16) - xScrollOffset;
            y                     = (y >> 16) - yScrollOffset;

            switch (info->type) {
                case H_TYPE_TOUCH:
                    if (showHitboxes & 1)
                        DrawRectangle(x, y, w, h, info->collision ? 0x80 : 0xFF, info->collision ? 0x80 : 0x00, 0x00, 0x60);
                    break;

                case H_TYPE_BOX:
                    if (showHitboxes & 1) {
                        DrawRectangle(x, y, w, h, 0x00, 0x00, 0xFF, 0x60);
                        if (info->collision & 1) // top
                            DrawRectangle(x, y, w, 1, 0xFF, 0xFF, 0x00, 0xC0);
                        if (info->collision & 8) // bottom
                            DrawRectangle(x, y + h, w, 1, 0xFF, 0xFF, 0x00, 0xC0);
                        if (info->collision & 2) { // left
                            int sy = y;
                            int sh = h;
                            if (info->collision & 1) {
                                sy++;
                                sh--;
                            }
                            if (info->collision & 8)
                                sh--;
                            DrawRectangle(x, sy, 1, sh, 0xFF, 0xFF, 0x00, 0xC0);
                        }
                        if (info->collision & 4) { // right
                            int sy = y;
                            int sh = h;
                            if (info->collision & 1) {
                                sy++;
                                sh--;
                            }
                            if (info->collision & 8)
                                sh--;
                            DrawRectangle(x + w, sy, 1, sh, 0xFF, 0xFF, 0x00, 0xC0);
                        }
                    }
                    break;

                case H_TYPE_PLAT:
                    if (showHitboxes & 1) {
                        DrawRectangle(x, y, w, h, 0x00, 0xFF, 0x00, 0x60);
                        if (info->collision & 1) // top
                            DrawRectangle(x, y, w, 1, 0xFF, 0xFF, 0x00, 0xC0);
                        if (info->collision & 8) // bottom
                            DrawRectangle(x, y + h, w, 1, 0xFF, 0xFF, 0x00, 0xC0);
                    }
                    break;

                case H_TYPE_FINGER:
                    if (showHitboxes & 2)
                        DrawRectangle(x + xScrollOffset, y + yScrollOffset, w, h, 0xF0, 0x00, 0xF0, 0x60);
                    break;
            }
        }
    }

    if (Engine.showPaletteOverlay) {
        for (int p = 0; p < PALETTE_COUNT; ++p) {
            int x = (SCREEN_XSIZE - (0x10 << 3));
            int y = (SCREEN_YSIZE - (0x10 << 2));
            for (int c = 0; c < PALETTE_COLOR_COUNT; ++c) {
                int g = fullPalette32[p][c].g;
                // HQ mode overrides any magenta px, so slightly change the g channel since it has the most bits to make it "not quite magenta"
                if (drawStageGFXHQ && fullPalette32[p][c].r == 0xFF && fullPalette32[p][c].g == 0x00 && fullPalette32[p][c].b == 0xFF)
                    g += 8;

                DrawRectangle(x + ((c & 0xF) << 1) + ((p % (PALETTE_COUNT / 2)) * (2 * 16)),
                              y + ((c >> 4) << 1) + ((p / (PALETTE_COUNT / 2)) * (2 * 16)), 2, 2, fullPalette32[p][c].r, g, fullPalette32[p][c].b,
                              0xFF);
            }
        }
    }
}
#endif

void DrawHLineScrollLayer(int layerID)
{
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    if (!layer->xsize || !layer->ysize)
        return;

#if RETRO_SOFTWARE_RENDER
    int screenwidth16  = (GFX_LINESIZE >> 4) - 1;
    int layerwidth     = layer->xsize;
    int layerheight    = layer->ysize;
    bool aboveMidPoint = layerID >= tLayerMidPoint;

    byte *lineScroll;
    int *deformationData;
    int *deformationDataW;

    int yscrollOffset = 0;
    if (activeTileLayers[layerID]) { // BG Layer
        int yScroll    = yScrollOffset * layer->parallaxFactor >> 8;
        int fullheight = layerheight << 7;
        layer->scrollPos += layer->scrollSpeed;
        if (layer->scrollPos > fullheight << 16)
            layer->scrollPos -= fullheight << 16;
        yscrollOffset    = (yScroll + (layer->scrollPos >> 16)) % fullheight;
        layerheight      = fullheight >> 7;
        lineScroll       = layer->lineScroll;
        deformationData  = &bgDeformationData2[(byte)(yscrollOffset + layer->deformationOffset)];
        deformationDataW = &bgDeformationData3[(byte)(yscrollOffset + waterDrawPos + layer->deformationOffsetW)];
    }
    else { // FG Layer
        lastXSize     = layer->xsize;
        yscrollOffset = yScrollOffset;
        lineScroll    = layer->lineScroll;
        for (int i = 0; i < PARALLAX_COUNT; ++i) hParallax.linePos[i] = xScrollOffset;
        deformationData  = &bgDeformationData0[(byte)(yscrollOffset + layer->deformationOffset)];
        deformationDataW = &bgDeformationData1[(byte)(yscrollOffset + waterDrawPos + layer->deformationOffsetW)];
    }

    if (layer->type == LAYER_HSCROLL) {
        if (lastXSize != layerwidth) {
            int fullLayerwidth = layerwidth << 7;
            for (int i = 0; i < hParallax.entryCount; ++i) {
                hParallax.linePos[i] = xScrollOffset * hParallax.parallaxFactor[i] >> 8;
                if (hParallax.scrollPos[i] > fullLayerwidth << 16)
                    hParallax.scrollPos[i] -= fullLayerwidth << 16;
                if (hParallax.scrollPos[i] < 0)
                    hParallax.scrollPos[i] += fullLayerwidth << 16;
                hParallax.linePos[i] += hParallax.scrollPos[i] >> 16;
                hParallax.linePos[i] %= fullLayerwidth;
            }
        }
        int w = -1;
        if (activeTileLayers[layerID])
            w = layerwidth;
        lastXSize = w;
    }

    ushort *frameBufferPtr = Engine.frameBuffer;
    byte *lineBuffer       = gfxLineBuffer;
    int tileYPos           = yscrollOffset % (layerheight << 7);
    if (tileYPos < 0)
        tileYPos += layerheight << 7;
    byte *scrollIndex = &lineScroll[tileYPos];
    int tileY16       = tileYPos & 0xF;
    int chunkY        = tileYPos >> 7;
    int tileY         = (tileYPos & 0x7F) >> 4;

    // Draw Above Water (if applicable)
    int drawableLines[2] = { waterDrawPos, SCREEN_YSIZE - waterDrawPos };
    for (int i = 0; i < 2; ++i) {
        while (drawableLines[i]--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int chunkX = hParallax.linePos[*scrollIndex];
            if (i == 0) {
                if (hParallax.deform[*scrollIndex])
                    chunkX += *deformationData;
                ++deformationData;
            }
            else {
                if (hParallax.deform[*scrollIndex])
                    chunkX += *deformationDataW;
                ++deformationDataW;
            }
            ++scrollIndex;

            int fullLayerwidth = layerwidth << 7;
            if (chunkX < 0)
                chunkX += fullLayerwidth;
            if (chunkX >= fullLayerwidth)
                chunkX -= fullLayerwidth;

            int chunkXPos         = chunkX >> 7;
            int tilePxXPos        = chunkX & 0xF;
            int tileXPxRemain     = TILE_SIZE - tilePxXPos;
            int chunk             = (layer->tiles[(chunkX >> 7) + (chunkY << 8)] << 6) + ((chunkX & 0x7F) >> 4) + 8 * tileY;
            int tileOffsetY       = TILE_SIZE * tileY16;
            int tileOffsetYFlipX  = TILE_SIZE * tileY16 + 0xF;
            int tileOffsetYFlipY  = TILE_SIZE * (0xF - tileY16);
            int tileOffsetYFlipXY = TILE_SIZE * (0xF - tileY16) + 0xF;
            int lineRemain        = GFX_LINESIZE;

            byte *gfxDataPtr  = NULL;
            int tilePxLineCnt = tileXPxRemain;

            // Draw the first tile to the left
            if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                lineRemain -= tilePxLineCnt;
                switch (tiles128x128.direction[chunk]) {
                    case FLIP_NONE:
                        gfxDataPtr = &tilesetGFXData[tileOffsetY + tiles128x128.gfxDataPos[chunk] + tilePxXPos];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;
                        }
                        break;
                    case FLIP_X:

                        gfxDataPtr = &tilesetGFXData[tileOffsetYFlipX + tiles128x128.gfxDataPos[chunk] - tilePxXPos];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;
                        }
                        break;

                    case FLIP_Y:
                        gfxDataPtr = &tilesetGFXData[tileOffsetYFlipY + tiles128x128.gfxDataPos[chunk] + tilePxXPos];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;
                        }
                        break;

                    case FLIP_XY:
                        gfxDataPtr = &tilesetGFXData[tileOffsetYFlipXY + tiles128x128.gfxDataPos[chunk] - tilePxXPos];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;
                        }
                        break;
                    default: break;
                }
            }
            else {
                frameBufferPtr += tilePxLineCnt;
                lineRemain -= tilePxLineCnt;
            }

            // Draw the bulk of the tiles
            int chunkTileX   = ((chunkX & 0x7F) >> 4) + 1;
            int tilesPerLine = screenwidth16;
            while (tilesPerLine--) {
                if (chunkTileX < 8) {
                    ++chunk;
                }
                else {
                    if (++chunkXPos == layerwidth)
                        chunkXPos = 0;

                    chunkTileX = 0;
                    chunk      = (layer->tiles[chunkXPos + (chunkY << 8)] << 6) + 8 * tileY;
                }
                lineRemain -= TILE_SIZE;

                // Loop Unrolling (faster but messier code)
                if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                    switch (tiles128x128.direction[chunk]) {
                        case FLIP_NONE:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetY];
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            break;

                        case FLIP_X:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipX];
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            break;

                        case FLIP_Y:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipY];
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            ++gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            break;

                        case FLIP_XY:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipXY];
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            --gfxDataPtr;

                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            ++frameBufferPtr;
                            break;
                    }
                }
                else {
                    frameBufferPtr += TILE_SIZE;
                }
                ++chunkTileX;
            }

            // Draw any remaining tiles
            while (lineRemain > 0) {
                if (chunkTileX++ < 8) {
                    ++chunk;
                }
                else {
                    chunkTileX = 0;
                    if (++chunkXPos == layerwidth)
                        chunkXPos = 0;

                    chunk = (layer->tiles[chunkXPos + (chunkY << 8)] << 6) + 8 * tileY;
                }

                tilePxLineCnt = lineRemain >= TILE_SIZE ? TILE_SIZE : lineRemain;
                lineRemain -= tilePxLineCnt;
                if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                    switch (tiles128x128.direction[chunk]) {
                        case FLIP_NONE:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetY];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                            }
                            break;

                        case FLIP_X:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipX];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                            }
                            break;

                        case FLIP_Y:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipY];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                ++gfxDataPtr;
                            }
                            break;

                        case FLIP_XY:
                            gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetYFlipXY];
                            while (tilePxLineCnt--) {
                                if (*gfxDataPtr > 0)
                                    *frameBufferPtr = activePalette[*gfxDataPtr];
                                ++frameBufferPtr;
                                --gfxDataPtr;
                            }
                            break;

                        default: break;
                    }
                }
                else {
                    frameBufferPtr += tilePxLineCnt;
                }
            }

            if (++tileY16 >= TILE_SIZE) {
                tileY16 = 0;
                ++tileY;
            }

            if (tileY >= 8) {
                if (++chunkY == layerheight) {
                    chunkY = 0;
                    scrollIndex -= 0x80 * layerheight;
                }
                tileY = 0;
            }
        }
    }
#endif
}
void DrawVLineScrollLayer(int layerID)
{
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    if (!layer->xsize || !layer->ysize)
        return;
#if RETRO_SOFTWARE_RENDER
    int layerwidth     = layer->xsize;
    int layerheight    = layer->ysize;
    bool aboveMidPoint = layerID >= tLayerMidPoint;

    byte *lineScroll;
    int *deformationData;

    int xscrollOffset = 0;
    if (activeTileLayers[layerID]) { // BG Layer
        int xScroll        = xScrollOffset * layer->parallaxFactor >> 8;
        int fullLayerwidth = layerwidth << 7;
        layer->scrollPos += layer->scrollSpeed;
        if (layer->scrollPos > fullLayerwidth << 16)
            layer->scrollPos -= fullLayerwidth << 16;
        xscrollOffset   = (xScroll + (layer->scrollPos >> 16)) % fullLayerwidth;
        layerwidth      = fullLayerwidth >> 7;
        lineScroll      = layer->lineScroll;
        deformationData = &bgDeformationData2[(byte)(xscrollOffset + layer->deformationOffset)];
    }
    else { // FG Layer
        lastYSize            = layer->ysize;
        xscrollOffset        = xScrollOffset;
        lineScroll           = layer->lineScroll;
        vParallax.linePos[0] = yScrollOffset;
        vParallax.deform[0]  = true;
        deformationData      = &bgDeformationData0[(byte)(xScrollOffset + layer->deformationOffset)];
    }

    if (layer->type == LAYER_VSCROLL) {
        if (lastYSize != layerheight) {
            int fullLayerheight = layerheight << 7;
            for (int i = 0; i < vParallax.entryCount; ++i) {
                vParallax.linePos[i] = yScrollOffset * vParallax.parallaxFactor[i] >> 8;

                vParallax.scrollPos[i] += vParallax.scrollPos[i] << 16;
                if (vParallax.scrollPos[i] > fullLayerheight << 16)
                    vParallax.scrollPos[i] -= fullLayerheight << 16;

                vParallax.linePos[i] += vParallax.scrollPos[i] >> 16;
                vParallax.linePos[i] %= fullLayerheight;
            }
            layerheight = fullLayerheight >> 7;
        }
        lastYSize = layerheight;
    }

    ushort *frameBufferPtr = Engine.frameBuffer;
    activePalette          = fullPalette[gfxLineBuffer[0]];
    activePalette32        = fullPalette32[gfxLineBuffer[0]];
    int tileXPos           = xscrollOffset % (layerheight << 7);
    if (tileXPos < 0)
        tileXPos += layerheight << 7;
    byte *scrollIndex = &lineScroll[tileXPos];
    int chunkX        = tileXPos >> 7;
    int tileX16       = tileXPos & 0xF;
    int tileX         = (tileXPos & 0x7F) >> 4;

    // Draw Above Water (if applicable)
    int drawableLines = SCREEN_XSIZE;
    while (drawableLines--) {
        int chunkY = vParallax.linePos[*scrollIndex];
        if (vParallax.deform[*scrollIndex])
            chunkY += *deformationData;
        ++deformationData;
        ++scrollIndex;

        int fullLayerHeight = layerheight << 7;
        if (chunkY < 0)
            chunkY += fullLayerHeight;
        if (chunkY >= fullLayerHeight)
            chunkY -= fullLayerHeight;

        int chunkYPos         = chunkY >> 7;
        int tileY             = chunkY & 0xF;
        int tileYPxRemain     = TILE_SIZE - tileY;
        int chunk             = (layer->tiles[chunkX + (chunkY >> 7 << 8)] << 6) + tileX + 8 * ((chunkY & 0x7F) >> 4);
        int tileOffsetXFlipX  = 0xF - tileX16;
        int tileOffsetXFlipY  = tileX16 + SCREEN_YSIZE;
        int tileOffsetXFlipXY = 0xFF - tileX16;
        int lineRemain        = SCREEN_YSIZE;

        byte *gfxDataPtr  = NULL;
        int tilePxLineCnt = tileYPxRemain;

        // Draw the first tile to the left
        if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
            lineRemain -= tilePxLineCnt;
            switch (tiles128x128.direction[chunk]) {
                case FLIP_NONE:
                    gfxDataPtr = &tilesetGFXData[TILE_SIZE * tileY + tileX16 + tiles128x128.gfxDataPos[chunk]];
                    while (tilePxLineCnt--) {
                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;
                    }
                    break;

                case FLIP_X:
                    gfxDataPtr = &tilesetGFXData[TILE_SIZE * tileY + tileOffsetXFlipX + tiles128x128.gfxDataPos[chunk]];
                    while (tilePxLineCnt--) {
                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;
                    }
                    break;

                case FLIP_Y:
                    gfxDataPtr = &tilesetGFXData[tileOffsetXFlipY + tiles128x128.gfxDataPos[chunk] - TILE_SIZE * tileY];
                    while (tilePxLineCnt--) {
                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;
                    }
                    break;

                case FLIP_XY:
                    gfxDataPtr = &tilesetGFXData[tileOffsetXFlipXY + tiles128x128.gfxDataPos[chunk] - TILE_SIZE * tileY];
                    while (tilePxLineCnt--) {
                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;
                    }
                    break;

                default: break;
            }
        }
        else {
            frameBufferPtr += GFX_LINESIZE * tileYPxRemain;
            lineRemain -= tilePxLineCnt;
        }

        // Draw the bulk of the tiles
        int chunkTileY   = ((chunkY & 0x7F) >> 4) + 1;
        int tilesPerLine = (SCREEN_YSIZE >> 4) - 1;

        while (tilesPerLine--) {
            if (chunkTileY < 8) {
                chunk += 8;
            }
            else {
                if (++chunkYPos == layerheight)
                    chunkYPos = 0;

                chunkTileY = 0;
                chunk      = (layer->tiles[chunkX + (chunkYPos << 8)] << 6) + tileX;
            }
            lineRemain -= TILE_SIZE;

            // Loop Unrolling (faster but messier code)
            if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                switch (tiles128x128.direction[chunk]) {
                    case FLIP_NONE:
                        gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileX16];
                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        break;

                    case FLIP_X:
                        gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipX];
                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr += TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        break;

                    case FLIP_Y:
                        gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipY];
                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        break;

                    case FLIP_XY:
                        gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipXY];
                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        gfxDataPtr -= TILE_SIZE;

                        if (*gfxDataPtr > 0)
                            *frameBufferPtr = activePalette[*gfxDataPtr];
                        frameBufferPtr += GFX_LINESIZE;
                        break;
                }
            }
            else {
                frameBufferPtr += GFX_LINESIZE * TILE_SIZE;
            }
            ++chunkTileY;
        }

        // Draw any remaining tiles
        while (lineRemain > 0) {
            if (chunkTileY < 8) {
                chunk += 8;
            }
            else {
                if (++chunkYPos == layerheight)
                    chunkYPos = 0;

                chunkTileY = 0;
                chunk      = (layer->tiles[chunkX + (chunkYPos << 8)] << 6) + tileX;
            }

            tilePxLineCnt = lineRemain >= TILE_SIZE ? TILE_SIZE : lineRemain;
            lineRemain -= tilePxLineCnt;

            if (tiles128x128.visualPlane[chunk] == (byte)aboveMidPoint) {
                switch (tiles128x128.direction[chunk]) {
                    case FLIP_NONE:
                        gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileX16];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;
                        }
                        break;

                    case FLIP_X:
                        gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipX];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr += TILE_SIZE;
                        }
                        break;

                    case FLIP_Y:
                        gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipY];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;
                        }
                        break;

                    case FLIP_XY:
                        gfxDataPtr = &tilesetGFXData[tiles128x128.gfxDataPos[chunk] + tileOffsetXFlipXY];
                        while (tilePxLineCnt--) {
                            if (*gfxDataPtr > 0)
                                *frameBufferPtr = activePalette[*gfxDataPtr];
                            frameBufferPtr += GFX_LINESIZE;
                            gfxDataPtr -= TILE_SIZE;
                        }
                        break;

                    default: break;
                }
            }
            else {
                frameBufferPtr += GFX_LINESIZE * tilePxLineCnt;
            }
            chunkTileY++;
        }

        if (++tileX16 >= TILE_SIZE) {
            tileX16 = 0;
            ++tileX;
        }

        if (tileX >= 8) {
            if (++chunkX == layerwidth) {
                chunkX = 0;
                scrollIndex -= 0x80 * layerwidth;
            }
            tileX = 0;
        }

        frameBufferPtr -= GFX_FBUFFERMINUSONE;
    }
#endif
}
void Draw3DFloorLayer(int layerID)
{
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    if (!layer->xsize || !layer->ysize)
        return;

#if RETRO_SOFTWARE_RENDER
    int layerWidth         = layer->xsize << 7;
    int layerHeight        = layer->ysize << 7;
    int layerYPos          = layer->ypos;
    int layerZPos          = layer->zpos;
    int sinValue           = sinM7LookupTable[layer->angle];
    int cosValue           = cosM7LookupTable[layer->angle];
    byte *gfxLineBufferPtr = &gfxLineBuffer[(SCREEN_YSIZE / 2) + 12];
    ushort *frameBufferPtr = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
    int layerXPos          = layer->xpos >> 4;
    int ZBuffer            = layerZPos >> 4;
    for (int i = 4; i < 112; ++i) {
        if (!(i & 1)) {
            activePalette   = fullPalette[*gfxLineBufferPtr];
            activePalette32 = fullPalette32[*gfxLineBufferPtr];
            gfxLineBufferPtr++;
        }
        int XBuffer    = layerYPos / (i << 9) * -cosValue >> 8;
        int YBuffer    = sinValue * (layerYPos / (i << 9)) >> 8;
        int XPos       = layerXPos + (3 * sinValue * (layerYPos / (i << 9)) >> 2) - XBuffer * SCREEN_CENTERX;
        int YPos       = ZBuffer + (3 * cosValue * (layerYPos / (i << 9)) >> 2) - YBuffer * SCREEN_CENTERX;
        int lineBuffer = 0;
        while (lineBuffer < GFX_LINESIZE) {
            int tileX = XPos >> 12;
            int tileY = YPos >> 12;
            if (tileX > -1 && tileX < layerWidth && tileY > -1 && tileY < layerHeight) {
                int chunk       = tile3DFloorBuffer[(YPos >> 16 << 8) + (XPos >> 16)];
                byte *tilePixel = &tilesetGFXData[tiles128x128.gfxDataPos[chunk]];
                switch (tiles128x128.direction[chunk]) {
                    case FLIP_NONE: tilePixel += 16 * (tileY & 0xF) + (tileX & 0xF); break;
                    case FLIP_X: tilePixel += 16 * (tileY & 0xF) + 15 - (tileX & 0xF); break;
                    case FLIP_Y: tilePixel += (tileX & 0xF) + SCREEN_YSIZE - 16 * (tileY & 0xF); break;
                    case FLIP_XY: tilePixel += 15 - (tileX & 0xF) + SCREEN_YSIZE - 16 * (tileY & 0xF); break;
                    default: break;
                }

                if (*tilePixel > 0)
                    *frameBufferPtr = activePalette[*tilePixel];
            }
            ++frameBufferPtr;
            ++lineBuffer;
            XPos += XBuffer;
            YPos += YBuffer;
        }
    }
#endif
}
void Draw3DSkyLayer(int layerID)
{
    TileLayer *layer = &stageLayouts[activeTileLayers[layerID]];
    if (!layer->xsize || !layer->ysize)
        return;

#if RETRO_SOFTWARE_RENDER
    int layerWidth         = layer->xsize << 7;
    int layerHeight        = layer->ysize << 7;
    int layerYPos          = layer->ypos;
    int sinValue           = sinM7LookupTable[layer->angle & 0x1FF];
    int cosValue           = cosM7LookupTable[layer->angle & 0x1FF];
    ushort *frameBufferPtr = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
    ushort *bufferPtr      = Engine.frameBuffer2x;
    if (!drawStageGFXHQ)
        bufferPtr = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
    byte *gfxLineBufferPtr = &gfxLineBuffer[((SCREEN_YSIZE / 2) + 12)];
    int layerXPos          = layer->xpos >> 4;
    int layerZPos          = layer->zpos >> 4;
    for (int i = TILE_SIZE / 2; i < SCREEN_YSIZE - TILE_SIZE; ++i) {
        if (!(i & 1)) {
            activePalette   = fullPalette[*gfxLineBufferPtr];
            activePalette32 = fullPalette32[*gfxLineBufferPtr];
            gfxLineBufferPtr++;
        }
        int xBuffer    = layerYPos / (i << 8) * -cosValue >> 9;
        int yBuffer    = sinValue * (layerYPos / (i << 8)) >> 9;
        int XPos       = layerXPos + (3 * sinValue * (layerYPos / (i << 8)) >> 2) - xBuffer * GFX_LINESIZE;
        int YPos       = layerZPos + (3 * cosValue * (layerYPos / (i << 8)) >> 2) - yBuffer * GFX_LINESIZE;
        int lineBuffer = 0;
        while (lineBuffer < GFX_LINESIZE * 2) {
            int tileX = XPos >> 12;
            int tileY = YPos >> 12;
            if (tileX > -1 && tileX < layerWidth && tileY > -1 && tileY < layerHeight) {
                int chunk       = tile3DFloorBuffer[(YPos >> 16 << 8) + (XPos >> 16)];
                byte *tilePixel = &tilesetGFXData[tiles128x128.gfxDataPos[chunk]];
                switch (tiles128x128.direction[chunk]) {
                    case FLIP_NONE: tilePixel += TILE_SIZE * (tileY & 0xF) + (tileX & 0xF); break;
                    case FLIP_X: tilePixel += TILE_SIZE * (tileY & 0xF) + 0xF - (tileX & 0xF); break;
                    case FLIP_Y: tilePixel += (tileX & 0xF) + SCREEN_YSIZE - TILE_SIZE * (tileY & 0xF); break;
                    case FLIP_XY: tilePixel += 0xF - (tileX & 0xF) + SCREEN_YSIZE - TILE_SIZE * (tileY & 0xF); break;
                    default: break;
                }

                if (*tilePixel > 0)
                    *bufferPtr = activePalette[*tilePixel];
                else if (drawStageGFXHQ)
                    *bufferPtr = *frameBufferPtr;
            }
            else if (drawStageGFXHQ) {
                *bufferPtr = *frameBufferPtr;
            }

            if (lineBuffer & 1)
                ++frameBufferPtr;

            if (drawStageGFXHQ)
                bufferPtr++;
            else if (lineBuffer & 1)
                ++bufferPtr;

            lineBuffer++;
            XPos += xBuffer;
            YPos += yBuffer;
        }

        if (!(i & 1))
            frameBufferPtr -= GFX_LINESIZE;

        if (!(i & 1) && !drawStageGFXHQ)
            bufferPtr -= GFX_LINESIZE;
    }

    if (drawStageGFXHQ) {
        frameBufferPtr = &Engine.frameBuffer[((SCREEN_YSIZE / 2) + 12) * GFX_LINESIZE];
        int cnt        = ((SCREEN_YSIZE / 2) - 12) * GFX_LINESIZE;
        while (cnt--) *frameBufferPtr++ = 0xF81F; // Magenta
    }
#endif
}

void DrawRectangle(int XPos, int YPos, int width, int height, int R, int G, int B, int A)
{
    if (A > 0xFF)
        A = 0xFF;
#if RETRO_SOFTWARE_RENDER
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        width += XPos;
        XPos = 0;
    }

    if (height + YPos > SCREEN_YSIZE)
        height = SCREEN_YSIZE - YPos;
    if (YPos < 0) {
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0 || A <= 0)
        return;
    int pitch              = GFX_LINESIZE - width;
    ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
    ushort clr             = PACK_RGB888(R, G, B);
    if (A == 0xFF) {
        int h = height;
        while (h--) {
            int w = width;
            while (w--) {
                *frameBufferPtr = clr;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
        }
    }
    else {
        ushort *fbufferBlend = &blendLookupTable[0x20 * (0xFF - A)];
        ushort *pixelBlend   = &blendLookupTable[0x20 * A];

        int h = height;
        while (h--) {
            int w = width;
            while (w--) {
                int R = (fbufferBlend[(*frameBufferPtr & 0xF800) >> 11] + pixelBlend[(clr & 0xF800) >> 11]) << 11;
                int G = (fbufferBlend[(*frameBufferPtr & 0x7E0) >> 6] + pixelBlend[(clr & 0x7E0) >> 6]) << 6;
                int B = fbufferBlend[*frameBufferPtr & 0x1F] + pixelBlend[clr & 0x1F];

                *frameBufferPtr = R | G | B;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
        }
    }
#endif
}

static inline void ApplyClassicTintChannel(int &channel, int target, int &remaining)
{
    if (remaining <= 0)
        return;

    int difference = target - channel;
    if (difference == 0)
        return;

    int magnitude = difference > 0 ? difference : -difference;
    if (magnitude >= remaining) {
        channel += difference > 0 ? remaining : -remaining;
        remaining = 0;
    }
    else {
        channel = target;
        remaining -= magnitude;
    }
}

void DrawClassicFade(int XPos, int YPos, int width, int height, int R, int G, int B, int A)
{
#if RETRO_SOFTWARE_RENDER
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        width += XPos;
        XPos = 0;
    }

    if (height + YPos > SCREEN_YSIZE)
        height = SCREEN_YSIZE - YPos;
    if (YPos < 0) {
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0 || A <= 0)
        return;
    if (A > 0xFF)
        A = 0xFF;

    int classicStrength = (A * 3) >> 3;
    int pitch = GFX_LINESIZE - width;
    ushort target = PACK_RGB888(R, G, B);
    int targetR = (target >> 11) & 0x1F;
    int targetG = (target >> 5) & 0x3F;
    int targetB = target & 0x1F;
    ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];

    int h = height;
    while (h--) {
        int w = width;
        while (w--) {
            ushort source = *frameBufferPtr;
            int sourceR = (source >> 11) & 0x1F;
            int sourceG = (source >> 5) & 0x3F;
            int sourceB = source & 0x1F;

            int remaining = classicStrength;
            ApplyClassicTintChannel(sourceR, targetR, remaining);
            ApplyClassicTintChannel(sourceG, targetG, remaining);
            ApplyClassicTintChannel(sourceB, targetB, remaining);
            *frameBufferPtr++ = (ushort)((sourceR << 11) | (sourceG << 5) | sourceB);
        }
        frameBufferPtr += pitch;
    }
#endif
}

void SetFadeHQ(int R, int G, int B, int A)
{
#if RETRO_SOFTWARE_RENDER
    if (A <= 0)
        return;
    if (A > 0xFF)
        A = 0xFF;
    int pitch              = GFX_LINESIZE * 2;
    ushort *frameBufferPtr = Engine.frameBuffer2x;
    ushort clr             = PACK_RGB888(R, G, B);
    if (A == 0xFF) {
        int h = SCREEN_YSIZE;
        while (h--) {
            int w = pitch;
            while (w--) {
                *frameBufferPtr = clr;
                ++frameBufferPtr;
            }
        }
    }
    else {
        ushort *fbufferBlend = &blendLookupTable[0x20 * (0xFF - A)];
        ushort *pixelBlend   = &blendLookupTable[0x20 * A];

        int h = SCREEN_YSIZE;
        while (h--) {
            int w = pitch;
            while (w--) {
                int R = (fbufferBlend[(*frameBufferPtr & 0xF800) >> 11] + pixelBlend[(clr & 0xF800) >> 11]) << 11;
                int G = (fbufferBlend[(*frameBufferPtr & 0x7E0) >> 6] + pixelBlend[(clr & 0x7E0) >> 6]) << 6;
                int B = fbufferBlend[*frameBufferPtr & 0x1F] + pixelBlend[clr & 0x1F];

                *frameBufferPtr = R | G | B;
                ++frameBufferPtr;
            }
        }
    }
#endif
}

void DrawTintRectangle(int XPos, int YPos, int width, int height)
{
#if RETRO_SOFTWARE_RENDER
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        width += XPos;
        XPos = 0;
    }

    if (height + YPos > SCREEN_YSIZE)
        height = SCREEN_YSIZE - YPos;
    if (YPos < 0) {
        height += YPos;
        YPos = 0;
    }
	
    if (width < 0 || height < 0)
        return;
	
    int yOffset = GFX_LINESIZE - width;
    for (ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];; frameBufferPtr += yOffset) {
        height--;
        if (height < 0)
            break;
        int w = width;
        while (w--) {
            *frameBufferPtr = tintLookupTable[*frameBufferPtr];
            ++frameBufferPtr;
        }
    }
#endif
}
void DrawScaledTintMask(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX, int sprY,
                        int sheetID)
{
#if RETRO_SOFTWARE_RENDER
    int roundedYPos = 0;
    int roundedXPos = 0;
    int truescaleX  = 4 * scaleX;
    int truescaleY  = 4 * scaleY;
    int widthM1     = width - 1;
    int trueXPos    = XPos - (truescaleX * pivotX >> 11);
    width           = truescaleX * width >> 11;
    int trueYPos    = YPos - (truescaleY * pivotY >> 11);
    height          = truescaleY * height >> 11;
    int finalscaleX = (signed int)(float)((float)(2048.0 / (float)truescaleX) * 2048.0);
    int finalscaleY = (signed int)(float)((float)(2048.0 / (float)truescaleY) * 2048.0);
    if (width + trueXPos > GFX_LINESIZE) {
        width = GFX_LINESIZE - trueXPos;
    }

    if (direction) {
        if (trueXPos < 0) {
            widthM1 -= trueXPos * -finalscaleX >> 11;
            roundedXPos = (ushort)trueXPos * -(short)finalscaleX & 0x7FF;
            width += trueXPos;
            trueXPos = 0;
        }
    }
    else if (trueXPos < 0) {
        sprX += trueXPos * -finalscaleX >> 11;
        roundedXPos = (ushort)trueXPos * -(short)finalscaleX & 0x7FF;
        width += trueXPos;
        trueXPos = 0;
    }

    if (height + trueYPos > SCREEN_YSIZE) {
        height = SCREEN_YSIZE - trueYPos;
    }
    if (trueYPos < 0) {
        sprY += trueYPos * -finalscaleY >> 11;
        roundedYPos = (ushort)trueYPos * -(short)finalscaleY & 0x7FF;
        height += trueYPos;
        trueYPos = 0;
    }

    if (width <= 0 || height <= 0)
        return;

    GFXSurface *surface = &gfxSurface[sheetID];
    int pitch           = GFX_LINESIZE - width;
    int gfxwidth        = surface->width;
    // byte *lineBuffer       = &gfxLineBuffer[trueYPos];
    byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
    ushort *frameBufferPtr = &Engine.frameBuffer[trueXPos + GFX_LINESIZE * trueYPos];
    if (direction == FLIP_X) {
        byte *gfxDataPtr = &gfxData[widthM1];
        int gfxPitch     = 0;
        while (height--) {
            int roundXPos = roundedXPos;
            int w         = width;
            while (w--) {
                if (*gfxDataPtr > 0)
                    *frameBufferPtr = tintLookupTable[*frameBufferPtr];
                int offsetX = finalscaleX + roundXPos;
                gfxDataPtr -= offsetX >> 11;
                gfxPitch += offsetX >> 11;
                roundXPos = offsetX & 0x7FF;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            int offsetY = finalscaleY + roundedYPos;
            gfxDataPtr += gfxPitch + (offsetY >> 11) * gfxwidth;
            roundedYPos = offsetY & 0x7FF;
            gfxPitch    = 0;
        }
    }
    else {
        int gfxPitch = 0;
        int h        = height;
        while (h--) {
            int roundXPos = roundedXPos;
            int w         = width;
            while (w--) {
                if (*gfxData > 0)
                    *frameBufferPtr = tintLookupTable[*frameBufferPtr];
                int offsetX = finalscaleX + roundXPos;
                gfxData += offsetX >> 11;
                gfxPitch += offsetX >> 11;
                roundXPos = offsetX & 0x7FF;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            int offsetY = finalscaleY + roundedYPos;
            gfxData += (offsetY >> 11) * gfxwidth - gfxPitch;
            roundedYPos = offsetY & 0x7FF;
            gfxPitch    = 0;
        }
    }
#endif
}

void DrawSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int sheetID)
{
#if RETRO_SOFTWARE_RENDER
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        sprX -= XPos;
        width += XPos;
        XPos = 0;
    }
    if (height + YPos > SCREEN_YSIZE)
        height = SCREEN_YSIZE - YPos;
    if (YPos < 0) {
        sprY -= YPos;
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0)
        return;

    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - width;
    int gfxPitch           = surface->width - width;
    byte *lineBuffer       = &gfxLineBuffer[YPos];
    byte *gfxDataPtr       = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
    ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
    while (height--) {
        activePalette   = fullPalette[*lineBuffer];
        activePalette32 = fullPalette32[*lineBuffer];
        lineBuffer++;
        int w = width;
        while (w--) {
            if (*gfxDataPtr > 0)
                *frameBufferPtr = activePalette[*gfxDataPtr];
            ++gfxDataPtr;
            ++frameBufferPtr;
        }
        frameBufferPtr += pitch;
        gfxDataPtr += gfxPitch;
    }
#endif
}

#if RETRO_REV00
void DrawSpriteClipped(int XPos, int YPos, int width, int height, int sprX, int sprY, int sheetID, int clipY)
{
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        sprX -= XPos;
        width += XPos;
        XPos = 0;
    }
    if (height + YPos > clipY)
        height = clipY - YPos;
    if (YPos < 0) {
        sprY -= YPos;
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0)
        return;

    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - width;
    int gfxPitch           = surface->width - width;
    byte *lineBuffer       = &gfxLineBuffer[YPos];
    byte *gfxDataPtr       = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
    ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
    while (height--) {
        activePalette   = fullPalette[*lineBuffer];
        activePalette32 = fullPalette32[*lineBuffer];
        lineBuffer++;
        int w = width;
        while (w--) {
            if (*gfxDataPtr > 0)
                *frameBufferPtr = activePalette[*gfxDataPtr];
            ++gfxDataPtr;
            ++frameBufferPtr;
        }
        frameBufferPtr += pitch;
        gfxDataPtr += gfxPitch;
    }
}
#endif

void DrawSpriteFlipped(int XPos, int YPos, int width, int height, int sprX, int sprY, int direction, int sheetID)
{
#if RETRO_SOFTWARE_RENDER
    int widthFlip  = width;
    int heightFlip = height;

    if (width + XPos > GFX_LINESIZE) {
        width = GFX_LINESIZE - XPos;
    }
    if (XPos < 0) {
        sprX -= XPos;
        width += XPos;
        widthFlip += XPos + XPos;
        XPos = 0;
    }
    if (height + YPos > SCREEN_YSIZE) {
        height = SCREEN_YSIZE - YPos;
    }
    if (YPos < 0) {
        sprY -= YPos;
        height += YPos;
        heightFlip += YPos + YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0)
        return;

    GFXSurface *surface = &gfxSurface[sheetID];
    int pitch;
    int gfxPitch;
    byte *lineBuffer;
    byte *gfxData;
    ushort *frameBufferPtr;
    switch (direction) {
        case FLIP_NONE:
            pitch          = GFX_LINESIZE - width;
            gfxPitch       = surface->width - width;
            lineBuffer     = &gfxLineBuffer[YPos];
            gfxData        = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
            frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];

            while (height--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int w = width;
                while (w--) {
                    if (*gfxData > 0)
                        *frameBufferPtr = activePalette[*gfxData];
                    ++gfxData;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                gfxData += gfxPitch;
            }
            break;
        case FLIP_X:
            pitch          = GFX_LINESIZE - width;
            gfxPitch       = width + surface->width;
            lineBuffer     = &gfxLineBuffer[YPos];
            gfxData        = &graphicData[widthFlip - 1 + sprX + surface->width * sprY + surface->dataPosition];
            frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
            while (height--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int w = width;
                while (w--) {
                    if (*gfxData > 0)
                        *frameBufferPtr = activePalette[*gfxData];
                    --gfxData;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                gfxData += gfxPitch;
            }
            break;
        case FLIP_Y:
            pitch          = GFX_LINESIZE - width;
            gfxPitch       = width + surface->width;
            lineBuffer     = &gfxLineBuffer[YPos];
            gfxData        = &graphicData[sprX + surface->width * (sprY + heightFlip - 1) + surface->dataPosition];
            frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
            while (height--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int w = width;
                while (w--) {
                    if (*gfxData > 0)
                        *frameBufferPtr = activePalette[*gfxData];
                    ++gfxData;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                gfxData -= gfxPitch;
            }
            break;
        case FLIP_XY:
            pitch          = GFX_LINESIZE - width;
            gfxPitch       = surface->width - width;
            lineBuffer     = &gfxLineBuffer[YPos];
            gfxData        = &graphicData[widthFlip - 1 + sprX + surface->width * (sprY + heightFlip - 1) + surface->dataPosition];
            frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
            while (height--) {
                activePalette   = fullPalette[*lineBuffer];
                activePalette32 = fullPalette32[*lineBuffer];
                lineBuffer++;
                int w = width;
                while (w--) {
                    if (*gfxData > 0)
                        *frameBufferPtr = activePalette[*gfxData];
                    --gfxData;
                    ++frameBufferPtr;
                }
                frameBufferPtr += pitch;
                gfxData -= gfxPitch;
            }
            break;
        default: break;
    }
#endif
}
void DrawSpriteScaled(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX, int sprY,
                      int sheetID)
{
#if RETRO_SOFTWARE_RENDER
    int roundedYPos = 0;
    int roundedXPos = 0;
    int truescaleX  = 4 * scaleX;
    int truescaleY  = 4 * scaleY;
    int widthM1     = width - 1;
    int trueXPos    = XPos - (truescaleX * pivotX >> 11);
    width           = truescaleX * width >> 11;
    int trueYPos    = YPos - (truescaleY * pivotY >> 11);
    height          = truescaleY * height >> 11;
    int finalscaleX = (signed int)(float)((float)(2048.0 / (float)truescaleX) * 2048.0);
    int finalscaleY = (signed int)(float)((float)(2048.0 / (float)truescaleY) * 2048.0);
    if (width + trueXPos > GFX_LINESIZE) {
        width = GFX_LINESIZE - trueXPos;
    }

    if (direction) {
        if (trueXPos < 0) {
            widthM1 -= trueXPos * -finalscaleX >> 11;
            roundedXPos = (ushort)trueXPos * -(short)finalscaleX & 0x7FF;
            width += trueXPos;
            trueXPos = 0;
        }
    }
    else if (trueXPos < 0) {
        sprX += trueXPos * -finalscaleX >> 11;
        roundedXPos = (ushort)trueXPos * -(short)finalscaleX & 0x7FF;
        width += trueXPos;
        trueXPos = 0;
    }

    if (height + trueYPos > SCREEN_YSIZE) {
        height = SCREEN_YSIZE - trueYPos;
    }
    if (trueYPos < 0) {
        sprY += trueYPos * -finalscaleY >> 11;
        roundedYPos = (ushort)trueYPos * -(short)finalscaleY & 0x7FF;
        height += trueYPos;
        trueYPos = 0;
    }

    if (width <= 0 || height <= 0)
        return;

    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - width;
    int gfxwidth           = surface->width;
    byte *lineBuffer       = &gfxLineBuffer[trueYPos];
    byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
    ushort *frameBufferPtr = &Engine.frameBuffer[trueXPos + GFX_LINESIZE * trueYPos];
    if (direction == FLIP_X) {
        byte *gfxDataPtr = &gfxData[widthM1];
        int gfxPitch     = 0;
        while (height--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int roundXPos = roundedXPos;
            int w         = width;
            while (w--) {
                if (*gfxDataPtr > 0)
                    *frameBufferPtr = activePalette[*gfxDataPtr];
                int offsetX = finalscaleX + roundXPos;
                gfxDataPtr -= offsetX >> 11;
                gfxPitch += offsetX >> 11;
                roundXPos = offsetX & 0x7FF;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            int offsetY = finalscaleY + roundedYPos;
            gfxDataPtr += gfxPitch + (offsetY >> 11) * gfxwidth;
            roundedYPos = offsetY & 0x7FF;
            gfxPitch    = 0;
        }
    }
    else {
        int gfxPitch = 0;
        int h        = height;
        while (h--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int roundXPos = roundedXPos;
            int w         = width;
            while (w--) {
                if (*gfxData > 0)
                    *frameBufferPtr = activePalette[*gfxData];
                int offsetX = finalscaleX + roundXPos;
                gfxData += offsetX >> 11;
                gfxPitch += offsetX >> 11;
                roundXPos = offsetX & 0x7FF;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            int offsetY = finalscaleY + roundedYPos;
            gfxData += (offsetY >> 11) * gfxwidth - gfxPitch;
            roundedYPos = offsetY & 0x7FF;
            gfxPitch    = 0;
        }
    }
#endif
}
#if !RETRO_REV02
void DrawScaledChar(int direction, int XPos, int YPos, int pivotX, int pivotY, int scaleX, int scaleY, int width, int height, int sprX, int sprY,
                    int sheetID)
{
#if RETRO_SOFTWARE_RENDER
    // Not avaliable in SW Render mode
#endif
}
#endif
void DrawSpriteRotated(int direction, int XPos, int YPos, int pivotX, int pivotY, int sprX, int sprY, int width, int height, int rotation,
                       int sheetID)
{
#if RETRO_SOFTWARE_RENDER
    int sprXPos    = (pivotX + sprX) << 9;
    int sprYPos    = (pivotY + sprY) << 9;
    int fullwidth  = width + sprX;
    int fullheight = height + sprY;
    int angle      = rotation & 0x1FF;
    if (angle < 0)
        angle += 0x200;
    if (angle)
        angle = 0x200 - angle;
    int sine   = sin512LookupTable[angle];
    int cosine = cos512LookupTable[angle];
    int xPositions[4];
    int yPositions[4];

    if (direction == FLIP_X) {
        xPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX + 2)) >> 9);
        yPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX + 2)) >> 9);
        xPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX - width - 2)) >> 9);
        yPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX - width - 2)) >> 9);
        xPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (pivotX + 2)) >> 9);
        yPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (pivotX + 2)) >> 9);
        int a         = pivotX - width - 2;
        int b         = height - pivotY + 2;
        xPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
        yPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
    }
    else {
        xPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (-pivotX - 2)) >> 9);
        yPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (-pivotX - 2)) >> 9);
        xPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (width - pivotX + 2)) >> 9);
        yPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (width - pivotX + 2)) >> 9);
        xPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (-pivotX - 2)) >> 9);
        yPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (-pivotX - 2)) >> 9);
        int a         = width - pivotX + 2;
        int b         = height - pivotY + 2;
        xPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
        yPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
    }

    int left = GFX_LINESIZE;
    for (int i = 0; i < 4; ++i) {
        if (xPositions[i] < left)
            left = xPositions[i];
    }
    if (left < 0)
        left = 0;

    int right = 0;
    for (int i = 0; i < 4; ++i) {
        if (xPositions[i] > right)
            right = xPositions[i];
    }
    if (right > GFX_LINESIZE)
        right = GFX_LINESIZE;
    int maxX = right - left;

    int top = SCREEN_YSIZE;
    for (int i = 0; i < 4; ++i) {
        if (yPositions[i] < top)
            top = yPositions[i];
    }
    if (top < 0)
        top = 0;

    int bottom = 0;
    for (int i = 0; i < 4; ++i) {
        if (yPositions[i] > bottom)
            bottom = yPositions[i];
    }
    if (bottom > SCREEN_YSIZE)
        bottom = SCREEN_YSIZE;
    int maxY = bottom - top;

    if (maxX <= 0 || maxY <= 0)
        return;

    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - maxX;
    int lineSize           = surface->widthShift;
    ushort *frameBufferPtr = &Engine.frameBuffer[left + GFX_LINESIZE * top];
    byte *lineBuffer       = &gfxLineBuffer[top];
    int startX             = left - XPos;
    int startY             = top - YPos;
    int shiftPivot         = (sprX << 9) - 1;
    fullwidth <<= 9;
    int shiftheight = (sprY << 9) - 1;
    fullheight <<= 9;
    byte *gfxData = &graphicData[surface->dataPosition];
    if (cosine < 0 || sine < 0)
        sprYPos += sine + cosine;

    if (direction == FLIP_X) {
        int drawX = sprXPos - (cosine * startX - sine * startY) - 0x100;
        int drawY = cosine * startY + sprYPos + sine * startX;
        while (maxY--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int finalX = drawX;
            int finalY = drawY;
            int w      = maxX;
            while (w--) {
                if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) {
                    byte index = gfxData[(finalY >> 9 << lineSize) + (finalX >> 9)];
                    if (index > 0)
                        *frameBufferPtr = activePalette[index];
                }
                ++frameBufferPtr;
                finalX -= cosine;
                finalY += sine;
            }
            drawX += sine;
            drawY += cosine;
            frameBufferPtr += pitch;
        }
    }
    else {
        int drawX = sprXPos + cosine * startX - sine * startY;
        int drawY = cosine * startY + sprYPos + sine * startX;
        while (maxY--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int finalX = drawX;
            int finalY = drawY;
            int w      = maxX;
            while (w--) {
                if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) {
                    byte index = gfxData[(finalY >> 9 << lineSize) + (finalX >> 9)];
                    if (index > 0)
                        *frameBufferPtr = activePalette[index];
                }
                ++frameBufferPtr;
                finalX += cosine;
                finalY += sine;
            }
            drawX -= sine;
            drawY += cosine;
            frameBufferPtr += pitch;
        }
    }
#endif
}

void DrawSpriteRotozoom(int direction, int XPos, int YPos, int pivotX, int pivotY, int sprX, int sprY, int width, int height, int rotation, int scale,
                        int sheetID)
{
#if RETRO_SOFTWARE_RENDER
    if (scale == 0)
        return;

    int sprXPos    = (pivotX + sprX) << 9;
    int sprYPos    = (pivotY + sprY) << 9;
    int fullwidth  = width + sprX;
    int fullheight = height + sprY;
    int angle      = rotation & 0x1FF;
    if (angle < 0)
        angle += 0x200;
    if (angle)
        angle = 0x200 - angle;
    int sine   = scale * sin512LookupTable[angle] >> 9;
    int cosine = scale * cos512LookupTable[angle] >> 9;
    int xPositions[4];
    int yPositions[4];

    if (direction == FLIP_X) {
        xPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX + 2)) >> 9);
        yPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX + 2)) >> 9);
        xPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (pivotX - width - 2)) >> 9);
        yPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (pivotX - width - 2)) >> 9);
        xPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (pivotX + 2)) >> 9);
        yPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (pivotX + 2)) >> 9);
        int a         = pivotX - width - 2;
        int b         = height - pivotY + 2;
        xPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
        yPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
    }
    else {
        xPositions[0] = XPos + ((sine * (-pivotY - 2) + cosine * (-pivotX - 2)) >> 9);
        yPositions[0] = YPos + ((cosine * (-pivotY - 2) - sine * (-pivotX - 2)) >> 9);
        xPositions[1] = XPos + ((sine * (-pivotY - 2) + cosine * (width - pivotX + 2)) >> 9);
        yPositions[1] = YPos + ((cosine * (-pivotY - 2) - sine * (width - pivotX + 2)) >> 9);
        xPositions[2] = XPos + ((sine * (height - pivotY + 2) + cosine * (-pivotX - 2)) >> 9);
        yPositions[2] = YPos + ((cosine * (height - pivotY + 2) - sine * (-pivotX - 2)) >> 9);
        int a         = width - pivotX + 2;
        int b         = height - pivotY + 2;
        xPositions[3] = XPos + ((sine * b + cosine * a) >> 9);
        yPositions[3] = YPos + ((cosine * b - sine * a) >> 9);
    }
    int truescale = (signed int)(float)((float)(512.0 / (float)scale) * 512.0);
    sine          = truescale * sin512LookupTable[angle] >> 9;
    cosine        = truescale * cos512LookupTable[angle] >> 9;

    int left = GFX_LINESIZE;
    for (int i = 0; i < 4; ++i) {
        if (xPositions[i] < left)
            left = xPositions[i];
    }
    if (left < 0)
        left = 0;

    int right = 0;
    for (int i = 0; i < 4; ++i) {
        if (xPositions[i] > right)
            right = xPositions[i];
    }
    if (right > GFX_LINESIZE)
        right = GFX_LINESIZE;
    int maxX = right - left;

    int top = SCREEN_YSIZE;
    for (int i = 0; i < 4; ++i) {
        if (yPositions[i] < top)
            top = yPositions[i];
    }
    if (top < 0)
        top = 0;

    int bottom = 0;
    for (int i = 0; i < 4; ++i) {
        if (yPositions[i] > bottom)
            bottom = yPositions[i];
    }
    if (bottom > SCREEN_YSIZE)
        bottom = SCREEN_YSIZE;
    int maxY = bottom - top;

    if (maxX <= 0 || maxY <= 0)
        return;

    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - maxX;
    int lineSize           = surface->widthShift;
    ushort *frameBufferPtr = &Engine.frameBuffer[left + GFX_LINESIZE * top];
    byte *lineBuffer       = &gfxLineBuffer[top];
    int startX             = left - XPos;
    int startY             = top - YPos;
    int shiftPivot         = (sprX << 9) - 1;
    fullwidth <<= 9;
    int shiftheight = (sprY << 9) - 1;
    fullheight <<= 9;
    byte *gfxData = &graphicData[surface->dataPosition];
    if (cosine < 0 || sine < 0)
        sprYPos += sine + cosine;

    if (direction == FLIP_X) {
        int drawX = sprXPos - (cosine * startX - sine * startY) - (truescale >> 1);
        int drawY = cosine * startY + sprYPos + sine * startX;
        while (maxY--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int finalX = drawX;
            int finalY = drawY;
            int w      = maxX;
            while (w--) {
                if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) {
                    byte index = gfxData[(finalY >> 9 << lineSize) + (finalX >> 9)];
                    if (index > 0)
                        *frameBufferPtr = activePalette[index];
                }
                ++frameBufferPtr;
                finalX -= cosine;
                finalY += sine;
            }
            drawX += sine;
            drawY += cosine;
            frameBufferPtr += pitch;
        }
    }
    else {
        int drawX = sprXPos + cosine * startX - sine * startY;
        int drawY = cosine * startY + sprYPos + sine * startX;
        while (maxY--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int finalX = drawX;
            int finalY = drawY;
            int w      = maxX;
            while (w--) {
                if (finalX > shiftPivot && finalX < fullwidth && finalY > shiftheight && finalY < fullheight) {
                    byte index = gfxData[(finalY >> 9 << lineSize) + (finalX >> 9)];
                    if (index > 0)
                        *frameBufferPtr = activePalette[index];
                }
                ++frameBufferPtr;
                finalX += cosine;
                finalY += sine;
            }
            drawX -= sine;
            drawY += cosine;
            frameBufferPtr += pitch;
        }
    }
#endif
}

void DrawBlendedSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int sheetID)
{
#if RETRO_SOFTWARE_RENDER
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        sprX -= XPos;
        width += XPos;
        XPos = 0;
    }
    if (height + YPos > SCREEN_YSIZE)
        height = SCREEN_YSIZE - YPos;
    if (YPos < 0) {
        sprY -= YPos;
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0)
        return;

    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - width;
    int gfxPitch           = surface->width - width;
    byte *lineBuffer       = &gfxLineBuffer[YPos];
    byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
    ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
    while (height--) {
        activePalette   = fullPalette[*lineBuffer];
        activePalette32 = fullPalette32[*lineBuffer];
        lineBuffer++;
        int w = width;
        while (w--) {
            if (*gfxData > 0)
                *frameBufferPtr = ((activePalette[*gfxData] & 0xF7DE) >> 1) + ((*frameBufferPtr & 0xF7DE) >> 1);
            ++gfxData;
            ++frameBufferPtr;
        }
        frameBufferPtr += pitch;
        gfxData += gfxPitch;
    }
#endif
}
void DrawAlphaBlendedSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int alpha, int sheetID)
{
    if (alpha > 0xFF)
        alpha = 0xFF;
#if RETRO_SOFTWARE_RENDER
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        sprX -= XPos;
        width += XPos;
        XPos = 0;
    }
    if (height + YPos > SCREEN_YSIZE)
        height = SCREEN_YSIZE - YPos;
    if (YPos < 0) {
        sprY -= YPos;
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0 || alpha <= 0)
        return;

    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - width;
    int gfxPitch           = surface->width - width;
    byte *lineBuffer       = &gfxLineBuffer[YPos];
    byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
    ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];
    if (alpha == 0xFF) {
        while (height--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int w = width;
            while (w--) {
                if (*gfxData > 0)
                    *frameBufferPtr = activePalette[*gfxData];
                ++gfxData;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            gfxData += gfxPitch;
        }
    }
    else {
        ushort *fbufferBlend = &blendLookupTable[0x20 * (0xFF - alpha)];
        ushort *pixelBlend   = &blendLookupTable[0x20 * alpha];

        while (height--) {
            activePalette   = fullPalette[*lineBuffer];
            activePalette32 = fullPalette32[*lineBuffer];
            lineBuffer++;
            int w = width;
            while (w--) {
                if (*gfxData > 0) {
                    ushort color = activePalette[*gfxData];

                    int R = (fbufferBlend[(*frameBufferPtr & 0xF800) >> 11] + pixelBlend[(color & 0xF800) >> 11]) << 11;
                    int G = (fbufferBlend[(*frameBufferPtr & 0x7E0) >> 6] + pixelBlend[(color & 0x7E0) >> 6]) << 6;
                    int B = fbufferBlend[*frameBufferPtr & 0x1F] + pixelBlend[color & 0x1F];

                    *frameBufferPtr = R | G | B;
                }
                ++gfxData;
                ++frameBufferPtr;
            }
            frameBufferPtr += pitch;
            gfxData += gfxPitch;
        }
    }
#endif
}
void DrawAdditiveBlendedSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int alpha, int sheetID)
{
    if (alpha > 0xFF)
        alpha = 0xFF;
#if RETRO_SOFTWARE_RENDER
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        sprX -= XPos;
        width += XPos;
        XPos = 0;
    }
    if (height + YPos > SCREEN_YSIZE)
        height = SCREEN_YSIZE - YPos;
    if (YPos < 0) {
        sprY -= YPos;
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0 || alpha <= 0)
        return;

    ushort *blendTablePtr  = &blendLookupTable[0x20 * alpha];
    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - width;
    int gfxPitch           = surface->width - width;
    byte *lineBuffer       = &gfxLineBuffer[YPos];
    byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
    ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];

    while (height--) {
        activePalette   = fullPalette[*lineBuffer];
        activePalette32 = fullPalette32[*lineBuffer];
        lineBuffer++;
        int w = width;
        while (w--) {
            if (*gfxData > 0) {
                ushort color = activePalette[*gfxData];

                int R = minVal((blendTablePtr[(color & 0xF800) >> 11] << 11) + (*frameBufferPtr & 0xF800), 0xF800);
                int G = minVal((blendTablePtr[(color & 0x7E0) >> 6] << 6) + (*frameBufferPtr & 0x7E0), 0x7E0);
                int B = minVal(blendTablePtr[color & 0x1F] + (*frameBufferPtr & 0x1F), 0x1F);

                *frameBufferPtr = R | G | B;
            }
            ++gfxData;
            ++frameBufferPtr;
        }
        frameBufferPtr += pitch;
        gfxData += gfxPitch;
    }
#endif
}
void DrawSubtractiveBlendedSprite(int XPos, int YPos, int width, int height, int sprX, int sprY, int alpha, int sheetID)
{
    if (alpha > 0xFF)
        alpha = 0xFF;

#if RETRO_SOFTWARE_RENDER
    if (width + XPos > GFX_LINESIZE)
        width = GFX_LINESIZE - XPos;
    if (XPos < 0) {
        sprX -= XPos;
        width += XPos;
        XPos = 0;
    }
    if (height + YPos > SCREEN_YSIZE)
        height = SCREEN_YSIZE - YPos;
    if (YPos < 0) {
        sprY -= YPos;
        height += YPos;
        YPos = 0;
    }
    if (width <= 0 || height <= 0 || alpha <= 0)
        return;

    ushort *subBlendTable  = &subtractLookupTable[0x20 * alpha];
    GFXSurface *surface    = &gfxSurface[sheetID];
    int pitch              = GFX_LINESIZE - width;
    int gfxPitch           = surface->width - width;
    byte *lineBuffer       = &gfxLineBuffer[YPos];
    byte *gfxData          = &graphicData[sprX + surface->width * sprY + surface->dataPosition];
    ushort *frameBufferPtr = &Engine.frameBuffer[XPos + GFX_LINESIZE * YPos];

    while (height--) {
        activePalette   = fullPalette[*lineBuffer];
        activePalette32 = fullPalette32[*lineBuffer];
        lineBuffer++;
        int w = width;
        while (w--) {
            if (*gfxData > 0) {
                ushort color = activePalette[*gfxData];

                int R = maxVal((*frameBufferPtr & 0xF800) - (subBlendTable[(color & 0xF800) >> 11] << 11), 0);
                int G = maxVal((*frameBufferPtr & 0x7E0) - (subBlendTable[(color & 0x7E0) >> 6] << 6), 0);
                int B = maxVal((*frameBufferPtr & 0x1F) - subBlendTable[color & 0x1F], 0);

                *frameBufferPtr = R | G | B;
            }
            ++gfxData;
            ++frameBufferPtr;
        }
        frameBufferPtr += pitch;
        gfxData += gfxPitch;
    }
#endif
}

void DrawSpriteAllFX(int direction, int XPos, int YPos, int pivotX, int pivotY, int sprX, int sprY, int width, int height, int rotation,
                     int scaleX, int scaleY, int sheetID, int alpha, int ink, int flags)
{
#if RETRO_SOFTWARE_RENDER
    if (sheetID < 0 || sheetID >= SURFACE_COUNT || width <= 0 || height <= 0)
        return;

    GFXSurface *surface = &gfxSurface[sheetID];
    if (surface->width <= 0 || surface->height <= 0 || sprX < 0 || sprY < 0 || sprX + width > surface->width
        || sprY + height > surface->height)
        return;

    if (!(flags & DRAWFX_HSCALE))
        scaleX = 0x200;
    if (!(flags & DRAWFX_VSCALE))
        scaleY = 0x200;
    if (scaleX <= 0 || scaleY <= 0)
        return;

    if (!(flags & DRAWFX_ROTATE))
        rotation = 0;
    if (!(flags & DRAWFX_FLIP))
        direction = FLIP_NONE;
    else
        direction &= FLIP_XY;
    if (!(flags & DRAWFX_INK))
        ink = INK_NONE;

    if (alpha < 0)
        alpha = 0;
    else if (alpha > 0xFF)
        alpha = 0xFF;

    int angle = rotation & 0x1FF;
    if (angle)
        angle = 0x200 - angle;
    int sine   = sin512LookupTable[angle];
    int cosine = cos512LookupTable[angle];
    int xPositions[4];
    int yPositions[4];
    const int cornerX[4] = { 0, width, width, 0 };
    const int cornerY[4] = { 0, 0, height, height };

    for (int i = 0; i < 4; ++i) {
        int64_t localX = ((int64_t)(cornerX[i] - pivotX) * scaleX) >> 9;
        int64_t localY = ((int64_t)(cornerY[i] - pivotY) * scaleY) >> 9;
        xPositions[i]  = XPos + (int)(((int64_t)cosine * localX + (int64_t)sine * localY) >> 9);
        yPositions[i]  = YPos + (int)(((int64_t)-sine * localX + (int64_t)cosine * localY) >> 9);
    }

    int left   = xPositions[0];
    int right  = xPositions[0];
    int top    = yPositions[0];
    int bottom = yPositions[0];
    for (int i = 1; i < 4; ++i) {
        if (xPositions[i] < left)
            left = xPositions[i];
        if (xPositions[i] > right)
            right = xPositions[i];
        if (yPositions[i] < top)
            top = yPositions[i];
        if (yPositions[i] > bottom)
            bottom = yPositions[i];
    }

    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > GFX_LINESIZE)
        right = GFX_LINESIZE;
    if (bottom > SCREEN_YSIZE)
        bottom = SCREEN_YSIZE;
    if (left >= right || top >= bottom)
        return;

    for (int y = top; y < bottom; ++y) {
        activePalette   = fullPalette[gfxLineBuffer[y]];
        activePalette32 = fullPalette32[gfxLineBuffer[y]];
        ushort *frameBufferPtr = &Engine.frameBuffer[y * GFX_LINESIZE + left];

        for (int x = left; x < right; ++x, ++frameBufferPtr) {
            int64_t dx = ((int64_t)(x - XPos) << 9) + 0x100;
            int64_t dy = ((int64_t)(y - YPos) << 9) + 0x100;
            int64_t localX = ((int64_t)cosine * dx - (int64_t)sine * dy) >> 9;
            int64_t localY = ((int64_t)sine * dx + (int64_t)cosine * dy) >> 9;
            int sourceX = (int)(localX * 0x200 / scaleX + ((int64_t)pivotX << 9)) >> 9;
            int sourceY = (int)(localY * 0x200 / scaleY + ((int64_t)pivotY << 9)) >> 9;

            if (sourceX < 0 || sourceX >= width || sourceY < 0 || sourceY >= height)
                continue;
            if (direction & FLIP_X)
                sourceX = width - 1 - sourceX;
            if (direction & FLIP_Y)
                sourceY = height - 1 - sourceY;

            byte pixelIndex = graphicData[surface->dataPosition + sprX + sourceX + (sprY + sourceY) * surface->width];
            if (!pixelIndex)
                continue;

            ushort source = activePalette[pixelIndex];
            ushort dest   = *frameBufferPtr;
            int sourceR   = (source >> 11) & 0x1F;
            int sourceG   = (source >> 5) & 0x3F;
            int sourceB   = source & 0x1F;
            int destR     = (dest >> 11) & 0x1F;
            int destG     = (dest >> 5) & 0x3F;
            int destB     = dest & 0x1F;

            switch (ink) {
                case INK_BLEND:
                    *frameBufferPtr = ((source & 0xF7DE) >> 1) + ((dest & 0xF7DE) >> 1);
                    break;
                case INK_ALPHA:
                    *frameBufferPtr = (ushort)((((sourceR * alpha + destR * (0xFF - alpha) + 127) / 0xFF) << 11)
                                               | (((sourceG * alpha + destG * (0xFF - alpha) + 127) / 0xFF) << 5)
                                               | ((sourceB * alpha + destB * (0xFF - alpha) + 127) / 0xFF));
                    break;
                case INK_ADD: {
                    int red   = destR + (sourceR * alpha + 127) / 0xFF;
                    int green = destG + (sourceG * alpha + 127) / 0xFF;
                    int blue  = destB + (sourceB * alpha + 127) / 0xFF;
                    if (red > 0x1F)
                        red = 0x1F;
                    if (green > 0x3F)
                        green = 0x3F;
                    if (blue > 0x1F)
                        blue = 0x1F;
                    *frameBufferPtr = (ushort)((red << 11) | (green << 5) | blue);
                    break;
                }
                case INK_SUB: {
                    int red   = destR - ((0x1F - sourceR) * alpha + 127) / 0xFF;
                    int green = destG - ((0x3F - sourceG) * alpha + 127) / 0xFF;
                    int blue  = destB - ((0x1F - sourceB) * alpha + 127) / 0xFF;
                    if (red < 0)
                        red = 0;
                    if (green < 0)
                        green = 0;
                    if (blue < 0)
                        blue = 0;
                    *frameBufferPtr = (ushort)((red << 11) | (green << 5) | blue);
                    break;
                }
                default: *frameBufferPtr = source; break;
            }
        }
    }
#else
    if (flags & DRAWFX_ROTATE) {
        if (scaleX == scaleY)
            DrawSpriteRotozoom(direction, XPos, YPos, pivotX, pivotY, sprX, sprY, width, height, rotation, scaleX, sheetID);
        else
            DrawSpriteRotated(direction, XPos, YPos, pivotX, pivotY, sprX, sprY, width, height, rotation, sheetID);
    }
    else if (flags & (DRAWFX_HSCALE | DRAWFX_VSCALE)) {
        DrawSpriteScaled(direction, XPos, YPos, pivotX, pivotY, scaleX, scaleY, width, height, sprX, sprY, sheetID);
    }
    else {
        DrawSpriteFlipped(XPos + pivotX, YPos + pivotY, width, height, sprX, sprY, direction, sheetID);
    }
#endif
}

void DrawObjectAnimation(void *objScr, void *ent, int XPos, int YPos)
{
    ObjectScript *objectScript = (ObjectScript *)objScr;
    Entity *entity             = (Entity *)ent;
    SpriteAnimation *sprAnim   = &animationList[objectScript->animFile->aniListOffset + entity->animation];
    SpriteFrame *frame         = &animFrames[sprAnim->frameListOffset + entity->frame];
    int rotation               = 0;

    if (entity->inkEffect != INK_NONE || entity->scale != 0x200) {
        int frameID      = entity->frame;
        int drawRotation = entity->rotation;
        int drawFlags    = DRAWFX_FLIP | DRAWFX_HSCALE | DRAWFX_VSCALE | DRAWFX_INK;

        switch (sprAnim->rotationStyle) {
            case ROTSTYLE_NONE: break;
            case ROTSTYLE_FULL: drawFlags |= DRAWFX_ROTATE; break;
            case ROTSTYLE_45DEG:
                drawFlags |= DRAWFX_ROTATE;
                if (entity->rotation >= 0x100)
                    drawRotation = 0x200 - ((0x214 - entity->rotation) >> 6 << 6);
                else
                    drawRotation = (entity->rotation + 20) >> 6 << 6;
                break;
            case ROTSTYLE_STATICFRAMES: {
                drawFlags |= DRAWFX_ROTATE;
                if (entity->rotation >= 0x100)
                    rotation = 8 - ((532 - entity->rotation) >> 6);
                else
                    rotation = (entity->rotation + 20) >> 6;

                switch (rotation) {
                    case 0:
                    case 8: drawRotation = 0; break;
                    case 1:
                        frameID += sprAnim->frameCount;
                        drawRotation = entity->direction ? 0 : 0x80;
                        break;
                    case 2: drawRotation = 0x80; break;
                    case 3:
                        frameID += sprAnim->frameCount;
                        drawRotation = entity->direction ? 0x80 : 0x100;
                        break;
                    case 4: drawRotation = 0x100; break;
                    case 5:
                        frameID += sprAnim->frameCount;
                        drawRotation = entity->direction ? 0x100 : 384;
                        break;
                    case 6: drawRotation = 384; break;
                    case 7:
                        frameID += sprAnim->frameCount;
                        drawRotation = entity->direction ? 384 : 0;
                        break;
                    default: break;
                }

                frame = &animFrames[sprAnim->frameListOffset + frameID];
                break;
            }
            default: return;
        }

        DrawSpriteAllFX(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width, frame->height,
                        drawRotation, entity->scale, entity->scale, frame->sheetID, entity->alpha, entity->inkEffect, drawFlags);
        return;
    }

    switch (sprAnim->rotationStyle) {
        case ROTSTYLE_NONE:
            switch (entity->direction) {
                case FLIP_NONE:
                    DrawSpriteFlipped(frame->pivotX + XPos, frame->pivotY + YPos, frame->width, frame->height, frame->sprX, frame->sprY, FLIP_NONE,
                                      frame->sheetID);
                    break;

                case FLIP_X:
                    DrawSpriteFlipped(XPos - frame->width - frame->pivotX, frame->pivotY + YPos, frame->width, frame->height, frame->sprX,
                                      frame->sprY, FLIP_X, frame->sheetID);
                    break;
                case FLIP_Y:

                    DrawSpriteFlipped(frame->pivotX + XPos, YPos - frame->height - frame->pivotY, frame->width, frame->height, frame->sprX,
                                      frame->sprY, FLIP_Y, frame->sheetID);
                    break;

                case FLIP_XY:
                    DrawSpriteFlipped(XPos - frame->width - frame->pivotX, YPos - frame->height - frame->pivotY, frame->width, frame->height,
                                      frame->sprX, frame->sprY, FLIP_XY, frame->sheetID);
                    break;

                default: break;
            }
            break;

        case ROTSTYLE_FULL:
            DrawSpriteRotated(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width, frame->height,
                              entity->rotation, frame->sheetID);
            break;

        case ROTSTYLE_45DEG:
            if (entity->rotation >= 0x100)
                DrawSpriteRotated(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width,
                                  frame->height, 0x200 - ((0x214 - entity->rotation) >> 6 << 6), frame->sheetID);
            else
                DrawSpriteRotated(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width,
                                  frame->height, (entity->rotation + 20) >> 6 << 6, frame->sheetID);
            break;

        case ROTSTYLE_STATICFRAMES: {
            if (entity->rotation >= 0x100)
                rotation = 8 - ((532 - entity->rotation) >> 6);
            else
                rotation = (entity->rotation + 20) >> 6;
            int frameID = entity->frame;
            switch (rotation) {
                case 0: // 0 deg
                case 8: // 360 deg
                    rotation = 0x00;
                    break;

                case 1: // 45 deg
                    frameID += sprAnim->frameCount;
                    if (entity->direction)
                        rotation = 0;
                    else
                        rotation = 0x80;
                    break;

                case 2: // 90 deg
                    rotation = 0x80;
                    break;

                case 3: // 135 deg
                    frameID += sprAnim->frameCount;
                    if (entity->direction)
                        rotation = 0x80;
                    else
                        rotation = 0x100;
                    break;

                case 4: // 180 deg
                    rotation = 0x100;
                    break;

                case 5: // 225 deg
                    frameID += sprAnim->frameCount;
                    if (entity->direction)
                        rotation = 0x100;
                    else
                        rotation = 384;
                    break;

                case 6: // 270 deg
                    rotation = 384;
                    break;

                case 7: // 315 deg
                    frameID += sprAnim->frameCount;
                    if (entity->direction)
                        rotation = 384;
                    else
                        rotation = 0;
                    break;

                default: break;
            }

            frame = &animFrames[sprAnim->frameListOffset + frameID];
            DrawSpriteRotated(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width, frame->height,
                              rotation, frame->sheetID);
            // DrawSpriteRotozoom(entity->direction, XPos, YPos, -frame->pivotX, -frame->pivotY, frame->sprX, frame->sprY, frame->width,
            // frame->height,
            //                  rotation, entity->scale, frame->sheetID);
            break;
        }

        default: break;
    }
}

void DrawFace(void *v, uint color)
{
    Vertex *verts = (Vertex *)v;
    int alpha     = (color & 0x7F000000) >> 23;
    if (alpha < 1)
        return;

    if (alpha > 0xFF)
        alpha = 0xFF;

    if (verts[0].x < 0 && verts[1].x < 0 && verts[2].x < 0 && verts[3].x < 0)
        return;

    if (verts[0].x > GFX_LINESIZE && verts[1].x > GFX_LINESIZE && verts[2].x > GFX_LINESIZE && verts[3].x > GFX_LINESIZE)
        return;

    if (verts[0].y < 0 && verts[1].y < 0 && verts[2].y < 0 && verts[3].y < 0)
        return;

    if (verts[0].y > SCREEN_YSIZE && verts[1].y > SCREEN_YSIZE && verts[2].y > SCREEN_YSIZE && verts[3].y > SCREEN_YSIZE)
        return;

    if (verts[0].x == verts[1].x && verts[1].x == verts[2].x && verts[2].x == verts[3].x)
        return;

    if (verts[0].y == verts[1].y && verts[1].y == verts[2].y && verts[2].y == verts[3].y)
        return;

#if RETRO_SOFTWARE_RENDER
    int vertexA = 0;
    int vertexB = 1;
    int vertexC = 2;
    int vertexD = 3;
    if (verts[1].y < verts[0].y) {
        vertexA = 1;
        vertexB = 0;
    }
    if (verts[2].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 2;
        vertexC  = temp;
    }
    if (verts[3].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 3;
        vertexD  = temp;
    }
    if (verts[vertexC].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexC;
        vertexC  = temp;
    }
    if (verts[vertexD].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexD;
        vertexD  = temp;
    }
    if (verts[vertexD].y < verts[vertexC].y) {
        int temp = vertexC;
        vertexC  = vertexD;
        vertexD  = temp;
    }

    int faceTop    = verts[vertexA].y;
    int faceBottom = verts[vertexD].y;
    if (faceTop < 0)
        faceTop = 0;
    if (faceBottom > SCREEN_YSIZE)
        faceBottom = SCREEN_YSIZE;
    for (int i = faceTop; i < faceBottom; ++i) {
        faceLineStart[i] = 100000;
        faceLineEnd[i]   = -100000;
    }

    ProcessScanEdge(&verts[vertexA], &verts[vertexB]);
    ProcessScanEdge(&verts[vertexA], &verts[vertexC]);
    ProcessScanEdge(&verts[vertexA], &verts[vertexD]);
    ProcessScanEdge(&verts[vertexB], &verts[vertexC]);
    ProcessScanEdge(&verts[vertexC], &verts[vertexD]);
    ProcessScanEdge(&verts[vertexB], &verts[vertexD]);

    ushort color16 = PACK_RGB888(((color >> 16) & 0xFF), ((color >> 8) & 0xFF), ((color >> 0) & 0xFF));

    ushort *frameBufferPtr = &Engine.frameBuffer[GFX_LINESIZE * faceTop];
    if (alpha == 255) {
        while (faceTop < faceBottom) {
            int startX = faceLineStart[faceTop];
            int endX   = faceLineEnd[faceTop];
            if (startX >= GFX_LINESIZE || endX <= 0) {
                frameBufferPtr += GFX_LINESIZE;
            }
            else {
                if (startX < 0)
                    startX = 0;
                if (endX > GFX_LINESIZE_MINUSONE)
                    endX = GFX_LINESIZE_MINUSONE;
                ushort *fbPtr = &frameBufferPtr[startX];
                frameBufferPtr += GFX_LINESIZE;
                int vertexwidth = endX - startX + 1;
                while (vertexwidth--) {
                    *fbPtr = color16;
                    ++fbPtr;
                }
            }
            ++faceTop;
        }
    }
    else {
        ushort *fbufferBlend = &blendLookupTable[0x20 * (0xFF - alpha)];
        ushort *pixelBlend   = &blendLookupTable[0x20 * alpha];

        while (faceTop < faceBottom) {
            int startX = faceLineStart[faceTop];
            int endX   = faceLineEnd[faceTop];
            if (startX >= GFX_LINESIZE || endX <= 0) {
                frameBufferPtr += GFX_LINESIZE;
            }
            else {
                if (startX < 0)
                    startX = 0;
                if (endX > GFX_LINESIZE_MINUSONE)
                    endX = GFX_LINESIZE_MINUSONE;
                ushort *fbPtr = &frameBufferPtr[startX];
                frameBufferPtr += GFX_LINESIZE;
                int vertexwidth = endX - startX + 1;
                while (vertexwidth--) {
                    int R = (fbufferBlend[(*fbPtr & 0xF800) >> 11] + pixelBlend[(color16 & 0xF800) >> 11]) << 11;
                    int G = (fbufferBlend[(*fbPtr & 0x7E0) >> 6] + pixelBlend[(color16 & 0x7E0) >> 6]) << 6;
                    int B = fbufferBlend[*fbPtr & 0x1F] + pixelBlend[color16 & 0x1F];

                    *fbPtr = R | G | B;
                    ++fbPtr;
                }
            }
            ++faceTop;
        }
    }
#endif
}
void DrawFadedFace(void *v, uint color, uint fogColor, int alpha)
{
    Vertex *verts = (Vertex *)v;
    if (alpha > 0xFF)
        alpha = 0xFF;

    if (alpha < 1)
        return;

    if (verts[0].x < 0 && verts[1].x < 0 && verts[2].x < 0 && verts[3].x < 0)
        return;

    if (verts[0].x > GFX_LINESIZE && verts[1].x > GFX_LINESIZE && verts[2].x > GFX_LINESIZE && verts[3].x > GFX_LINESIZE)
        return;

    if (verts[0].y < 0 && verts[1].y < 0 && verts[2].y < 0 && verts[3].y < 0)
        return;

    if (verts[0].y > SCREEN_YSIZE && verts[1].y > SCREEN_YSIZE && verts[2].y > SCREEN_YSIZE && verts[3].y > SCREEN_YSIZE)
        return;

    if (verts[0].x == verts[1].x && verts[1].x == verts[2].x && verts[2].x == verts[3].x)
        return;

    if (verts[0].y == verts[1].y && verts[1].y == verts[2].y && verts[2].y == verts[3].y)
        return;

#if RETRO_SOFTWARE_RENDER
    int vertexA = 0;
    int vertexB = 1;
    int vertexC = 2;
    int vertexD = 3;
    if (verts[1].y < verts[0].y) {
        vertexA = 1;
        vertexB = 0;
    }
    if (verts[2].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 2;
        vertexC  = temp;
    }
    if (verts[3].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 3;
        vertexD  = temp;
    }
    if (verts[vertexC].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexC;
        vertexC  = temp;
    }
    if (verts[vertexD].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexD;
        vertexD  = temp;
    }
    if (verts[vertexD].y < verts[vertexC].y) {
        int temp = vertexC;
        vertexC  = vertexD;
        vertexD  = temp;
    }

    int faceTop    = verts[vertexA].y;
    int faceBottom = verts[vertexD].y;
    if (faceTop < 0)
        faceTop = 0;
    if (faceBottom > SCREEN_YSIZE)
        faceBottom = SCREEN_YSIZE;
    for (int i = faceTop; i < faceBottom; ++i) {
        faceLineStart[i] = 100000;
        faceLineEnd[i]   = -100000;
    }

    ProcessScanEdge(&verts[vertexA], &verts[vertexB]);
    ProcessScanEdge(&verts[vertexA], &verts[vertexC]);
    ProcessScanEdge(&verts[vertexA], &verts[vertexD]);
    ProcessScanEdge(&verts[vertexB], &verts[vertexC]);
    ProcessScanEdge(&verts[vertexC], &verts[vertexD]);
    ProcessScanEdge(&verts[vertexB], &verts[vertexD]);

    ushort color16    = PACK_RGB888(((color >> 16) & 0xFF), ((color >> 8) & 0xFF), ((color >> 0) & 0xFF));
    ushort fogColor16 = PACK_RGB888(((fogColor >> 16) & 0xFF), ((fogColor >> 8) & 0xFF), ((fogColor >> 0) & 0xFF));

    ushort *frameBufferPtr = &Engine.frameBuffer[GFX_LINESIZE * faceTop];
    ushort *fbufferBlend   = &blendLookupTable[0x20 * (0xFF - alpha)];
    ushort *pixelBlend     = &blendLookupTable[0x20 * alpha];

    while (faceTop < faceBottom) {
        int startX = faceLineStart[faceTop];
        int endX   = faceLineEnd[faceTop];
        if (startX >= GFX_LINESIZE || endX <= 0) {
            frameBufferPtr += GFX_LINESIZE;
        }
        else {
            if (startX < 0)
                startX = 0;
            if (endX > GFX_LINESIZE_MINUSONE)
                endX = GFX_LINESIZE_MINUSONE;
            ushort *fbPtr = &frameBufferPtr[startX];
            frameBufferPtr += GFX_LINESIZE;
            int vertexwidth = endX - startX + 1;
            while (vertexwidth--) {
                int R = (fbufferBlend[(fogColor16 & 0xF800) >> 11] + pixelBlend[(color16 & 0xF800) >> 11]) << 11;
                int G = (fbufferBlend[(fogColor16 & 0x7E0) >> 6] + pixelBlend[(color16 & 0x7E0) >> 6]) << 6;
                int B = fbufferBlend[fogColor16 & 0x1F] + pixelBlend[color16 & 0x1F];

                *fbPtr = R | G | B;
                ++fbPtr;
            }
        }
        ++faceTop;
    }
#endif
}
void DrawTexturedFace(void *v, byte sheetID)
{
    Vertex *verts = (Vertex *)v;

    if (verts[0].x < 0 && verts[1].x < 0 && verts[2].x < 0 && verts[3].x < 0)
        return;
    if (verts[0].x > GFX_LINESIZE && verts[1].x > GFX_LINESIZE && verts[2].x > GFX_LINESIZE && verts[3].x > GFX_LINESIZE)
        return;
    if (verts[0].y < 0 && verts[1].y < 0 && verts[2].y < 0 && verts[3].y < 0)
        return;
    if (verts[0].y > SCREEN_YSIZE && verts[1].y > SCREEN_YSIZE && verts[2].y > SCREEN_YSIZE && verts[3].y > SCREEN_YSIZE)
        return;
    if (verts[0].x == verts[1].x && verts[1].x == verts[2].x && verts[2].x == verts[3].x)
        return;
    if (verts[0].y == verts[1].y && verts[1].y == verts[2].y && verts[2].y == verts[3].y)
        return;

#if RETRO_SOFTWARE_RENDER
    int vertexA = 0;
    int vertexB = 1;
    int vertexC = 2;
    int vertexD = 3;
    if (verts[1].y < verts[0].y) {
        vertexA = 1;
        vertexB = 0;
    }
    if (verts[2].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 2;
        vertexC  = temp;
    }
    if (verts[3].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 3;
        vertexD  = temp;
    }
    if (verts[vertexC].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexC;
        vertexC  = temp;
    }
    if (verts[vertexD].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexD;
        vertexD  = temp;
    }
    if (verts[vertexD].y < verts[vertexC].y) {
        int temp = vertexC;
        vertexC  = vertexD;
        vertexD  = temp;
    }

    int faceTop    = verts[vertexA].y;
    int faceBottom = verts[vertexD].y;
    if (faceTop < 0)
        faceTop = 0;
    if (faceBottom > SCREEN_YSIZE)
        faceBottom = SCREEN_YSIZE;
    for (int i = faceTop; i < faceBottom; ++i) {
        faceLineStart[i] = 100000;
        faceLineEnd[i]   = -100000;
    }

    ProcessScanEdgeUV(&verts[vertexA], &verts[vertexB]);
    ProcessScanEdgeUV(&verts[vertexA], &verts[vertexC]);
    ProcessScanEdgeUV(&verts[vertexA], &verts[vertexD]);
    ProcessScanEdgeUV(&verts[vertexB], &verts[vertexC]);
    ProcessScanEdgeUV(&verts[vertexC], &verts[vertexD]);
    ProcessScanEdgeUV(&verts[vertexB], &verts[vertexD]);

    ushort *frameBufferPtr = &Engine.frameBuffer[GFX_LINESIZE * faceTop];
    byte *sheetPtr         = &graphicData[gfxSurface[sheetID].dataPosition];
    int shiftwidth         = gfxSurface[sheetID].widthShift;
    byte *lineBuffer       = &gfxLineBuffer[faceTop];
    while (faceTop < faceBottom) {
        activePalette   = fullPalette[*lineBuffer];
        activePalette32 = fullPalette32[*lineBuffer];
        lineBuffer++;
        int startX = faceLineStart[faceTop];
        int endX   = faceLineEnd[faceTop];
        int UPos   = faceLineStartU[faceTop];
        int VPos   = faceLineStartV[faceTop];
        if (startX >= GFX_LINESIZE || endX <= 0) {
            frameBufferPtr += GFX_LINESIZE;
        }
        else {
            int posDifference = endX - startX;
            int bufferedUPos  = 0;
            int bufferedVPos  = 0;
            if (endX == startX) {
                bufferedUPos = 0;
                bufferedVPos = 0;
            }
            else {
                bufferedUPos = (faceLineEndU[faceTop] - UPos) / posDifference;
                bufferedVPos = (faceLineEndV[faceTop] - VPos) / posDifference;
            }
            if (endX > GFX_LINESIZE_MINUSONE)
                posDifference = GFX_LINESIZE_MINUSONE - startX;
            if (startX < 0) {
                posDifference += startX;
                UPos -= startX * bufferedUPos;
                VPos -= startX * bufferedVPos;
                startX = 0;
            }
            ushort *fbPtr = &frameBufferPtr[startX];
            frameBufferPtr += GFX_LINESIZE;

            int counter = posDifference + 1;

            while (counter--) {
                if (UPos < 0)
                    UPos = 0;
                if (VPos < 0)
                    VPos = 0;
                ushort index = sheetPtr[(VPos >> 16 << shiftwidth) + (UPos >> 16)];
                if (index > 0)
                    *fbPtr = activePalette[index];
                fbPtr++;
                UPos += bufferedUPos;
                VPos += bufferedVPos;
            }
        }
        ++faceTop;
    }
#endif
}
void DrawTexturedFaceBlended(void *v, byte sheetID)
{
    Vertex *verts = (Vertex *)v;
    if (verts[0].x < 0 && verts[1].x < 0 && verts[2].x < 0 && verts[3].x < 0)
        return;

    if (verts[0].x > GFX_LINESIZE && verts[1].x > GFX_LINESIZE && verts[2].x > GFX_LINESIZE && verts[3].x > GFX_LINESIZE)
        return;

    if (verts[0].y < 0 && verts[1].y < 0 && verts[2].y < 0 && verts[3].y < 0)
        return;

    if (verts[0].y > SCREEN_YSIZE && verts[1].y > SCREEN_YSIZE && verts[2].y > SCREEN_YSIZE && verts[3].y > SCREEN_YSIZE)
        return;

    if (verts[0].x == verts[1].x && verts[1].x == verts[2].x && verts[2].x == verts[3].x)
        return;

    if (verts[0].y == verts[1].y && verts[1].y == verts[2].y && verts[2].y == verts[3].y)
        return;

#if RETRO_SOFTWARE_RENDER
    int vertexA = 0;
    int vertexB = 1;
    int vertexC = 2;
    int vertexD = 3;
    if (verts[1].y < verts[0].y) {
        vertexA = 1;
        vertexB = 0;
    }
    if (verts[2].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 2;
        vertexC  = temp;
    }
    if (verts[3].y < verts[vertexA].y) {
        int temp = vertexA;
        vertexA  = 3;
        vertexD  = temp;
    }
    if (verts[vertexC].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexC;
        vertexC  = temp;
    }
    if (verts[vertexD].y < verts[vertexB].y) {
        int temp = vertexB;
        vertexB  = vertexD;
        vertexD  = temp;
    }
    if (verts[vertexD].y < verts[vertexC].y) {
        int temp = vertexC;
        vertexC  = vertexD;
        vertexD  = temp;
    }

    int faceTop    = verts[vertexA].y;
    int faceBottom = verts[vertexD].y;
    if (faceTop < 0)
        faceTop = 0;
    if (faceBottom > SCREEN_YSIZE)
        faceBottom = SCREEN_YSIZE;
    for (int i = faceTop; i < faceBottom; ++i) {
        faceLineStart[i] = 100000;
        faceLineEnd[i]   = -100000;
    }

    ProcessScanEdgeUV(&verts[vertexA], &verts[vertexB]);
    ProcessScanEdgeUV(&verts[vertexA], &verts[vertexC]);
    ProcessScanEdgeUV(&verts[vertexA], &verts[vertexD]);
    ProcessScanEdgeUV(&verts[vertexB], &verts[vertexC]);
    ProcessScanEdgeUV(&verts[vertexC], &verts[vertexD]);
    ProcessScanEdgeUV(&verts[vertexB], &verts[vertexD]);

    ushort *frameBufferPtr = &Engine.frameBuffer[GFX_LINESIZE * faceTop];
    byte *sheetPtr         = &graphicData[gfxSurface[sheetID].dataPosition];
    int shiftwidth         = gfxSurface[sheetID].widthShift;
    byte *lineBuffer       = &gfxLineBuffer[faceTop];
    while (faceTop < faceBottom) {
        activePalette   = fullPalette[*lineBuffer];
        activePalette32 = fullPalette32[*lineBuffer];
        lineBuffer++;
        int startX = faceLineStart[faceTop];
        int endX   = faceLineEnd[faceTop];
        int UPos   = faceLineStartU[faceTop];
        int VPos   = faceLineStartV[faceTop];
        if (startX >= GFX_LINESIZE || endX <= 0) {
            frameBufferPtr += GFX_LINESIZE;
        }
        else {
            int posDifference = endX - startX;
            int bufferedUPos  = 0;
            int bufferedVPos  = 0;
            if (endX == startX) {
                bufferedUPos = 0;
                bufferedVPos = 0;
            }
            else {
                bufferedUPos = (faceLineEndU[faceTop] - UPos) / posDifference;
                bufferedVPos = (faceLineEndV[faceTop] - VPos) / posDifference;
            }
            if (endX > GFX_LINESIZE_MINUSONE)
                posDifference = GFX_LINESIZE_MINUSONE - startX;
            if (startX < 0) {
                posDifference += startX;
                UPos -= startX * bufferedUPos;
                VPos -= startX * bufferedVPos;
                startX = 0;
            }
            ushort *fbPtr = &frameBufferPtr[startX];
            frameBufferPtr += GFX_LINESIZE;
#if RETRO_REV02
            int counter = posDifference;
#else
            int counter = posDifference + 1;
#endif
            while (counter--) {
                if (UPos < 0)
                    UPos = 0;
                if (VPos < 0)
                    VPos = 0;
                ushort index = sheetPtr[(VPos >> 16 << shiftwidth) + (UPos >> 16)];
                if (index > 0)
                    *fbPtr = ((activePalette[index] & 0xF7BC) >> 1) + ((*fbPtr & 0xF7BC) >> 1);
                fbPtr++;
                UPos += bufferedUPos;
                VPos += bufferedVPos;
            }
        }
        ++faceTop;
    }
#endif
}

#if !RETRO_REV02
void DrawBitmapText(void *menu, int XPos, int YPos, int scale, int spacing, int rowStart, int rowCount)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int Y           = YPos << 9;
    if (rowCount < 0)
        rowCount = tMenu->rowCount;
    if (rowStart + rowCount > tMenu->rowCount)
        rowCount = tMenu->rowCount - rowStart;

    while (rowCount > 0) {
        int X = XPos << 9;
        for (int i = 0; i < tMenu->entrySize[rowStart]; ++i) {
            ushort c             = tMenu->textData[tMenu->entryStart[rowStart] + i];
            FontCharacter *fChar = &fontCharacterList[c];
#if RETRO_SOFTWARE_RENDER
            DrawSpriteScaled(FLIP_NONE, X >> 9, Y >> 9, -fChar->pivotX, -fChar->pivotY, scale, scale, fChar->width, fChar->height, fChar->srcX,
                             fChar->srcY, textMenuSurfaceNo);
#endif
            X += fChar->xAdvance * scale;
        }
        Y += spacing * scale;
        rowStart++;
        rowCount--;
    }
}
#endif

void DrawTextMenuEntry(void *menu, int rowID, int XPos, int YPos, int textHighlight)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int id          = tMenu->entryStart[rowID];
    for (int i = 0; i < tMenu->entrySize[rowID]; ++i) {
        DrawSprite(XPos + (i << 3) - (((tMenu->entrySize[rowID] % 2) & (tMenu->alignment == 2)) * 4), YPos, 8, 8, ((tMenu->textData[id] & 0xF) << 3),
                   ((tMenu->textData[id] >> 4) << 3) + textHighlight, textMenuSurfaceNo);
        id++;
    }
}
void DrawStageTextEntry(void *menu, int rowID, int XPos, int YPos, int textHighlight)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int id          = tMenu->entryStart[rowID];
    for (int i = 0; i < tMenu->entrySize[rowID]; ++i) {
        if (i == tMenu->entrySize[rowID] - 1) {
            DrawSprite(XPos + (i << 3), YPos, 8, 8, ((tMenu->textData[id] & 0xF) << 3), ((tMenu->textData[id] >> 4) << 3), textMenuSurfaceNo);
        }
        else {
            DrawSprite(XPos + (i << 3), YPos, 8, 8, ((tMenu->textData[id] & 0xF) << 3), ((tMenu->textData[id] >> 4) << 3) + textHighlight,
                       textMenuSurfaceNo);
        }
        id++;
    }
}
void DrawBlendedTextMenuEntry(void *menu, int rowID, int XPos, int YPos, int textHighlight)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int id          = tMenu->entryStart[rowID];
    for (int i = 0; i < tMenu->entrySize[rowID]; ++i) {
        DrawBlendedSprite(XPos + (i << 3), YPos, 8, 8, ((tMenu->textData[id] & 0xF) << 3), ((tMenu->textData[id] >> 4) << 3) + textHighlight,
                          textMenuSurfaceNo);
        id++;
    }
}
void DrawTextMenu(void *menu, int XPos, int YPos)
{
    TextMenu *tMenu = (TextMenu *)menu;
    int cnt         = 0;

    if (tMenu->visibleRowCount > 0) {
        cnt = (int)(tMenu->visibleRowCount + tMenu->visibleRowOffset);
    }
    else {
        tMenu->visibleRowOffset = 0;
        cnt                     = (int)tMenu->rowCount;
    }

    if (tMenu->selectionCount == 3) {
        tMenu->selection2 = -1;
        for (int i = 0; i <= tMenu->selection1; ++i) {
            if (tMenu->entryHighlight[i]) {
                tMenu->selection2 = i;
            }
        }
    }

    switch (tMenu->alignment) {
        case 0:
            for (int i = (int)tMenu->visibleRowOffset; i < cnt; ++i) {
                switch (tMenu->selectionCount) {
                    case 1:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 0);
                        break;

                    case 2:
                        if (i == tMenu->selection1 || i == tMenu->selection2)
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 0);
                        break;

                    case 3:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, XPos, YPos, 0);

                        if (i == tMenu->selection2 && i != tMenu->selection1)
                            DrawStageTextEntry(tMenu, i, XPos, YPos, 128);
                        break;
                }
                YPos += 8;
            }
            break;

        case 1:
            for (int i = (int)tMenu->visibleRowOffset; i < cnt; ++i) {
                int entryX = XPos - (tMenu->entrySize[i] << 3);
                switch (tMenu->selectionCount) {
                    case 1:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);
                        break;

                    case 2:
                        if (i == tMenu->selection1 || i == tMenu->selection2)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);
                        break;

                    case 3:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);

                        if (i == tMenu->selection2 && i != tMenu->selection1)
                            DrawStageTextEntry(tMenu, i, entryX, YPos, 128);
                        break;
                }
                YPos += 8;
            }
            break;

        case 2:
            for (int i = (int)tMenu->visibleRowOffset; i < cnt; ++i) {
                int entryX = XPos - (tMenu->entrySize[i] >> 1 << 3);
                switch (tMenu->selectionCount) {
                    case 1:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);
                        break;
                    case 2:
                        if (i == tMenu->selection1 || i == tMenu->selection2)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);
                        break;
                    case 3:
                        if (i == tMenu->selection1)
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 128);
                        else
                            DrawTextMenuEntry(tMenu, i, entryX, YPos, 0);

                        if (i == tMenu->selection2 && i != tMenu->selection1)
                            DrawStageTextEntry(tMenu, i, entryX, YPos, 128);
                        break;
                }
                YPos += 8;
            }
            break;

        default: break;
    }
}
