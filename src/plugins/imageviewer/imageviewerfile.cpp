/****************************************************************************
**
** Copyright (C) 2016 Denis Mingulov.
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

#include "imageviewerfile.h"
#include "imageviewer.h"
#include "imageviewerconstants.h"

#include <coreplugin/editormanager/documentmodel.h>
#include <utils/fileutils.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>

#include <QFileInfo>
#include <QGraphicsPixmapItem>
#ifndef QT_NO_SVG
#include <QGraphicsSvgItem>
#endif
#include <QImageReader>
#include <QMovie>
#include <QPainter>
#include <QPixmap>

namespace ImageViewer {
namespace Internal {

class MovieItem : public QObject, public QGraphicsPixmapItem
{
public:
    MovieItem(QMovie *movie)
        : m_movie(movie)
    {
        setPixmap(m_movie->currentPixmap());
        connect(m_movie, &QMovie::updated, this, [this](const QRectF &rect) {
            update(rect);
        });
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const bool smoothTransform = painter->worldTransform().m11() < 1;
        painter->setRenderHint(QPainter::SmoothPixmapTransform, smoothTransform);
        painter->drawPixmap(offset(), m_movie->currentPixmap());
    }

private:
    QMovie *m_movie;
};

ImageViewerFile::ImageViewerFile()
{
    setId(Constants::IMAGEVIEWER_ID);
    connect(this, &ImageViewerFile::mimeTypeChanged, this, &ImageViewerFile::changed);
}

ImageViewerFile::~ImageViewerFile()
{
    cleanUp();
}

Core::IDocument::OpenResult ImageViewerFile::open(QString *errorString,
                                                  const Utils::FilePath &filePath,
                                                  const Utils::FilePath &realfilePath)
{
    QTC_CHECK(filePath == realfilePath); // does not support auto save
    OpenResult success = openImpl(errorString, filePath);
    emit openFinished(success == OpenResult::Success);
    return success;
}

Core::IDocument::OpenResult ImageViewerFile::openImpl(QString *errorString,
                                                      const Utils::FilePath &filePath)
{
    cleanUp();

    if (!filePath.isReadableFile())
        return OpenResult::ReadError;

    const QString &fileName = filePath.toString();
    QByteArray format = QImageReader::imageFormat(fileName);
    // if it is impossible to recognize a file format - file will not be open correctly
    if (format.isEmpty()) {
        if (errorString)
            *errorString = tr("Image format not supported.");
        return OpenResult::CannotHandle;
    }

#ifndef QT_NO_SVG
    if (format.startsWith("svg")) {
        m_tempSvgItem = new QGraphicsSvgItem(fileName);
        QRectF bound = m_tempSvgItem->boundingRect();
        if (qFuzzyIsNull(bound.width()) && qFuzzyIsNull(bound.height())) {
            delete m_tempSvgItem;
            m_tempSvgItem = nullptr;
            if (errorString)
                *errorString = tr("Failed to read SVG image.");
            return OpenResult::CannotHandle;
        }
        m_type = TypeSvg;
        emit imageSizeChanged(m_tempSvgItem->boundingRect().size().toSize());
    } else
#endif
    if (QMovie::supportedFormats().contains(format)) {
        m_type = TypeMovie;
        m_movie = new QMovie(fileName, QByteArray(), this);
        m_movie->setCacheMode(QMovie::CacheAll);
        connect(
            m_movie,
            &QMovie::finished,
            m_movie,
            [this] {
                if (m_movie->isValid())
                    m_movie->start();
            },
            Qt::QueuedConnection);
        connect(m_movie, &QMovie::resized, this, &ImageViewerFile::imageSizeChanged);
        m_movie->start();
        m_isPaused = false; // force update
        setPaused(true);
    } else {
        m_pixmap = new QPixmap(fileName);
        if (m_pixmap->isNull()) {
            if (errorString)
                *errorString = tr("Failed to read image.");
            delete m_pixmap;
            m_pixmap = nullptr;
            return OpenResult::CannotHandle;
        }
        m_type = TypePixmap;
        emit imageSizeChanged(m_pixmap->size());
    }

    setFilePath(filePath);
    setMimeType(Utils::mimeTypeForFile(filePath).name());
    return OpenResult::Success;
}

Core::IDocument::ReloadBehavior ImageViewerFile::reloadBehavior(ChangeTrigger state, ChangeType type) const
{
    if (type == TypeRemoved)
        return BehaviorSilent;
    if (type == TypeContents && state == TriggerInternal && !isModified())
        return BehaviorSilent;
    return BehaviorAsk;
}

bool ImageViewerFile::reload(QString *errorString,
                             Core::IDocument::ReloadFlag flag,
                             Core::IDocument::ChangeType type)
{
    Q_UNUSED(type)
    if (flag == FlagIgnore)
        return true;
    emit aboutToReload();
    bool success = (openImpl(errorString, filePath()) == OpenResult::Success);
    emit reloadFinished(success);
    return success;
}

bool ImageViewerFile::isPaused() const
{
    return m_isPaused;
}

void ImageViewerFile::setPaused(bool paused)
{
    if (!m_movie || m_isPaused == paused)
        return;
    m_isPaused = paused;
    m_movie->setPaused(paused);
    emit isPausedChanged(m_isPaused);
}

QGraphicsItem *ImageViewerFile::createGraphicsItem() const
{
    QGraphicsItem *val = nullptr;
    switch (m_type) {
    case TypeInvalid:
        break;
    case TypeSvg:
#ifndef QT_NO_SVG
        if (m_tempSvgItem) {
            val = m_tempSvgItem;
            m_tempSvgItem = nullptr;
        } else {
            val = new QGraphicsSvgItem(filePath().toString());
        }
#endif
        break;
    case TypeMovie:
        val = new MovieItem(m_movie);
        break;
    case TypePixmap: {
        auto pixmapItem = new QGraphicsPixmapItem(*m_pixmap);
        pixmapItem->setTransformationMode(Qt::SmoothTransformation);
        val = pixmapItem;
        break;
    }
    default:
        break;
    }
    return val;
}

ImageViewerFile::ImageType ImageViewerFile::type() const
{
    return m_type;
}

void ImageViewerFile::updateVisibility()
{
    if (!m_movie || m_isPaused)
        return;
    bool visible = false;
    foreach (Core::IEditor *editor, Core::DocumentModel::editorsForDocument(this)) {
        if (editor->widget()->isVisible()) {
            visible = true;
            break;
        }
    }
    m_movie->setPaused(!visible);
}

void ImageViewerFile::cleanUp()
{
    delete m_pixmap;
    m_pixmap = nullptr;
    delete m_movie;
    m_movie = nullptr;
#ifndef QT_NO_SVG
    delete m_tempSvgItem;
    m_tempSvgItem = nullptr;
#endif
    m_type = TypeInvalid;
}

} // namespace Internal
} // namespace ImageViewer
