/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Baldur Karlsson
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

#pragma once

#include <vector>
#include "d3d12_common.h"
#include "d3d12_manager.h"

class D3D12ResourceManager;

enum SignatureElementType
{
  eRootUnknown,
  eRootConst,
  eRootTable,
  eRootCBV,
  eRootSRV,
  eRootUAV,
};

struct D3D12RenderState
{
  D3D12RenderState();
  D3D12RenderState &operator=(const D3D12RenderState &o);

  void ApplyState(ID3D12GraphicsCommandList *list);

  vector<D3D12_VIEWPORT> views;
  vector<D3D12_RECT> scissors;

  vector<PortableHandle> rts;
  bool rtSingle;
  PortableHandle dsv;

  struct SignatureElement
  {
    SignatureElement() : type(eRootUnknown), offset(0) {}
    SignatureElement(SignatureElementType t, ResourceId i, UINT64 o) : type(t), id(i), offset(o) {}
    SignatureElement(UINT val) : type(eRootConst), offset(0) { constants.push_back(val); }
    SignatureElement(UINT numVals, const void *vals, UINT destIdx)
    {
      SetValues(numVals, vals, destIdx);
    }

    void SetValues(UINT numVals, const void *vals, UINT destIdx)
    {
      type = eRootConst;
      offset = 0;

      if(constants.size() < destIdx + numVals)
        constants.resize(destIdx + numVals);

      memcpy(&constants[destIdx], vals, numVals * sizeof(UINT));
    }

    void SetToCommandList(D3D12ResourceManager *rm, ID3D12GraphicsCommandList *cmd, UINT slot)
    {
      if(type == eRootConst)
      {
        if(constants.size() == 1)
          cmd->SetGraphicsRoot32BitConstant(slot, constants[0], 0);
        else
          cmd->SetGraphicsRoot32BitConstants(slot, (UINT)constants.size(), &constants[0], 0);
      }
      else if(type == eRootTable)
      {
        D3D12_GPU_DESCRIPTOR_HANDLE handle =
            rm->GetCurrentAs<ID3D12DescriptorHeap>(id)->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += sizeof(D3D12Descriptor) * offset;
        cmd->SetGraphicsRootDescriptorTable(slot, handle);
      }
      else if(type == eRootCBV)
      {
        ID3D12Resource *res = rm->GetCurrentAs<ID3D12Resource>(id);
        cmd->SetGraphicsRootConstantBufferView(slot, res->GetGPUVirtualAddress() + offset);
      }
      else if(type == eRootSRV)
      {
        ID3D12Resource *res = rm->GetCurrentAs<ID3D12Resource>(id);
        cmd->SetGraphicsRootShaderResourceView(slot, res->GetGPUVirtualAddress() + offset);
      }
      else if(type == eRootUAV)
      {
        ID3D12Resource *res = rm->GetCurrentAs<ID3D12Resource>(id);
        cmd->SetGraphicsRootUnorderedAccessView(slot, res->GetGPUVirtualAddress() + offset);
      }
      else
      {
        RDCWARN("Unexpected root signature element of type '%u' - skipping.", type);
      }
    }

    SignatureElementType type;

    ResourceId id;
    UINT64 offset;
    vector<UINT> constants;
  };

  vector<ResourceId> heaps;

  struct Pipeline
  {
    ResourceId rootsig;

    vector<SignatureElement> sigelems;
  } compute, graphics;

  ResourceId pipe;

  D3D12_PRIMITIVE_TOPOLOGY topo;

  struct IdxBuffer
  {
    ResourceId buf;
    UINT64 offs;
    int bytewidth;
    UINT size;
  } ibuffer;

  struct VertBuffer
  {
    ResourceId buf;
    UINT64 offs;
    UINT stride;
    UINT size;
  };
  vector<VertBuffer> vbuffers;

  D3D12ResourceManager *GetResourceManager() { return m_ResourceManager; }
  D3D12ResourceManager *m_ResourceManager;
};
