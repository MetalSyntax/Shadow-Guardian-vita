/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  glutil.h
 * @brief OpenGL API initializer, related functions.
 */

#ifndef SOLOADER_GLUTIL_H
#define SOLOADER_GLUTIL_H

#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_init();

void gl_preload();

void gl_swap();

// Escalado directo: el motor cree que la pantalla es 800x480 (su
// resolucion de referencia, Samsung i9000) y el viewport de pantalla
// completa se estira a los 960x544 reales. Sin FBO intermedio.
void glViewport_soloader(GLint x, GLint y, GLsizei width, GLsizei height);
void glBindFramebuffer_soloader(GLenum target, GLuint framebuffer);

void glCompileShader_soloader(GLuint shader);

void glLinkProgram_soloader(GLuint program);

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_GLUTIL_H
