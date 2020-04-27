#include "../include/canvas.h"
#include "../include/graphics.h"
#include "../include/vertex.h"

#include <stdio.h>
#include <stdlib.h>

static graphics_Vertex const imageData[] = {
    {{0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}
};

static unsigned char const imageIndices[] = { 0, 1, 2, 3 };

static void setup_quad(graphics_Canvas* c) {
    glGenBuffers(1, &c->image.vbo);
    glGenBuffers(1, &c->image.ibo);

    glBindBuffer(GL_ARRAY_BUFFER, c->image.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(imageData), imageData, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c->image.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(imageIndices), imageIndices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(graphics_Vertex), 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(graphics_Vertex), (GLvoid const*)(2*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(graphics_Vertex), (GLvoid const*)(4*sizeof(float)));
}

void graphics_Canvas_new(graphics_Canvas *c, int width, int height) {
	setup_quad(c);

	glGenFramebuffers(1, &c->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, c->fbo);
	// create a color attachment texture
	glGenTextures(1, &c->image.texID);
	glBindTexture(GL_TEXTURE_2D, c->image.texID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->image.texID, 0);
	// create a renderbuffer object for depth and stencil attachment
	/*glGenRenderbuffers(1, &c->rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, c->rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_STENCIL, width, height); // use a single renderbuffer object for both a depth AND stencil buffer.
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, c->rbo); // now actually attach it
	// now that we actually created the framebuffer and added all attachments we want to check if it is actually complete now
	*/
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("%s\n", "ERROR::FRAMEBUFFER:: Framebuffer is not complete!");
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void graphics_Canvas_free(graphics_Canvas *canvas) {

}

void graphics_Canvas_draw(graphics_Canvas const* canvas, graphics_Quad const* quad,
                         float x, float y, float r, float sx, float sy,
                         float ox, float oy, float kx, float ky) {
    m4x4_newTransform2d(&canvas->image.tr2d, x, y, r, sx, sy, ox, oy, kx, ky);

	glBindVertexArray(canvas->image.vbo);
	glBindTexture(GL_TEXTURE_2D, canvas->image.texID);

	graphics_drawArray(quad, &canvas->image.tr2d,  canvas->image.ibo, 4, GL_TRIANGLE_STRIP, GL_UNSIGNED_BYTE,
			graphics_getColor(), canvas->image.width * quad->w, canvas->image.height * quad->h);
}

void graphics_setCanvas(graphics_Canvas* canvas) {
	if (canvas != NULL) {
		glBindFramebuffer(GL_FRAMEBUFFER, canvas->fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, canvas->image.texID, 0);
		GLenum a[] = {GL_COLOR_ATTACHMENT0};
		glDrawBuffers(1, a);
		//glViewport(0, 0, canvas->image.width, canvas->image.height);
	} else {
		// render the main framebuffer's window
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		//glViewport(0, 0, graphics_getWidth(), graphics_getHeight());
	}
}
