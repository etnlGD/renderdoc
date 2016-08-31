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

#include "d3d12_command_queue.h"
#include "d3d12_command_list.h"
#include "d3d12_resources.h"

void STDMETHODCALLTYPE WrappedID3D12CommandQueue::UpdateTileMappings(
    ID3D12Resource *pResource, UINT NumResourceRegions,
    const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
    const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, ID3D12Heap *pHeap, UINT NumRanges,
    const D3D12_TILE_RANGE_FLAGS *pRangeFlags, const UINT *pHeapRangeStartOffsets,
    const UINT *pRangeTileCounts, D3D12_TILE_MAPPING_FLAGS Flags)
{
  m_pReal->UpdateTileMappings(Unwrap(pResource), NumResourceRegions, pResourceRegionStartCoordinates,
                              pResourceRegionSizes, Unwrap(pHeap), NumRanges, pRangeFlags,
                              pHeapRangeStartOffsets, pRangeTileCounts, Flags);
}

void STDMETHODCALLTYPE WrappedID3D12CommandQueue::CopyTileMappings(
    ID3D12Resource *pDstResource, const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate,
    ID3D12Resource *pSrcResource, const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate,
    const D3D12_TILE_REGION_SIZE *pRegionSize, D3D12_TILE_MAPPING_FLAGS Flags)
{
  m_pReal->CopyTileMappings(Unwrap(pDstResource), pDstRegionStartCoordinate, Unwrap(pSrcResource),
                            pSrcRegionStartCoordinate, pRegionSize, Flags);
}

bool WrappedID3D12CommandQueue::Serialise_ExecuteCommandLists(UINT NumCommandLists,
                                                              ID3D12CommandList *const *ppCommandLists)
{
  SERIALISE_ELEMENT(UINT, numCmds, NumCommandLists);

  vector<ResourceId> cmdIds;
  ID3D12CommandList **cmds = m_State >= WRITING ? NULL : new ID3D12CommandList *[numCmds];

  if(m_State >= WRITING)
  {
    for(UINT i = 0; i < numCmds; i++)
    {
      D3D12ResourceRecord *record = GetRecord(ppCommandLists[i]);
      RDCASSERT(record->bakedCommands);
      if(record->bakedCommands)
        cmdIds.push_back(record->bakedCommands->GetResourceID());
    }
  }

  m_pSerialiser->Serialise("ppCommandLists", cmdIds);

  if(m_State < WRITING)
  {
    for(UINT i = 0; i < numCmds; i++)
    {
      cmds[i] = cmdIds[i] != ResourceId()
                    ? Unwrap(GetResourceManager()->GetLiveAs<ID3D12CommandList>(cmdIds[i]))
                    : NULL;
    }
  }

  const string desc = m_pSerialiser->GetDebugStr();

  D3D12NOTIMP("Serialise_DebugMessages");

  if(m_State == READING)
  {
    m_pReal->ExecuteCommandLists(numCmds, cmds);

    for(uint32_t i = 0; i < numCmds; i++)
    {
      ResourceId cmd = GetResourceManager()->GetLiveID(cmdIds[i]);
      m_pDevice->ApplyBarriers(m_Cmd.m_BakedCmdListInfo[cmd].barriers);
    }

    m_Cmd.AddEvent(EXECUTE_CMD_LISTS, desc);

    // we're adding multiple events, need to increment ourselves
    m_Cmd.m_RootEventID++;

    string basename = "ExecuteCommandLists(" + ToStr::Get(numCmds) + ")";

    for(uint32_t c = 0; c < numCmds; c++)
    {
      string name = StringFormat::Fmt("=> %s[%u]: ID3D12CommandList(%s)", basename.c_str(), c,
                                      ToStr::Get(cmdIds[c]).c_str());

      // add a fake marker
      FetchDrawcall draw;
      draw.name = name;
      draw.flags |= eDraw_SetMarker;
      m_Cmd.AddEvent(SET_MARKER, name);
      m_Cmd.AddDrawcall(draw, true);
      m_Cmd.m_RootEventID++;

      BakedCmdListInfo &cmdBufInfo = m_Cmd.m_BakedCmdListInfo[cmdIds[c]];

      // insert the baked command list in-line into this list of notes, assigning new event and
      // drawIDs
      m_Cmd.InsertDrawsAndRefreshIDs(cmdBufInfo.draw->children);

      for(size_t e = 0; e < cmdBufInfo.draw->executedCmds.size(); e++)
      {
        vector<uint32_t> &submits =
            m_Cmd.m_Partial[D3D12CommandData::Secondary].cmdListExecs[cmdBufInfo.draw->executedCmds[e]];

        for(size_t s = 0; s < submits.size(); s++)
          submits[s] += m_Cmd.m_RootEventID;
      }

      D3D12NOTIMP("Debug Messages");
      /*
      for(size_t i = 0; i < cmdBufInfo.debugMessages.size(); i++)
      {
        m_DebugMessages.push_back(cmdBufInfo.debugMessages[i]);
        m_DebugMessages.back().eventID += m_RootEventID;
      }
      */

      // only primary command lists can be submitted
      m_Cmd.m_Partial[D3D12CommandData::Primary].cmdListExecs[cmdIds[c]].push_back(
          m_Cmd.m_RootEventID);

      m_Cmd.m_RootEventID += cmdBufInfo.eventCount;
      m_Cmd.m_RootDrawcallID += cmdBufInfo.drawCount;

      name = StringFormat::Fmt("=> %s[%u]: Close(%s)", basename.c_str(), c,
                               ToStr::Get(cmdIds[c]).c_str());
      draw.name = name;
      m_Cmd.AddEvent(SET_MARKER, name);
      m_Cmd.AddDrawcall(draw, true);
      m_Cmd.m_RootEventID++;
    }

    // account for the outer loop thinking we've added one event and incrementing,
    // since we've done all the handling ourselves this will be off by one.
    m_Cmd.m_RootEventID--;
  }
  else if(m_State == EXECUTING)
  {
    // account for the queue submit event
    m_Cmd.m_RootEventID++;

    uint32_t startEID = m_Cmd.m_RootEventID;

    // advance m_CurEventID to match the events added when reading
    for(uint32_t c = 0; c < numCmds; c++)
    {
      // 2 extra for the virtual labels around the command list
      m_Cmd.m_RootEventID += 2 + m_Cmd.m_BakedCmdListInfo[cmdIds[c]].eventCount;
      m_Cmd.m_RootDrawcallID += 2 + m_Cmd.m_BakedCmdListInfo[cmdIds[c]].drawCount;
    }

    // same accounting for the outer loop as above
    m_Cmd.m_RootEventID--;

    if(numCmds == 0)
    {
      // do nothing, don't bother with the logic below
    }
    else if(m_Cmd.m_LastEventID <= startEID)
    {
#ifdef VERBOSE_PARTIAL_REPLAY
      RDCDEBUG("Queue Submit no replay %u == %u", m_Cmd.m_LastEventID, startEID);
#endif
    }
    else if(m_Cmd.m_DrawcallCallback && m_Cmd.m_DrawcallCallback->RecordAllCmds())
    {
#ifdef VERBOSE_PARTIAL_REPLAY
      RDCDEBUG("Queue Submit re-recording from %u", m_Cmd.m_RootEventID);
#endif

      vector<ID3D12CommandList *> rerecordedCmds;

      for(uint32_t c = 0; c < numCmds; c++)
      {
        ID3D12CommandList *cmd = m_Cmd.RerecordCmdList(cmdIds[c]);
        ResourceId rerecord = GetResID(cmd);
#ifdef VERBOSE_PARTIAL_REPLAY
        RDCDEBUG("Queue Submit fully re-recorded replay of %llu, using %llu", cmdIds[c], rerecord);
#endif
        rerecordedCmds.push_back(Unwrap(cmd));

        m_pDevice->ApplyBarriers(m_Cmd.m_BakedCmdListInfo[rerecord].barriers);
      }

      m_pReal->ExecuteCommandLists((UINT)rerecordedCmds.size(), &rerecordedCmds[0]);
    }
    else if(m_Cmd.m_LastEventID > startEID && m_Cmd.m_LastEventID < m_Cmd.m_RootEventID)
    {
#ifdef VERBOSE_PARTIAL_REPLAY
      RDCDEBUG("Queue Submit partial replay %u < %u", m_Cmd.m_LastEventID, m_Cmd.m_RootEventID);
#endif

      uint32_t eid = startEID;

      vector<ResourceId> trimmedCmdIds;
      vector<ID3D12CommandList *> trimmedCmds;

      for(uint32_t c = 0; c < numCmds; c++)
      {
        // account for the virtual label at the start of the events here
        // so it matches up to baseEvent
        eid++;

        uint32_t end = eid + m_Cmd.m_BakedCmdListInfo[cmdIds[c]].eventCount;

        if(eid == m_Cmd.m_Partial[D3D12CommandData::Primary].baseEvent)
        {
          ID3D12GraphicsCommandList *list =
              m_Cmd.RerecordCmdList(cmdIds[c], D3D12CommandData::Primary);
          ResourceId partial = GetResID(list);
#ifdef VERBOSE_PARTIAL_REPLAY
          RDCDEBUG("Queue Submit partial replay of %llu at %u, using %llu", cmdIds[c], eid, partial);
#endif
          trimmedCmdIds.push_back(partial);
          trimmedCmds.push_back(Unwrap(list));
        }
        else if(m_Cmd.m_LastEventID >= end)
        {
#ifdef VERBOSE_PARTIAL_REPLAY
          RDCDEBUG("Queue Submit full replay %llu", cmdIds[c]);
#endif
          trimmedCmdIds.push_back(cmdIds[c]);
          trimmedCmds.push_back(Unwrap(GetResourceManager()->GetLiveAs<ID3D12CommandList>(cmdIds[c])));
        }
        else
        {
#ifdef VERBOSE_PARTIAL_REPLAY
          RDCDEBUG("Queue not submitting %llu", cmdIds[c]);
#endif
        }

        // 1 extra to account for the virtual end command list label (begin is accounted for
        // above)
        eid += 1 + m_Cmd.m_BakedCmdListInfo[cmdIds[c]].eventCount;
      }

      RDCASSERT(trimmedCmds.size() > 0);

      m_pReal->ExecuteCommandLists((UINT)trimmedCmds.size(), &trimmedCmds[0]);

      for(uint32_t i = 0; i < trimmedCmdIds.size(); i++)
      {
        ResourceId cmd = trimmedCmdIds[i];
        m_pDevice->ApplyBarriers(m_Cmd.m_BakedCmdListInfo[cmd].barriers);
      }
    }
    else
    {
#ifdef VERBOSE_PARTIAL_REPLAY
      RDCDEBUG("Queue Submit full replay %u >= %u", m_Cmd.m_LastEventID, m_Cmd.m_RootEventID);
#endif

      m_pReal->ExecuteCommandLists(numCmds, cmds);

      for(uint32_t i = 0; i < numCmds; i++)
      {
        ResourceId cmd = GetResourceManager()->GetLiveID(cmdIds[i]);
        m_pDevice->ApplyBarriers(m_Cmd.m_BakedCmdListInfo[cmd].barriers);
      }
    }
  }

  SAFE_DELETE_ARRAY(cmds);

  return true;
}

void STDMETHODCALLTYPE WrappedID3D12CommandQueue::ExecuteCommandLists(
    UINT NumCommandLists, ID3D12CommandList *const *ppCommandLists)
{
  ID3D12CommandList **unwrapped = new ID3D12CommandList *[NumCommandLists];
  for(UINT i = 0; i < NumCommandLists; i++)
    unwrapped[i] = Unwrap(ppCommandLists[i]);

  m_pReal->ExecuteCommandLists(NumCommandLists, unwrapped);

  SAFE_DELETE_ARRAY(unwrapped);

  if(m_State >= WRITING)
  {
    bool capframe = false;
    set<ResourceId> refdIDs;

    for(UINT i = 0; i < NumCommandLists; i++)
    {
      D3D12ResourceRecord *record = GetRecord(ppCommandLists[i]);

      m_pDevice->ApplyBarriers(record->bakedCommands->cmdInfo->barriers);

      // need to lock the whole section of code, not just the check on
      // m_State, as we also need to make sure we don't check the state,
      // start marking dirty resources then while we're doing so the
      // state becomes capframe.
      // the next sections where we mark resources referenced and add
      // the submit chunk to the frame record don't have to be protected.
      // Only the decision of whether we're inframe or not, and marking
      // dirty.
      {
        SCOPED_LOCK(m_pDevice->GetCapTransitionLock());
        if(m_State == WRITING_CAPFRAME)
        {
          for(auto it = record->bakedCommands->cmdInfo->dirtied.begin();
              it != record->bakedCommands->cmdInfo->dirtied.end(); ++it)
            GetResourceManager()->MarkPendingDirty(*it);

          capframe = true;
        }
        else
        {
          for(auto it = record->bakedCommands->cmdInfo->dirtied.begin();
              it != record->bakedCommands->cmdInfo->dirtied.end(); ++it)
            GetResourceManager()->MarkDirtyResource(*it);
        }
      }

      if(capframe)
      {
        // for each bound descriptor table, mark it referenced as well as all resources currently
        // bound to it
        for(auto it = record->bakedCommands->cmdInfo->boundDescs.begin();
            it != record->bakedCommands->cmdInfo->boundDescs.end(); ++it)
        {
          D3D12Descriptor &desc = **it;

          switch(desc.GetType())
          {
            case D3D12Descriptor::TypeUndefined:
            case D3D12Descriptor::TypeSampler:
              // nothing to do - no resource here
              break;
            case D3D12Descriptor::TypeCBV:
              GetResourceManager()->MarkResourceFrameReferenced(
                  WrappedID3D12Resource::GetResIDFromAddr(desc.nonsamp.cbv.BufferLocation),
                  eFrameRef_Read);
              break;
            case D3D12Descriptor::TypeSRV:
              GetResourceManager()->MarkResourceFrameReferenced(GetResID(desc.nonsamp.resource),
                                                                eFrameRef_Read);
              break;
            case D3D12Descriptor::TypeUAV:
              GetResourceManager()->MarkResourceFrameReferenced(GetResID(desc.nonsamp.resource),
                                                                eFrameRef_Write);
              GetResourceManager()->MarkResourceFrameReferenced(
                  GetResID(desc.nonsamp.uav.counterResource), eFrameRef_Write);
              break;
            case D3D12Descriptor::TypeRTV:
              GetResourceManager()->MarkResourceFrameReferenced(GetResID(desc.nonsamp.resource),
                                                                eFrameRef_Write);
              break;
            case D3D12Descriptor::TypeDSV:
              GetResourceManager()->MarkResourceFrameReferenced(GetResID(desc.nonsamp.resource),
                                                                eFrameRef_Write);
              break;
          }
        }

        // pull in frame refs from this baked command list
        record->bakedCommands->AddResourceReferences(GetResourceManager());
        record->bakedCommands->AddReferencedIDs(refdIDs);

        // ref the parent command list by itself, this will pull in the cmd buffer pool
        GetResourceManager()->MarkResourceFrameReferenced(record->GetResourceID(), eFrameRef_Read);

        // reference all executed bundles as well
        for(size_t b = 0; b < record->bakedCommands->cmdInfo->bundles.size(); b++)
        {
          record->bakedCommands->cmdInfo->bundles[b]->bakedCommands->AddResourceReferences(
              GetResourceManager());
          record->bakedCommands->cmdInfo->bundles[b]->bakedCommands->AddReferencedIDs(refdIDs);
          GetResourceManager()->MarkResourceFrameReferenced(
              record->bakedCommands->cmdInfo->bundles[b]->GetResourceID(), eFrameRef_Read);

          record->bakedCommands->cmdInfo->bundles[b]->bakedCommands->AddRef();
        }

        {
          m_CmdListRecords.push_back(record->bakedCommands);
          for(size_t sub = 0; sub < record->bakedCommands->cmdInfo->bundles.size(); sub++)
            m_CmdListRecords.push_back(record->bakedCommands->cmdInfo->bundles[sub]->bakedCommands);
        }

        record->bakedCommands->AddRef();
      }

      record->cmdInfo->dirtied.clear();
    }

    if(capframe)
    {
      // flush coherent maps
      for(UINT i = 0; i < NumCommandLists; i++)
      {
        SCOPED_SERIALISE_CONTEXT(EXECUTE_CMD_LISTS);
        Serialise_ExecuteCommandLists(1, ppCommandLists + i);

        m_QueueRecord->AddChunk(scope.Get());
      }
    }
  }
}

void STDMETHODCALLTYPE WrappedID3D12CommandQueue::SetMarker(UINT Metadata, const void *pData,
                                                            UINT Size)
{
  m_pReal->SetMarker(Metadata, pData, Size);
}

void STDMETHODCALLTYPE WrappedID3D12CommandQueue::BeginEvent(UINT Metadata, const void *pData,
                                                             UINT Size)
{
  m_pReal->BeginEvent(Metadata, pData, Size);
}

void STDMETHODCALLTYPE WrappedID3D12CommandQueue::EndEvent()
{
  m_pReal->EndEvent();
}

bool WrappedID3D12CommandQueue::Serialise_Signal(ID3D12Fence *pFence, UINT64 Value)
{
  SERIALISE_ELEMENT(ResourceId, Fence, GetResID(pFence));
  SERIALISE_ELEMENT(UINT64, val, Value);

  if(m_State <= EXECUTING && GetResourceManager()->HasLiveResource(Fence))
  {
    pFence = GetResourceManager()->GetLiveAs<ID3D12Fence>(Fence);

    m_pReal->Signal(Unwrap(pFence), val);
  }

  return true;
}

HRESULT STDMETHODCALLTYPE WrappedID3D12CommandQueue::Signal(ID3D12Fence *pFence, UINT64 Value)
{
  if(m_State == WRITING_CAPFRAME)
  {
    SCOPED_SERIALISE_CONTEXT(SIGNAL);
    Serialise_Signal(pFence, Value);

    m_QueueRecord->AddChunk(scope.Get());
    GetResourceManager()->MarkResourceFrameReferenced(GetResID(pFence), eFrameRef_Read);
  }

  return m_pReal->Signal(Unwrap(pFence), Value);
}

HRESULT STDMETHODCALLTYPE WrappedID3D12CommandQueue::Wait(ID3D12Fence *pFence, UINT64 Value)
{
  return m_pReal->Wait(Unwrap(pFence), Value);
}

HRESULT STDMETHODCALLTYPE WrappedID3D12CommandQueue::GetTimestampFrequency(UINT64 *pFrequency)
{
  return m_pReal->GetTimestampFrequency(pFrequency);
}

HRESULT STDMETHODCALLTYPE WrappedID3D12CommandQueue::GetClockCalibration(UINT64 *pGpuTimestamp,
                                                                         UINT64 *pCpuTimestamp)
{
  return m_pReal->GetClockCalibration(pGpuTimestamp, pCpuTimestamp);
}