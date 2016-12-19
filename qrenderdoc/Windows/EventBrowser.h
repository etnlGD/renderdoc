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

#include <QFrame>
#include <QIcon>
#include "Code/CaptureContext.h"

namespace Ui
{
class EventBrowser;
}

class QTreeWidgetItem;
class QTimer;
class SizeDelegate;

class EventBrowser : public QFrame, public ILogViewerForm
{
private:
  Q_OBJECT

public:
  explicit EventBrowser(CaptureContext *ctx, QWidget *parent = 0);
  ~EventBrowser();

  void OnLogfileLoaded();
  void OnLogfileClosed();
  void OnEventSelected(uint32_t eventID);

private slots:
  // automatic slots
  void on_find_clicked();
  void on_gotoEID_clicked();
  void on_timeDraws_clicked();
  void on_toolButton_clicked();
  void on_HideFindJump();
  void on_jumpToEID_returnPressed();
  void on_findEvent_returnPressed();
  void on_findEvent_textEdited(const QString &arg1);
  void on_events_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
  void on_findNext_clicked();
  void on_findPrev_clicked();

  // manual slots
  void findHighlight_timeout();

private:
  uint AddDrawcalls(QTreeWidgetItem *parent, const rdctype::array<FetchDrawcall> &draws);
  void SetDrawcallTimes(QTreeWidgetItem *node, const rdctype::array<CounterResult> &results);

  void ExpandNode(QTreeWidgetItem *node);

  bool FindEventNode(QTreeWidgetItem *&found, QTreeWidgetItem *parent, uint32_t eventID);
  bool SelectEvent(uint32_t eventID);

  void ClearFindIcons(QTreeWidgetItem *parent);
  void ClearFindIcons();

  int SetFindIcons(QTreeWidgetItem *parent, QString filter);
  int SetFindIcons(QString filter);

  QTreeWidgetItem *FindNode(QTreeWidgetItem *parent, QString filter, uint32_t after);
  int FindEvent(QTreeWidgetItem *parent, QString filter, uint32_t after, bool forward);
  int FindEvent(QString filter, uint32_t after, bool forward);
  void Find(bool forward);

  QIcon m_CurrentIcon;
  QIcon m_FindIcon;
  QIcon m_BookmarkIcon;

  SizeDelegate *m_SizeDelegate;
  QTimer *m_FindHighlight;

  void RefreshIcon(QTreeWidgetItem *item);

  Ui::EventBrowser *ui;
  CaptureContext *m_Ctx;
};
