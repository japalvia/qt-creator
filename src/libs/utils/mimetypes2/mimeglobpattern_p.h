/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#pragma once

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/qstringlist.h>
#include <QtCore/qhash.h>

namespace Utils {

struct MimeGlobMatchResult
{
    void addMatch(const QString &mimeType, int weight, const QString &pattern, int knownSuffixLength = 0);

    QStringList m_matchingMimeTypes; // only those with highest weight
    QStringList m_allMatchingMimeTypes;
    int m_weight = 0;
    int m_matchingPatternLength = 0;
    int m_knownSuffixLength = 0;
};

class MimeGlobPattern
{
public:
    static const unsigned MaxWeight = 100;
    static const unsigned DefaultWeight = 50;
    static const unsigned MinWeight = 1;

    explicit MimeGlobPattern(const QString &thePattern, const QString &theMimeType, unsigned theWeight = DefaultWeight, Qt::CaseSensitivity s = Qt::CaseInsensitive) :
        m_pattern(s == Qt::CaseInsensitive ? thePattern.toLower() : thePattern),
        m_mimeType(theMimeType),
        m_weight(theWeight),
        m_caseSensitivity(s),
        m_patternType(detectPatternType(m_pattern))
    {
    }

    void swap(MimeGlobPattern &other) noexcept
    {
        qSwap(m_pattern,         other.m_pattern);
        qSwap(m_mimeType,        other.m_mimeType);
        qSwap(m_weight,          other.m_weight);
        qSwap(m_caseSensitivity, other.m_caseSensitivity);
        qSwap(m_patternType,     other.m_patternType);
    }

    bool matchFileName(const QString &inputFileName) const;

    inline const QString &pattern() const { return m_pattern; }
    inline unsigned weight() const { return m_weight; }
    inline const QString &mimeType() const { return m_mimeType; }
    inline bool isCaseSensitive() const { return m_caseSensitivity == Qt::CaseSensitive; }

private:
    enum PatternType {
        SuffixPattern,
        PrefixPattern,
        LiteralPattern,
        VdrPattern,        // special handling for "[0-9][0-9][0-9].vdr" pattern
        AnimPattern,       // special handling for "*.anim[1-9j]" pattern
        OtherPattern
    };
    PatternType detectPatternType(const QString &pattern) const;

    QString m_pattern;
    QString m_mimeType;
    int m_weight;
    Qt::CaseSensitivity m_caseSensitivity;
    PatternType m_patternType;
};

class MimeGlobPatternList : public QList<MimeGlobPattern>
{
public:
    bool hasPattern(const QString &mimeType, const QString &pattern) const
    {
        const_iterator it = begin();
        const const_iterator myend = end();
        for (; it != myend; ++it)
            if ((*it).pattern() == pattern && (*it).mimeType() == mimeType)
                return true;
        return false;
    }

    /*!
        "noglobs" is very rare occurrence, so it's ok if it's slow
     */
    void removeMimeType(const QString &mimeType)
    {
        auto isMimeTypeEqual = [&mimeType](const MimeGlobPattern &pattern) {
            return pattern.mimeType() == mimeType;
        };
        erase(std::remove_if(begin(), end(), isMimeTypeEqual), end());
    }

    void match(MimeGlobMatchResult &result, const QString &fileName) const;
};

/*!
    Result of the globs parsing, as data structures ready for efficient MIME type matching.
    This contains:
    1) a map of fast regular patterns (e.g. *.txt is stored as "txt" in a qhash's key)
    2) a linear list of high-weight globs
    3) a linear list of low-weight globs
 */
class MimeAllGlobPatterns
{
public:
    typedef QHash<QString, QStringList> PatternsMap; // MIME type -> patterns

    void addGlob(const MimeGlobPattern &glob);
    void removeMimeType(const QString &mimeType);
    void matchingGlobs(const QString &fileName, MimeGlobMatchResult &result) const;
    void clear();

    PatternsMap m_fastPatterns; // example: "doc" -> "application/msword", "text/plain"
    MimeGlobPatternList m_highWeightGlobs;
    MimeGlobPatternList m_lowWeightGlobs; // <= 50, including the non-fast 50 patterns
};

} // namespace Utils

Q_DECLARE_SHARED(Utils::MimeGlobPattern)
