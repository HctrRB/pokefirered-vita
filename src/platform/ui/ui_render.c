#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#include "ui_internal.h"
#include "platform/ui.h"

#define UI_BACKDROP_TEX_SIZE 16

static GLuint sAtlasTex;
static GLuint sBackdropTex;
static uint32_t sTexBuf[UI_TEX_W * UI_TEX_H];
static uint32_t sBackdropBuf[UI_BACKDROP_TEX_SIZE * UI_BACKDROP_TEX_SIZE];
static bool sAtlasUploaded;
static int8_t sUploadedBackdropKind = -2;
static uint32_t sLastDrawnSeq;

static void UploadFixedAtlas(void)
{
    if (sAtlasUploaded || !UiAssets_Ready())
        return;

    uint16_t aw = 0, ah = 0;
    const uint16_t *const src = UiAssets_Pixels(&aw, &ah);
    if (src == NULL || aw != UI_TEX_W || ah == 0 || ah > UI_FIXED_MAX_H)
        return;

    glGenTextures(1, &sAtlasTex);
    glBindTexture(GL_TEXTURE_2D, sAtlasTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    memset(sTexBuf, 0, sizeof(sTexBuf));
    const uint32_t count = (uint32_t)aw * (uint32_t)ah;
    for (uint32_t i = 0; i < count; i++)
        sTexBuf[i] = Ui_UnpackTexel(src[i]);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, UI_TEX_W, UI_TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTexBuf);

    glGenTextures(1, &sBackdropTex);
    glBindTexture(GL_TEXTURE_2D, sBackdropTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, UI_BACKDROP_TEX_SIZE, UI_BACKDROP_TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    sAtlasUploaded = true;
}

static void DrawBackdrop(const int8_t kind)
{
    if (kind < 0 || sBackdropTex == 0)
        return;

    glBindTexture(GL_TEXTURE_2D, sBackdropTex);
    if (sUploadedBackdropKind != kind)
    {
        UiW_GbaBgPattern((uint8_t)kind, sBackdropBuf);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, UI_BACKDROP_TEX_SIZE, UI_BACKDROP_TEX_SIZE,
                        GL_RGBA, GL_UNSIGNED_BYTE, sBackdropBuf);
        sUploadedBackdropKind = kind;
    }

    const float uMax = (float)UI_SCREEN_W / (float)UI_BACKDROP_TEX_SIZE;
    const float vMax = (float)UI_SCREEN_H / (float)UI_BACKDROP_TEX_SIZE;
    static const float pos[8] = {
        0.0f, 0.0f,
        (float)UI_SCREEN_W, 0.0f,
        (float)UI_SCREEN_W, (float)UI_SCREEN_H,
        0.0f, (float)UI_SCREEN_H
    };
    const float uvs[8] = { 0.0f, 0.0f, uMax, 0.0f, uMax, vMax, 0.0f, vMax };

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexPointer(2, GL_FLOAT, 0, pos);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void Ui_Render(const int32_t viewWidth, const int32_t viewHeight)
{
    const UiDrawList *const list = UiAssets_Ready() ? UiDraw_Acquire() : NULL;
    if (list == NULL || (list->vcount == 0 && list->backdropKind < 0))
        return;
    UiStall_Render(list->seq);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    UploadFixedAtlas();
    if (sAtlasUploaded)
    {
        glViewport(0, 0, viewWidth, viewHeight);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (double)UI_SCREEN_W, (double)UI_SCREEN_H, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        if (list->backdropKind >= 0)
            DrawBackdrop(list->backdropKind);

        if (list->vcount > 0)
        {
            glBindTexture(GL_TEXTURE_2D, sAtlasTex);
            UiSpr_FlushUploads();

            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glVertexPointer(2, GL_FLOAT, sizeof(UiGlVertex), &list->verts[0].x);
            glTexCoordPointer(2, GL_FLOAT, sizeof(UiGlVertex), &list->verts[0].u);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(UiGlVertex), &list->verts[0].rgba);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)list->vcount);
            glDisableClientState(GL_COLOR_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
        }

        if (list->seq != sLastDrawnSeq)
        {
            sLastDrawnSeq = list->seq;
            UiStall_Render(list->seq);
        }
    }

    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
