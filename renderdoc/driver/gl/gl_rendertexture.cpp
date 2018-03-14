/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "maths/camera.h"
#include "maths/formatpacking.h"
#include "maths/matrix.h"
#include "gl_driver.h"
#include "gl_replay.h"
#include "gl_resources.h"

#define OPENGL 1
#include "data/glsl/debuguniforms.h"

bool GLReplay::RenderTexture(TextureDisplay cfg)
{
  return RenderTextureInternal(cfg, eTexDisplay_BlendAlpha | eTexDisplay_MipShift);
}

bool GLReplay::RenderTextureInternal(TextureDisplay cfg, int flags)
{
  const bool blendAlpha = (flags & eTexDisplay_BlendAlpha) != 0;
  const bool mipShift = (flags & eTexDisplay_MipShift) != 0;

  WrappedOpenGL &gl = *m_pDriver;

  auto &texDetails = m_pDriver->m_Textures[cfg.resourceId];

  if(texDetails.internalFormat == eGL_NONE)
    return false;

  bool renderbuffer = false;

  int intIdx = 0;

  int resType;
  switch(texDetails.curType)
  {
    case eGL_RENDERBUFFER:
      resType = RESTYPE_TEX2D;
      if(texDetails.samples > 1)
        resType = RESTYPE_TEX2DMS;
      renderbuffer = true;
      break;
    case eGL_TEXTURE_1D: resType = RESTYPE_TEX1D; break;
    default:
      RDCWARN("Unexpected texture type");
    // fall through
    case eGL_TEXTURE_2D: resType = RESTYPE_TEX2D; break;
    case eGL_TEXTURE_2D_MULTISAMPLE: resType = RESTYPE_TEX2DMS; break;
    case eGL_TEXTURE_RECTANGLE: resType = RESTYPE_TEXRECT; break;
    case eGL_TEXTURE_BUFFER: resType = RESTYPE_TEXBUFFER; break;
    case eGL_TEXTURE_3D: resType = RESTYPE_TEX3D; break;
    case eGL_TEXTURE_CUBE_MAP: resType = RESTYPE_TEXCUBE; break;
    case eGL_TEXTURE_1D_ARRAY: resType = RESTYPE_TEX1DARRAY; break;
    case eGL_TEXTURE_2D_ARRAY: resType = RESTYPE_TEX2DARRAY; break;
    case eGL_TEXTURE_CUBE_MAP_ARRAY: resType = RESTYPE_TEXCUBEARRAY; break;
  }

  GLuint texname = texDetails.resource.name;
  GLenum target = texDetails.curType;

  // do blit from renderbuffer to texture, then sample from texture
  if(renderbuffer)
  {
    // need replay context active to do blit (as FBOs aren't shared)
    MakeCurrentReplayContext(&m_ReplayCtx);

    GLuint curDrawFBO = 0;
    GLuint curReadFBO = 0;
    gl.glGetIntegerv(eGL_DRAW_FRAMEBUFFER_BINDING, (GLint *)&curDrawFBO);
    gl.glGetIntegerv(eGL_READ_FRAMEBUFFER_BINDING, (GLint *)&curReadFBO);

    gl.glBindFramebuffer(eGL_DRAW_FRAMEBUFFER, texDetails.renderbufferFBOs[1]);
    gl.glBindFramebuffer(eGL_READ_FRAMEBUFFER, texDetails.renderbufferFBOs[0]);

    gl.glBlitFramebuffer(
        0, 0, texDetails.width, texDetails.height, 0, 0, texDetails.width, texDetails.height,
        GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, eGL_NEAREST);

    gl.glBindFramebuffer(eGL_DRAW_FRAMEBUFFER, curDrawFBO);
    gl.glBindFramebuffer(eGL_READ_FRAMEBUFFER, curReadFBO);

    texname = texDetails.renderbufferReadTex;
    if(resType == RESTYPE_TEX2D)
      target = eGL_TEXTURE_2D;
    else
      target = eGL_TEXTURE_2D_MULTISAMPLE;
  }

  MakeCurrentReplayContext(m_DebugCtx);

  RDCGLenum dsTexMode = eGL_NONE;
  if(IsDepthStencilFormat(texDetails.internalFormat))
  {
    // stencil-only, make sure we display it as such
    if(texDetails.internalFormat == eGL_STENCIL_INDEX8)
    {
      cfg.red = false;
      cfg.green = true;
      cfg.blue = false;
      cfg.alpha = false;
    }

    // depth-only, make sure we display it as such
    if(GetBaseFormat(texDetails.internalFormat) == eGL_DEPTH_COMPONENT)
    {
      cfg.red = true;
      cfg.green = false;
      cfg.blue = false;
      cfg.alpha = false;
    }

    if(!cfg.red && cfg.green)
    {
      dsTexMode = eGL_STENCIL_INDEX;

      // Stencil texture sampling is not normalized in OpenGL
      intIdx = 1;
      float rangeScale = 1.0f;
      switch(texDetails.internalFormat)
      {
        case eGL_STENCIL_INDEX1: rangeScale = 1.0f; break;
        case eGL_STENCIL_INDEX4: rangeScale = 16.0f; break;
        default:
          RDCWARN("Unexpected raw format for stencil visualization");
        // fall through
        case eGL_DEPTH24_STENCIL8:
        case eGL_DEPTH32F_STENCIL8:
        case eGL_STENCIL_INDEX8: rangeScale = 255.0f; break;
        case eGL_STENCIL_INDEX16: rangeScale = 65535.0f; break;
      }
      cfg.rangeMin *= rangeScale;
      cfg.rangeMax *= rangeScale;
    }
    else
      dsTexMode = eGL_DEPTH_COMPONENT;
  }
  else
  {
    if(IsUIntFormat(texDetails.internalFormat))
      intIdx = 1;
    if(IsSIntFormat(texDetails.internalFormat))
      intIdx = 2;
  }

  gl.glBindProgramPipeline(0);
  gl.glUseProgram(DebugData.texDisplayProg[intIdx]);

  int numMips =
      GetNumMips(gl.m_Real, target, texname, texDetails.width, texDetails.height, texDetails.depth);

  GLuint customProgram = 0;

  if(cfg.customShaderId != ResourceId() &&
     gl.GetResourceManager()->HasCurrentResource(cfg.customShaderId))
  {
    GLuint customShader = gl.GetResourceManager()->GetCurrentResource(cfg.customShaderId).name;

    customProgram = gl.glCreateProgram();

    gl.glAttachShader(customProgram, DebugData.texDisplayVertexShader);
    gl.glAttachShader(customProgram, customShader);

    gl.glLinkProgram(customProgram);

    gl.glDetachShader(customProgram, DebugData.texDisplayVertexShader);
    gl.glDetachShader(customProgram, customShader);

    char buffer[1024] = {};
    GLint status = 0;
    gl.glGetProgramiv(customProgram, eGL_LINK_STATUS, &status);
    if(status == 0)
    {
      gl.glGetProgramInfoLog(customProgram, 1024, NULL, buffer);
      RDCERR("Error linking custom shader program: %s", buffer);

      gl.glDeleteProgram(customProgram);
      customProgram = 0;
    }

    if(customProgram)
    {
      gl.glUseProgram(customProgram);

      GLint loc = -1;

      loc = gl.glGetUniformLocation(customProgram, "RENDERDOC_TexDim");
      if(loc >= 0)
        gl.glProgramUniform4ui(customProgram, loc, texDetails.width, texDetails.height,
                               texDetails.depth, (uint32_t)numMips);

      loc = gl.glGetUniformLocation(customProgram, "RENDERDOC_SelectedMip");
      if(loc >= 0)
        gl.glProgramUniform1ui(customProgram, loc, cfg.mip);

      loc = gl.glGetUniformLocation(customProgram, "RENDERDOC_SelectedSliceFace");
      if(loc >= 0)
        gl.glProgramUniform1ui(customProgram, loc, cfg.sliceFace);

      loc = gl.glGetUniformLocation(customProgram, "RENDERDOC_SelectedSample");
      if(loc >= 0)
      {
        if(cfg.sampleIdx == ~0U)
          gl.glProgramUniform1i(customProgram, loc, -texDetails.samples);
        else
          gl.glProgramUniform1i(customProgram, loc,
                                (int)RDCCLAMP(cfg.sampleIdx, 0U, (uint32_t)texDetails.samples - 1));
      }

      loc = gl.glGetUniformLocation(customProgram, "RENDERDOC_TextureType");
      if(loc >= 0)
        gl.glProgramUniform1ui(customProgram, loc, resType);
    }
  }

  gl.glActiveTexture((RDCGLenum)(eGL_TEXTURE0 + resType));
  gl.glBindTexture(target, texname);

  GLint origDSTexMode = eGL_DEPTH_COMPONENT;
  if(dsTexMode != eGL_NONE && HasExt[ARB_stencil_texturing])
  {
    gl.glGetTexParameteriv(target, eGL_DEPTH_STENCIL_TEXTURE_MODE, &origDSTexMode);
    gl.glTexParameteri(target, eGL_DEPTH_STENCIL_TEXTURE_MODE, dsTexMode);
  }

  // defined as arrays mostly for Coverity code analysis to stay calm about passing
  // them to the *TexParameter* functions
  GLint maxlevel[4] = {-1};
  GLint clampmaxlevel[4] = {};

  if(cfg.resourceId != DebugData.CustomShaderTexID)
    clampmaxlevel[0] = GLint(numMips - 1);

  gl.glGetTextureParameterivEXT(texname, target, eGL_TEXTURE_MAX_LEVEL, maxlevel);

  // need to ensure texture is mipmap complete by clamping TEXTURE_MAX_LEVEL.
  if(clampmaxlevel[0] != maxlevel[0] && cfg.resourceId != DebugData.CustomShaderTexID)
  {
    gl.glTextureParameterivEXT(texname, target, eGL_TEXTURE_MAX_LEVEL, clampmaxlevel);
  }
  else
  {
    maxlevel[0] = -1;
  }

  if(cfg.mip == 0 && cfg.scale < 1.0f && dsTexMode == eGL_NONE && resType != RESTYPE_TEXBUFFER &&
     resType != RESTYPE_TEXRECT)
  {
    gl.glBindSampler(resType, DebugData.linearSampler);
  }
  else
  {
    if(resType == RESTYPE_TEXRECT || resType == RESTYPE_TEX2DMS || resType == RESTYPE_TEXBUFFER)
      gl.glBindSampler(resType, DebugData.pointNoMipSampler);
    else
      gl.glBindSampler(resType, DebugData.pointSampler);
  }

  GLint tex_x = texDetails.width, tex_y = texDetails.height, tex_z = texDetails.depth;

  gl.glBindBufferBase(eGL_UNIFORM_BUFFER, 0, DebugData.UBOs[0]);

  TexDisplayUBOData *ubo =
      (TexDisplayUBOData *)gl.glMapBufferRange(eGL_UNIFORM_BUFFER, 0, sizeof(TexDisplayUBOData),
                                               GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

  float x = cfg.xOffset;
  float y = cfg.yOffset;

  ubo->Position.x = x;
  ubo->Position.y = y;
  ubo->Scale = cfg.scale;

  if(cfg.scale <= 0.0f)
  {
    float xscale = DebugData.outWidth / float(tex_x);
    float yscale = DebugData.outHeight / float(tex_y);

    ubo->Scale = RDCMIN(xscale, yscale);

    if(yscale > xscale)
    {
      ubo->Position.x = 0;
      ubo->Position.y = (DebugData.outHeight - (tex_y * ubo->Scale)) * 0.5f;
    }
    else
    {
      ubo->Position.y = 0;
      ubo->Position.x = (DebugData.outWidth - (tex_x * ubo->Scale)) * 0.5f;
    }
  }

  ubo->HDRMul = cfg.hdrMultiplier;

  ubo->FlipY = cfg.flipY ? 1 : 0;

  if(cfg.rangeMax <= cfg.rangeMin)
    cfg.rangeMax += 0.00001f;

  if(dsTexMode == eGL_NONE)
  {
    ubo->Channels.x = cfg.red ? 1.0f : 0.0f;
    ubo->Channels.y = cfg.green ? 1.0f : 0.0f;
    ubo->Channels.z = cfg.blue ? 1.0f : 0.0f;
    ubo->Channels.w = cfg.alpha ? 1.0f : 0.0f;
  }
  else
  {
    // Both depth and stencil texture mode use the red channel
    ubo->Channels.x = 1.0f;
    ubo->Channels.y = 0.0f;
    ubo->Channels.z = 0.0f;
    ubo->Channels.w = 0.0f;
  }

  ubo->RangeMinimum = cfg.rangeMin;
  ubo->InverseRangeSize = 1.0f / (cfg.rangeMax - cfg.rangeMin);

  ubo->MipLevel = (int)cfg.mip;
  if(texDetails.curType != eGL_TEXTURE_3D)
    ubo->Slice = (float)cfg.sliceFace + 0.001f;
  else
    ubo->Slice = (float)(cfg.sliceFace >> cfg.mip);

  ubo->OutputDisplayFormat = resType;

  if(cfg.overlay == DebugOverlay::NaN)
    ubo->OutputDisplayFormat |= TEXDISPLAY_NANS;

  if(cfg.overlay == DebugOverlay::Clipping)
    ubo->OutputDisplayFormat |= TEXDISPLAY_CLIPPING;

  if(!IsSRGBFormat(texDetails.internalFormat) && cfg.linearDisplayAsGamma)
    ubo->OutputDisplayFormat |= TEXDISPLAY_GAMMA_CURVE;

  ubo->RawOutput = cfg.rawOutput ? 1 : 0;

  ubo->TextureResolutionPS.x = float(RDCMAX(1, tex_x >> cfg.mip));
  ubo->TextureResolutionPS.y = float(RDCMAX(1, tex_y >> cfg.mip));
  ubo->TextureResolutionPS.z = float(RDCMAX(1, tex_z >> cfg.mip));

  if(mipShift)
    ubo->MipShift = float(1 << cfg.mip);
  else
    ubo->MipShift = 1.0f;

  ubo->OutputRes.x = DebugData.outWidth;
  ubo->OutputRes.y = DebugData.outHeight;

  ubo->SampleIdx = (int)RDCCLAMP(cfg.sampleIdx, 0U, (uint32_t)texDetails.samples - 1);

  // hacky resolve
  if(cfg.sampleIdx == ~0U)
    ubo->SampleIdx = -texDetails.samples;

  gl.glUnmapBuffer(eGL_UNIFORM_BUFFER);

  if(cfg.rawOutput || !blendAlpha)
  {
    gl.glDisable(eGL_BLEND);
  }
  else
  {
    gl.glEnable(eGL_BLEND);
    gl.glBlendFunc(eGL_SRC_ALPHA, eGL_ONE_MINUS_SRC_ALPHA);
  }

  gl.glDisable(eGL_DEPTH_TEST);

  gl.glEnable(eGL_FRAMEBUFFER_SRGB);

  gl.glBindVertexArray(DebugData.emptyVAO);
  gl.glDrawArrays(eGL_TRIANGLE_STRIP, 0, 4);

  if(maxlevel[0] >= 0)
    gl.glTextureParameterivEXT(texname, target, eGL_TEXTURE_MAX_LEVEL, maxlevel);

  gl.glBindSampler(0, 0);

  if(customProgram)
  {
    gl.glUseProgram(0);
    gl.glDeleteProgram(customProgram);
  }

  if(dsTexMode != eGL_NONE && HasExt[ARB_stencil_texturing])
    gl.glTexParameteri(target, eGL_DEPTH_STENCIL_TEXTURE_MODE, origDSTexMode);

  return true;
}
