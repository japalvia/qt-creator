/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include "qt5nodeinstanceserver.h"

namespace QmlDesigner {

class Qt5PreviewNodeInstanceServer : public Qt5NodeInstanceServer
{
    Q_OBJECT
public:
    explicit Qt5PreviewNodeInstanceServer(NodeInstanceClientInterface *nodeInstanceClient);

    void createScene(const CreateSceneCommand &command) override;
    void changeState(const ChangeStateCommand &command) override;
    void removeSharedMemory(const RemoveSharedMemoryCommand &command) override;
    void changePreviewImageSize(const ChangePreviewImageSizeCommand &command) override;
    bool isPreviewServer() const override;

    QImage renderPreviewImage();

protected:
    void collectItemChangesAndSendChangeCommands() override;
    void startRenderTimer() override;

private:
    ServerNodeInstance m_currentState;
    QSize m_previewSize{160, 160};
};

} // namespace QmlDesigner
